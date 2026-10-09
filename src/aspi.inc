; aspi.inc - ASPI for DOS SCSI Request Block (SRB) layout and constants
;
; Layout as defined by the Adaptec "ASPI for DOS" specification.  All
; offsets are relative to the start of the SRB.

; --- common header -----------------------------------------------------------
SRB_Cmd                 equ     00h     ; ASPI command code
SRB_Status              equ     01h     ; ASPI command status
SRB_HaId                equ     02h     ; host adapter number
SRB_Flags               equ     03h     ; request flags
SRB_Hdr_Rsvd            equ     04h     ; dword, reserved

; --- SC_HA_INQUIRY (0) -------------------------------------------------------
HA_Count                equ     08h     ; number of host adapters
HA_SCSI_ID              equ     09h     ; SCSI ID of the host adapter
HA_ManagerId            equ     0Ah     ; 16 bytes, ASPI manager id string
HA_Identifier           equ     1Ah     ; 16 bytes, host adapter id string
HA_Unique               equ     2Ah     ; 16 bytes, adapter unique parameters
HA_ExtFlags             equ     3Ah     ; word, only with the extended inquiry
                                        ; (SRB_Hdr_Rsvd = 55h AAh n 00h)
HA_EXT_RESIDUAL         equ     0002h   ; residual byte count supported

; --- SC_GET_DEV_TYPE (1) -----------------------------------------------------
SRB_Target              equ     08h
SRB_Lun                 equ     09h
SRB_DeviceType          equ     0Ah

; --- SC_EXEC_SCSI_CMD (2) ----------------------------------------------------
SRB_BufLen              equ     0Ah     ; dword, data allocation length
SRB_SenseLen            equ     0Eh     ; byte, sense allocation length
SRB_BufPointer          equ     0Fh     ; dword, far pointer to data buffer
SRB_LinkPointer         equ     13h     ; dword, far pointer to linked SRB
SRB_CDBLen              equ     17h     ; byte, CDB length
SRB_HaStat              equ     18h     ; byte, host adapter status
SRB_TargStat            equ     19h     ; byte, target status
SRB_PostProc            equ     1Ah     ; dword, far pointer to post routine
SRB_Workspace           equ     1Eh     ; 34 bytes, ASPI workspace
SRB_CDB                 equ     40h     ; CDB, followed by the sense area

; --- SC_ABORT_SRB (3) --------------------------------------------------------
SRB_ToAbort             equ     08h     ; dword, far pointer to SRB to abort

; --- SC_RESET_DEV (4): Target/Lun/HaStat/TargStat/PostProc as above ----------

; --- SC_GET_DISK_INFO (6) ----------------------------------------------------
SRB_DriveFlags          equ     0Ah
SRB_Int13HDriveInfo     equ     0Bh
SRB_Heads               equ     0Ch
SRB_Sectors             equ     0Dh

; --- command codes -----------------------------------------------------------
SC_HA_INQUIRY           equ     00h
SC_GET_DEV_TYPE         equ     01h
SC_EXEC_SCSI_CMD        equ     02h
SC_ABORT_SRB            equ     03h
SC_RESET_DEV            equ     04h
SC_SET_HA_PARMS         equ     05h
SC_GET_DISK_INFO        equ     06h

; --- SRB status --------------------------------------------------------------
SS_PENDING              equ     00h
SS_COMP                 equ     01h
SS_ABORTED              equ     02h
SS_ABORT_FAIL           equ     03h
SS_ERR                  equ     04h
SS_INVALID_CMD          equ     80h
SS_INVALID_HA           equ     81h
SS_NO_DEVICE            equ     82h

; --- SRB flags ---------------------------------------------------------------
SRB_POSTING             equ     01h
SRB_LINKING             equ     02h
SRB_RESIDUAL            equ     04h     ; return residual count in SRB_BufLen
SRB_DIR_MASK            equ     18h
SRB_DIR_AUTO            equ     00h     ; direction determined by the CDB
SRB_DIR_IN              equ     08h     ; target to host
SRB_DIR_OUT             equ     10h     ; host to target
SRB_DIR_NONE            equ     18h     ; no data transfer

; --- host adapter status -----------------------------------------------------
HASTAT_OK               equ     00h
HASTAT_SEL_TO           equ     11h     ; selection timeout
HASTAT_DO_DU            equ     12h     ; data overrun / underrun
HASTAT_BUS_FREE         equ     13h     ; unexpected bus free
HASTAT_PHASE_ERR        equ     14h     ; target bus phase sequence failure

; --- target status -----------------------------------------------------------
STATUS_GOOD             equ     00h
STATUS_CHKCOND          equ     02h
