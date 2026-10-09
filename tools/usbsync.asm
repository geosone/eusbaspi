;=============================================================================
; USBSYNC.COM - write all cached data to the USB drives, so that they can be
; unplugged (DOS, and DOS boxes of Windows 3.x)
;
; Usage: USBSYNC [/E] [/A] [/Q]
;   /E  also stop the drives (STOP UNIT, eject): unplug them afterwards
;   /A  all ASPI host adapters, not only those of EUSBASPI
;   /Q  quiet: messages only on errors
;
; 1. DOS buffers: INT 21h AH=0Dh (disk reset; also the 32-bit file access
;    cache of Windows for Workgroups 3.11 in a DOS box)
; 2. SMARTDRV and compatible caches: INT 2Fh AX=4A10h BX=0001h (commit)
; 3. the drives' own write caches: SYNCHRONIZE CACHE(10) through ASPI
; Exit code: 0 = done, 1 = no ASPI manager, 2 = a drive reported an error
;
; Copyright (C) 2026 GeosOne.  GNU General Public License version 3.
;=============================================================================
        cpu     386
        bits    16
        org     100h

%include "aspi.inc"

SRB_SIZE        equ     SRB_CDB + 16 + 32

start:
        cld
        call    parse_args
        mov     si, msg_title
        call    info

        ; 1. DOS buffers
        mov     ah, 0Dh
        int     21h

        ; 2. SMARTDRV write-behind cache
        mov     ax, 4A10h
        xor     bx, bx
        int     2Fh
        cmp     ax, 0BABEh
        jne     .nocache
        mov     ax, 4A10h
        mov     bx, 1                   ; commit cache
        int     2Fh
        mov     si, msg_smartdrv
        call    info
.nocache:

        ; 3. the drives
        mov     ax, 3D00h               ; open SCSIMGR$
        mov     dx, dev_name
        int     21h
        jnc     .opened
        mov     si, msg_no_mgr
        call    print
        mov     byte [exitcode], 1
        jmp     done
.opened:
        mov     bx, ax
        mov     ax, 4402h               ; IOCTL read: entry point
        mov     dx, aspi_entry
        mov     cx, 4
        int     21h
        mov     ah, 3Eh
        int     21h

        call    clear_srb
        mov     byte [srb+SRB_Cmd], SC_HA_INQUIRY
        call    call_aspi
        mov     al, [srb+HA_Count]
        mov     [ha_count], al

        mov     byte [hanum], 0
.adapter:
        mov     al, [hanum]
        cmp     al, [ha_count]
        jae     .drives_done
        call    clear_srb
        mov     byte [srb+SRB_Cmd], SC_HA_INQUIRY
        mov     [srb+SRB_HaId], al
        call    call_aspi
        mov     al, [srb+HA_SCSI_ID]
        mov     [ha_id], al
        cmp     byte [opt_all], 0
        jne     .use
        mov     si, srb+HA_ManagerId    ; only adapters of EUSBASPI
        mov     di, mgr_name
        mov     cx, 8
        repe    cmpsb
        jne     .next_ha
.use:   mov     byte [target], 0
.target:
        mov     al, [target]
        cmp     al, [ha_id]
        je      .skip
        call    sync_target
.skip:  inc     byte [target]
        cmp     byte [target], 8
        jb      .target
.next_ha:
        inc     byte [hanum]
        jmp     .adapter

.drives_done:
        mov     ah, 0Dh                 ; once more, after the drives
        int     21h
        cmp     byte [n_drives], 0
        jne     .some
        mov     si, msg_none
        call    info
        jmp     done
.some:  cmp     byte [exitcode], 0
        jne     done
        mov     si, msg_done
        cmp     byte [opt_eject], 0
        je      .msg
        mov     si, msg_done_e
.msg:   call    info

done:   mov     ah, 4Ch
        mov     al, [exitcode]
        int     21h

;-----------------------------------------------------------------------------
; one target: disks and optical memory get SYNCHRONIZE CACHE (and STOP)
;-----------------------------------------------------------------------------
sync_target:
        call    clear_srb
        mov     byte [srb+SRB_Cmd], SC_GET_DEV_TYPE
        mov     al, [hanum]
        mov     [srb+SRB_HaId], al
        mov     al, [target]
        mov     [srb+SRB_Target], al
        call    call_aspi
        cmp     byte [srb+SRB_Status], SS_COMP
        jne     .ret
        mov     al, [srb+SRB_DeviceType]
        and     al, 1Fh
        cmp     al, 00h                 ; direct access (sticks, disks, floppy)
        je      .go
        cmp     al, 07h                 ; optical memory
        je      .go
        cmp     al, 0Eh                 ; RBC
        je      .go
.ret:   ret
.go:    inc     byte [n_drives]
        ; name from INQUIRY
        mov     di, cdb_inquiry
        mov     ecx, 36
        mov     bx, inq_buf
        mov     al, SRB_DIR_IN
        call    scsi
        mov     si, msg_drive
        call    info
        movzx   ax, byte [hanum]
        call    info_dec
        mov     si, msg_drive_t
        call    info
        movzx   ax, byte [target]
        call    info_dec
        mov     si, msg_colon
        call    info
        mov     si, inq_buf+8           ; vendor + product
        mov     cx, 24
        call    info_n

        mov     di, cdb_sync
        xor     ecx, ecx
        xor     bx, bx
        mov     al, SRB_DIR_NONE
        call    scsi
        call    result
        cmp     byte [opt_eject], 0
        je      .crlf
        mov     di, cdb_allow           ; allow medium removal
        xor     ecx, ecx
        xor     bx, bx
        mov     al, SRB_DIR_NONE
        call    scsi
        mov     di, cdb_stop            ; stop, eject
        xor     ecx, ecx
        xor     bx, bx
        mov     al, SRB_DIR_NONE
        call    scsi
        mov     si, msg_stopped
        call    info
.crlf:  mov     si, msg_crlf
        call    info
        ret

; result of SYNCHRONIZE CACHE: "not supported" (ILLEGAL REQUEST) is fine,
; the drive then has no write cache to flush
result:
        cmp     al, SS_COMP
        jne     .err
        mov     si, msg_synced
        jmp     info
.err:   cmp     byte [srb+SRB_TargStat], STATUS_CHKCOND
        jne     .fail
        mov     al, [srb+SRB_CDB+10+2]  ; sense key (CDB length 10)
        and     al, 0Fh
        cmp     al, 05h
        jne     .fail
        mov     si, msg_nocache
        jmp     info
.fail:  mov     byte [exitcode], 2
        mov     si, msg_failed
        jmp     print                   ; errors also with /Q

;-----------------------------------------------------------------------------
; SCSI command on [hanum]/[target]: DI = CDB (first byte = length),
; ECX = data length, BX = buffer, AL = direction  ->  AL = SRB status
;-----------------------------------------------------------------------------
scsi:   push    ax
        call    clear_srb
        pop     ax
        mov     byte [srb+SRB_Cmd], SC_EXEC_SCSI_CMD
        mov     [srb+SRB_Flags], al
        mov     al, [hanum]
        mov     [srb+SRB_HaId], al
        mov     al, [target]
        mov     [srb+SRB_Target], al
        mov     [srb+SRB_BufLen], ecx
        mov     [srb+SRB_BufPointer], bx
        mov     [srb+SRB_BufPointer+2], cs
        mov     byte [srb+SRB_SenseLen], 18
        movzx   cx, byte [di]
        mov     [srb+SRB_CDBLen], cl
        lea     si, [di+1]
        mov     di, srb+SRB_CDB
        rep     movsb
        call    call_aspi
        mov     al, [srb+SRB_Status]
        ret

call_aspi:
        push    ds
        push    srb
        call    far [aspi_entry]
        add     sp, 4
.wait:  cmp     byte [srb+SRB_Status], SS_PENDING
        je      .wait
        ret

clear_srb:
        push    di
        push    cx
        mov     di, srb
        mov     cx, SRB_SIZE
        xor     al, al
        rep     stosb
        pop     cx
        pop     di
        ret

;-----------------------------------------------------------------------------
parse_args:
        mov     si, 81h
.next:  lodsb
        cmp     al, 13
        je      .end
        cmp     al, '/'
        jne     .next
        lodsb
        and     al, 0DFh
        cmp     al, 'E'
        jne     .a
        mov     byte [opt_eject], 1
.a:     cmp     al, 'A'
        jne     .q
        mov     byte [opt_all], 1
.q:     cmp     al, 'Q'
        jne     .h
        mov     byte [opt_quiet], 1
.h:     cmp     al, '?' & 0DFh
        jne     .next
        mov     si, msg_usage
        call    print
        mov     ax, 4C00h
        int     21h
.end:   ret

; info*: output unless /Q
info:   cmp     byte [opt_quiet], 0
        jne     ret0
print:  lodsb
        or      al, al
        jz      ret0
        call    putc
        jmp     print
ret0:   ret

info_n: cmp     byte [opt_quiet], 0
        jne     ret0
.l:     lodsb
        call    putc
        loop    .l
        ret

info_dec:
        cmp     byte [opt_quiet], 0
        jne     ret0
        mov     cx, 10
        xor     dx, dx
        div     cx
        or      ax, ax
        jz      .digit
        push    dx
        call    info_dec
        pop     dx
.digit: mov     al, dl
        add     al, '0'
putc:   mov     dl, al
        mov     ah, 2
        int     21h
        ret

;-----------------------------------------------------------------------------
dev_name        db      'SCSIMGR$', 0
mgr_name        db      'EUSBASPI'
cdb_inquiry     db      6, 12h, 0, 0, 0, 36, 0
cdb_sync        db      10, 35h, 0, 0, 0, 0, 0, 0, 0, 0, 0
cdb_allow       db      6, 1Eh, 0, 0, 0, 0, 0
cdb_stop        db      6, 1Bh, 0, 0, 0, 02h, 0  ; START=0, LOEJ=1

msg_title       db      'USBSYNC - write cached data to the USB drives', 13, 10, 0
msg_usage       db      'USBSYNC [/E] [/A] [/Q]', 13, 10
                db      '  /E  also stop (eject) the drives', 13, 10
                db      '  /A  all ASPI host adapters, not only EUSBASPI', 13, 10
                db      '  /Q  quiet', 13, 10, 0
msg_smartdrv    db      'SMARTDRV cache written', 13, 10, 0
msg_no_mgr      db      'USBSYNC: no ASPI manager (SCSIMGR$) loaded', 13, 10, 0
msg_drive       db      '  HA ', 0
msg_drive_t     db      ' ID ', 0
msg_colon       db      ': ', 0
msg_synced      db      '  synchronized', 0
msg_nocache     db      '  no write cache', 0
msg_failed      db      '  ERROR: the drive did not confirm the data', 13, 10, 0
msg_stopped     db      ', stopped', 0
msg_crlf        db      13, 10, 0
msg_none        db      'No USB drives found (DOS buffers written).', 13, 10, 0
msg_done        db      'All data written: the USB drives can be unplugged now.', 13, 10, 0
msg_done_e      db      'All data written and the drives stopped: unplug them now.', 13, 10, 0

opt_eject       db      0
opt_all         db      0
opt_quiet       db      0
exitcode        db      0
n_drives        db      0
ha_count        db      0
hanum           db      0
ha_id           db      0
target          db      0
aspi_entry      dd      0
inq_buf         times 36 db ' '
srb             times SRB_SIZE db 0
