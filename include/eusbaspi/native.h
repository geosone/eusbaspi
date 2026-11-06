/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EUSBASPI native: USB mass storage for OpenASPI's native layer, on the
 * eusbcore USB stack. The embedding system adds its controllers and
 * enumerates (usb_add(), usb_enumerate() in eusbcore/usb.h), then registers
 * the storage adapter. */

#ifndef EUSBASPI_NATIVE_H
#define EUSBASPI_NATIVE_H

#include "openaspi/types.h"

/* After usb_enumerate(): register one ASPI adapter "USB" with a target for
 * every LUN of every mass storage device (Bulk-Only, CBI, CB; SCSI, ATAPI,
 * UFI, SFF-8070i command sets). Returns the number of targets. */
int eusbaspi_register_storage(void);

#endif
