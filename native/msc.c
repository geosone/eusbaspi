/* SPDX-License-Identifier: GPL-2.0-or-later */
/* USB mass storage for OpenASPI's native layer (polled).
 *
 * The transports of EUSBASPI.SYS's usb_scsi: Bulk-Only (CBW, data, CSW,
 * reset recovery on phase errors, a stalled data phase cleared and the CSW
 * read anyway, a stalled CSW read twice) and CBI/CB (ADSC on EP0, status
 * from the interrupt endpoint for CBI; UFI reports ASC/ASCQ there).
 * Requests run through OpenASPI's packet device handling: autosense with
 * REQUEST SENSE and, for the ATAPI, UFI and SFF-8070i command sets, the
 * 6-to-10-byte rules and 12-byte packets. Every LUN is its own target. */

#include <string.h>

#include "eusbaspi/native.h"
#include "eusbcore/usb.h"
#include "openaspi/native/atapi.h"
#include "openaspi/native/hba.h"
#include "openaspi/native/host.h"
#include "openaspi/scsi.h"

#define MAX_TARGETS 16
#define MAX_LUNS    8

#define PROTO_CBI 0x00u
#define PROTO_CB  0x01u
#define PROTO_BOT 0x50u

struct msc {
    struct usb_dev *dev;
    oa_u8 iface, subclass, proto;
    oa_u8 ep_in, ep_out, ep_int;
    oa_u32 tag;
};

struct target {
    struct msc *m;
    oa_u8 lun;
    struct oa_atapi pkt;
};

static struct msc mscs[USB_MAX_DEVICES];
static unsigned nmscs;
static struct target targets[MAX_TARGETS];
static unsigned ntargets;
static struct oa_hba hba;
static oa_u8 cbw[31], csw[13];

/* command length for the opcode; ATAPI/UFI/SFF devices take 12 bytes */
static oa_u8 cdb_len(const struct msc *m, oa_u8 opcode)
{
    static const oa_u8 group[8] = { 6, 10, 10, 12, 16, 12, 12, 12 };
    oa_u8 n = group[opcode >> 5];

    if ((m->subclass == 2u || m->subclass == 4u || m->subclass == 5u) && n < 12u) {
        n = 12;
    }
    return n;
}

static void reset_recovery(struct msc *m)
{
    oa_u8 cmd[12];

    if (m->proto == PROTO_BOT) {
        usb_control(m->dev, 0x21u, 0xFFu, 0, m->iface, 0, 0, 0); /* Bulk-Only reset */
    } else {
        memset(cmd, 0xFF, sizeof(cmd)); /* command block reset */
        cmd[0] = 0x1Du;
        cmd[1] = 0x04u;
        usb_control(m->dev, 0x21u, 0x00u, 0, m->iface, cmd, sizeof(cmd), 0);
    }
    usb_clear_halt(m->dev, (oa_u8)(m->ep_in | 0x80u));
    usb_clear_halt(m->dev, m->ep_out);
}

static int host_error(int r)
{
    return r == USB_TIMEOUT ? OA_HOST_TIMEOUT : OA_HOST_BUS_ERROR;
}

/* Data phase in pieces the controller can take. Returns USB_*; *moved. */
static int data_phase(struct msc *m, oa_u8 dir, oa_u8 *p, oa_u32 len, oa_u32 timeout,
                      oa_u32 *moved)
{
    struct usb_hc *hc = m->dev->hc;
    oa_u32 n, got;
    int r;

    *moved = 0;
    while (len > 0u) {
        n = len < hc->max_xfer ? len : hc->max_xfer;
        got = 0;
        r = usb_transfer(m->dev, dir == OA_DIR_IN ? (oa_u8)(m->ep_in | 0x80u) : m->ep_out, p, n,
                         &got, timeout);
        *moved += got;
        if (r != USB_OK) {
            return r;
        }
        if (got < n) {
            break; /* short packet */
        }
        p += n;
        len -= n;
    }
    return USB_OK;
}

static int bot_packet(struct target *t, const oa_u8 packet[16], oa_u8 dir, void *data,
                      oa_u32 len, oa_u32 timeout, oa_u8 *status, oa_u8 *error, oa_u32 *xfer)
{
    struct msc *m = t->m;
    oa_u32 got, residue, tag = ++m->tag;
    unsigned i;
    int r;

    memset(cbw, 0, sizeof(cbw));
    cbw[0] = 'U';
    cbw[1] = 'S';
    cbw[2] = 'B';
    cbw[3] = 'C';
    cbw[4] = (oa_u8)tag;
    cbw[5] = (oa_u8)(tag >> 8);
    cbw[6] = (oa_u8)(tag >> 16);
    cbw[7] = (oa_u8)(tag >> 24);
    cbw[8] = (oa_u8)len;
    cbw[9] = (oa_u8)(len >> 8);
    cbw[10] = (oa_u8)(len >> 16);
    cbw[11] = (oa_u8)(len >> 24);
    cbw[12] = (len != 0u && dir == OA_DIR_IN) ? 0x80u : 0x00u;
    cbw[13] = t->lun;
    cbw[14] = cdb_len(m, packet[0]);
    memcpy(cbw + 15, packet, 16);
    r = usb_transfer(m->dev, m->ep_out, cbw, 31, &got, 5000);
    if (r != USB_OK) {
        reset_recovery(m);
        return host_error(r);
    }
    if (len != 0u) {
        r = data_phase(m, dir, data, len, timeout, xfer);
        if (r == USB_STALL) { /* clear it; the CSW tells the result */
            usb_clear_halt(m->dev, dir == OA_DIR_IN ? (oa_u8)(m->ep_in | 0x80u) : m->ep_out);
        } else if (r != USB_OK) {
            reset_recovery(m);
            return host_error(r);
        }
    }
    for (i = 0; i < 2u; i++) {
        memset(csw, 0, sizeof(csw));
        r = usb_transfer(m->dev, (oa_u8)(m->ep_in | 0x80u), csw, 13, &got, timeout);
        if (r != USB_STALL) {
            break;
        }
        usb_clear_halt(m->dev, (oa_u8)(m->ep_in | 0x80u));
    }
    if (r != USB_OK) {
        reset_recovery(m);
        return host_error(r);
    }
    if (got < 13u || csw[0] != 'U' || csw[1] != 'S' || csw[2] != 'B' || csw[3] != 'S' ||
        oa_get_le32(csw + 4) != tag || csw[12] > 1u) {
        reset_recovery(m); /* phase error or a broken CSW */
        return OA_HOST_BUS_ERROR;
    }
    residue = oa_get_le32(csw + 8);
    if (residue <= len && len - residue < *xfer) {
        *xfer = len - residue;
    }
    *status = csw[12] ? 0x01u : 0x00u; /* CHECK CONDITION */
    *error = 0;
    return OA_HOST_OK;
}

static int cbi_packet(struct target *t, const oa_u8 packet[16], oa_u8 dir, void *data,
                      oa_u32 len, oa_u32 timeout, oa_u8 *status, oa_u8 *error, oa_u32 *xfer)
{
    struct msc *m = t->m;
    oa_u8 cmd[16], intr[2];
    oa_u32 got;
    int r;

    *status = 0;
    *error = 0;
    memcpy(cmd, packet, 16);
    r = usb_control(m->dev, 0x21u, 0x00u, 0, m->iface, cmd, cdb_len(m, packet[0]), 0);
    if (r == USB_STALL) {
        *status = 0x01u; /* command refused */
        return OA_HOST_OK;
    }
    if (r != USB_OK) {
        return host_error(r);
    }
    if (len != 0u) {
        r = data_phase(m, dir, data, len, timeout, xfer);
        if (r == USB_STALL) {
            usb_clear_halt(m->dev, dir == OA_DIR_IN ? (oa_u8)(m->ep_in | 0x80u) : m->ep_out);
            *status = 0x01u;
            return OA_HOST_OK;
        }
        if (r != USB_OK) {
            return host_error(r);
        }
    }
    if (m->proto != PROTO_CBI || m->ep_int == 0u) {
        return OA_HOST_OK; /* CB: no stall means good */
    }
    memset(intr, 0, sizeof(intr));
    r = usb_transfer(m->dev, (oa_u8)(m->ep_int | 0x80u), intr, 2, &got, timeout);
    if (r == USB_STALL) {
        usb_clear_halt(m->dev, (oa_u8)(m->ep_int | 0x80u));
        *status = 0x01u;
        return OA_HOST_OK;
    }
    if (r != USB_OK) {
        return host_error(r);
    }
    if (m->subclass == 4u) { /* UFI: ASC/ASCQ; INQUIRY and REQUEST SENSE leave it alone */
        if (packet[0] != OA_SCSI_INQUIRY && packet[0] != OA_SCSI_REQUEST_SENSE &&
            (intr[0] | intr[1]) != 0u) {
            *status = 0x01u;
        }
        return OA_HOST_OK;
    }
    if (intr[0] != 0u || (intr[1] & 3u) > 1u) {
        return OA_HOST_BUS_ERROR;
    }
    if ((intr[1] & 3u) == 1u) {
        *status = 0x01u;
    }
    return OA_HOST_OK;
}

static int packet(void *ctx, const oa_u8 pk[16], oa_u8 dir, void *data, oa_u32 len,
                  oa_u32 timeout_ms, oa_u8 *status, oa_u8 *error, oa_u32 *xfer)
{
    struct target *t = ctx;
    oa_u32 timeout = timeout_ms ? timeout_ms : 15000u;

    *xfer = 0;
    if (dir == OA_DIR_NONE) {
        len = 0;
    }
    *xfer = len; /* reduced by short packets and the residue */
    if (t->m->proto == PROTO_BOT) {
        oa_u32 moved = len;
        int r = bot_packet(t, pk, dir, data, len, timeout, status, error, &moved);
        *xfer = moved;
        return r;
    }
    {
        oa_u32 moved = len;
        int r = cbi_packet(t, pk, dir, data, len, timeout, status, error, &moved);
        *xfer = moved;
        return r;
    }
}

static void msc_execute(struct oa_hba *h, struct oa_srb *srb)
{
    (void)h;
    if (srb->target >= ntargets) {
        srb->host_status = OA_HOST_NO_DEVICE;
        return;
    }
    oa_atapi_execute(&targets[srb->target].pkt, srb);
}

static int msc_reset(struct oa_hba *h, oa_u8 target)
{
    (void)h;
    if (target < ntargets) {
        reset_recovery(targets[target].m);
    }
    return OA_HOST_OK;
}

static const struct oa_hba_ops msc_ops = { msc_execute, msc_reset };

static void probe_device(struct usb_dev *dev)
{
    const oa_u8 *d, *e;
    oa_u16 pos = 0, epos;
    struct msc *m;
    oa_u8 maxlun = 0, lun;
    oa_u16 got;

    while ((d = usb_find_desc(dev, 4u, &pos)) != 0) { /* interface */
        if (d[0] < 9u || d[5] != 8u || d[3] != 0u) {
            continue; /* not mass storage, or an alternate setting */
        }
        if (d[7] != PROTO_BOT && d[7] != PROTO_CBI && d[7] != PROTO_CB) {
            continue;
        }
        if (nmscs == USB_MAX_DEVICES) {
            return;
        }
        m = &mscs[nmscs];
        memset(m, 0, sizeof(*m));
        m->dev = dev;
        m->iface = d[2];
        m->subclass = d[6];
        m->proto = d[7];
        /* the endpoints up to the next interface descriptor */
        for (epos = (oa_u16)(pos + d[0]); epos + 2u <= dev->config_len; epos = (oa_u16)(epos + e[0])) {
            e = dev->config_desc + epos;
            if (e[0] < 2u || e[1] == 4u) {
                break;
            }
            if (e[1] != 5u || e[0] < 7u) {
                continue;
            }
            if ((e[3] & 3u) == USB_EP_BULK) {
                if (e[2] & 0x80u) {
                    if (!m->ep_in) {
                        m->ep_in = (oa_u8)(e[2] & 15u);
                    }
                } else if (!m->ep_out) {
                    m->ep_out = (oa_u8)(e[2] & 15u);
                }
            } else if ((e[3] & 3u) == USB_EP_INTR && (e[2] & 0x80u) && !m->ep_int) {
                m->ep_int = (oa_u8)(e[2] & 15u);
            }
        }
        if (!m->ep_in || !m->ep_out) {
            continue;
        }
        nmscs++;
        if (m->proto == PROTO_BOT &&
            (usb_control(dev, 0xA1u, 0xFEu, 0, m->iface, &maxlun, 1, &got) != USB_OK || got != 1u ||
             maxlun >= MAX_LUNS)) {
            maxlun = 0; /* GET MAX LUN may stall: one LUN */
            usb_clear_halt(dev, 0);
        }
        for (lun = 0; lun <= maxlun && ntargets < MAX_TARGETS; lun++) {
            struct target *t = &targets[ntargets];
            memset(t, 0, sizeof(*t));
            t->m = m;
            t->lun = lun;
            if (oa_atapi_init(&t->pkt, packet, t, 0) != OA_HOST_OK) {
                continue;
            }
            /* SCSI devices take 6-byte commands as they are */
            t->pkt.translate = (oa_u8)(m->subclass == 2u || m->subclass == 4u || m->subclass == 5u);
            ntargets++;
        }
        return; /* one storage interface per device */
    }
}

int eusbaspi_register_storage(void)
{
    unsigned i;

    for (i = 0; i < usb_device_count(); i++) {
        probe_device(usb_device(i));
    }
    if (ntargets == 0u) {
        return 0;
    }
    hba.name = "USB";
    hba.ops = &msc_ops;
    hba.priv = 0;
    hba.max_target = (oa_u8)(ntargets - 1u);
    hba.max_lun = 0;
    oa_hba_register(&hba);
    return (int)ntargets;
}
