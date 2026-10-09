;=============================================================================
; defs.inc - PCI, ATA and SCSI constants of EUSBASPI
;=============================================================================

; --- PCI ---------------------------------------------------------------------
PCI_CMD                 equ     04h
PCI_CMD_IO              equ     0001h
PCI_CMD_MEM             equ     0002h
PCI_CMD_MASTER          equ     0004h
PCI_BAR0                equ     10h
; --- SCSI opcodes ------------------------------------------------------------
SCSI_REZERO_UNIT        equ     01h
SCSI_REQUEST_SENSE      equ     03h
SCSI_READ6              equ     08h
SCSI_WRITE6             equ     0Ah
SCSI_SEEK6              equ     0Bh
SCSI_INQUIRY            equ     12h
SCSI_MODE_SELECT6       equ     15h
SCSI_MODE_SENSE6        equ     1Ah
SCSI_SEEK10             equ     2Bh
SCSI_MODE_SELECT10      equ     55h
SCSI_MODE_SENSE10       equ     5Ah
SENSE_ILLEGAL_REQUEST   equ     05h
