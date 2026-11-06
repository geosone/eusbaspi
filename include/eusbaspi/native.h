/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EUSBASPI native: USB host controllers and mass storage for OpenASPI's
 * native layer. The embedding system adds its controllers, enumerates
 * (usb_enumerate() in usb.h) and registers the storage adapter. */

#ifndef EUSBASPI_NATIVE_H
#define EUSBASPI_NATIVE_H

#include "openaspi/types.h"

/* PCI class/subclass of USB controllers and the programming interfaces. */
#define EUSBASPI_PCI_CLASS_USB 0x0C03u
#define EUSBASPI_PCI_UHCI 0x00u
#define EUSBASPI_PCI_OHCI 0x10u
#define EUSBASPI_PCI_EHCI 0x20u
#define EUSBASPI_PCI_XHCI 0x30u

/* Take over the controller at PCI address `pci` and register it with the
 * USB core; 0 on success, -1 if it is not usable. */
int eusbaspi_add_xhci(oa_u32 pci);
int eusbaspi_add_ehci(oa_u32 pci);
int eusbaspi_add_ohci(oa_u32 pci);
int eusbaspi_add_uhci(oa_u32 pci);

/* Any USB controller by its programming interface. EHCI controllers are to
 * be added before their companion UHCI/OHCI controllers. */
int eusbaspi_add(oa_u32 pci);

/* After usb_enumerate(): register one ASPI adapter "USB" with a target for
 * every LUN of every mass storage device (Bulk-Only, CBI, CB; SCSI, ATAPI,
 * UFI, SFF-8070i command sets). Returns the number of targets. */
int eusbaspi_register_storage(void);

#endif
