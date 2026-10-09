;=============================================================================
; ASPITEST.COM - exercise a DOS ASPI manager (SCSIMGR$)
;
; Usage: ASPITEST [/E9] [/X]
;   /E9  mirror all output to port E9h (QEMU debug console)
;   /X   exit QEMU through isa-debug-exit (port F4h) when done
;
; For every target: INQUIRY, TEST UNIT READY, READ CAPACITY, a sector read
; with READ(10) and READ(6), MODE SENSE(6), direction "auto" and an odd
; buffer address.  Every check prints OK or FAIL.
;=============================================================================
        cpu     486
        bits    16
        org     100h

%include "aspi.inc"

SRB_SIZE        equ     SRB_CDB + 16 + 32

start:
        cld
        call    parse_args
        mov     si, msg_title
        call    print

        mov     ax, 3D00h               ; open SCSIMGR$
        mov     dx, dev_name
        int     21h
        jnc     .opened
        mov     si, msg_no_mgr
        call    print
        jmp     finish
.opened:
        mov     bx, ax
        mov     ax, 4402h               ; IOCTL read: entry point
        mov     dx, aspi_entry
        mov     cx, 4
        int     21h
        mov     ah, 3Eh
        int     21h
        mov     si, msg_entry
        call    print
        mov     ax, [aspi_entry+2]
        call    print_hex16
        mov     al, ':'
        call    putc
        mov     ax, [aspi_entry]
        call    print_hex16
        call    crlf

        ; host adapter inquiry
        call    clear_srb
        mov     byte [srb+SRB_Cmd], SC_HA_INQUIRY
        call    call_aspi
        mov     si, msg_ha
        call    print
        mov     al, [srb+SRB_Status]
        call    print_hex8
        mov     si, msg_ha_count
        call    print
        movzx   ax, byte [srb+HA_Count]
        call    print_dec
        mov     si, msg_ha_id
        call    print
        movzx   ax, byte [srb+HA_SCSI_ID]
        call    print_dec
        mov     si, msg_ha_mgr
        call    print
        mov     si, srb+HA_ManagerId
        mov     cx, 16
        call    print_n
        mov     si, msg_ha_ident
        call    print
        mov     si, srb+HA_Identifier
        mov     cx, 16
        call    print_n
        mov     al, "'"
        call    putc
        call    crlf
        mov     al, [srb+HA_Count]
        mov     [ha_count], al
        mov     al, [srb+SRB_Status]
        mov     si, msg_t_hainq
        call    check_status

        ; extended host adapter inquiry: 55 AA n 00 -> AA 55 n 00 + flags
        call    clear_srb
        mov     byte [srb+SRB_Cmd], SC_HA_INQUIRY
        mov     dword [srb+SRB_Hdr_Rsvd], 0002AA55h
        call    call_aspi
        mov     si, msg_ext
        call    print
        mov     ax, [srb+SRB_Hdr_Rsvd]
        call    print_hex16
        mov     si, msg_extflags
        call    print
        mov     ax, [srb+HA_ExtFlags]
        call    print_hex16
        call    crlf
        cmp     word [srb+SRB_Hdr_Rsvd], 55AAh
        mov     si, msg_t_ext
        call    report_zf
        test    word [srb+HA_ExtFlags], HA_EXT_RESIDUAL
        mov     si, msg_t_extres
        call    report_nz

        ; invalid adapter number must be rejected
        call    clear_srb
        mov     byte [srb+SRB_Cmd], SC_GET_DEV_TYPE
        mov     al, [ha_count]          ; first adapter number not in use
        mov     [srb+SRB_HaId], al
        call    call_aspi
        cmp     byte [srb+SRB_Status], SS_INVALID_HA
        mov     si, msg_t_badha
        call    report_zf

        mov     byte [hanum], 0
.adapter:
        mov     al, [hanum]
        cmp     al, [ha_count]
        jae     finish
        call    clear_srb               ; inquiry of this adapter
        mov     byte [srb+SRB_Cmd], SC_HA_INQUIRY
        mov     al, [hanum]
        mov     [srb+SRB_HaId], al
        call    call_aspi
        mov     al, [srb+HA_SCSI_ID]
        mov     [ha_id], al
        mov     si, msg_adapter
        call    print
        movzx   ax, byte [hanum]
        call    print_dec
        mov     si, msg_ha_mgr
        call    print
        mov     si, srb+HA_ManagerId
        mov     cx, 16
        call    print_n
        mov     si, msg_ha_ident
        call    print
        mov     si, srb+HA_Identifier
        mov     cx, 16
        call    print_n
        mov     si, msg_ha_id2
        call    print
        movzx   ax, byte [ha_id]
        call    print_dec
        call    crlf
        mov     byte [target], 0
.target:
        mov     al, [target]
        cmp     al, [ha_id]
        je      .skip
        call    test_target
.skip:  inc     byte [target]
        cmp     byte [target], 8
        jb      .target
        inc     byte [hanum]
        jmp     .adapter

finish:
        mov     si, msg_summary
        call    print
        mov     ax, [n_ok]
        call    print_dec
        mov     si, msg_sum_ok
        call    print
        mov     ax, [n_fail]
        call    print_dec
        mov     si, msg_sum_fail
        call    print
        cmp     byte [opt_exit], 0
        je      .dos
        mov     al, 0
        out     0F4h, al
.dos:   mov     ax, 4C00h
        int     21h

;-----------------------------------------------------------------------------
test_target:
        call    clear_srb
        mov     byte [srb+SRB_Cmd], SC_GET_DEV_TYPE
        mov     al, [hanum]
        mov     [srb+SRB_HaId], al
        mov     al, [target]
        mov     [srb+SRB_Target], al
        call    call_aspi
        cmp     byte [srb+SRB_Status], SS_COMP
        je      .present
        ret
.present:
        mov     al, [srb+SRB_DeviceType]
        mov     [devtype], al
        mov     si, msg_target
        call    print
        movzx   ax, byte [target]
        call    print_dec
        mov     si, msg_type
        call    print
        mov     al, [devtype]
        call    print_hex8
        call    crlf

        ; INQUIRY
        mov     di, cdb_inquiry
        mov     ecx, 36
        mov     bx, buf
        mov     al, SRB_DIR_IN
        call    scsi
        mov     si, msg_t_inq
        call    check_status
        jc      .no_inq
        mov     si, msg_ident
        call    print
        mov     si, buf+8
        mov     cx, 28
        call    print_n
        mov     al, "'"
        call    putc
        call    crlf
        mov     si, buf+8               ; keep for later comparisons
        mov     di, inq_save
        mov     cx, 28
        rep     movsb
.no_inq:

        ; TEST UNIT READY (up to 10 times, unit attention expected)
        mov     word [retries], 10
.tur:   mov     di, cdb_tur
        xor     ecx, ecx
        xor     bx, bx
        mov     al, SRB_DIR_NONE
        call    scsi
        cmp     al, SS_COMP
        je      .ready
        call    print_sense
        dec     word [retries]
        jz      .notready
        mov     cx, 9                   ; wait ~0.5 s
.wt:    call    wait_tick
        loop    .wt
        jmp     .tur
.notready:
        mov     si, msg_t_tur
        mov     al, SS_ERR
        call    check_status
        ret
.ready: mov     si, msg_t_tur
        call    check_status

        ; READ CAPACITY(10)
        mov     di, cdb_readcap
        mov     ecx, 8
        mov     bx, buf
        mov     al, SRB_DIR_IN
        call    scsi
        mov     si, msg_t_cap
        call    check_status
        jc      .modesense
        mov     eax, [buf]
        bswap   eax
        inc     eax
        mov     [blocks], eax
        mov     eax, [buf+4]
        bswap   eax
        mov     [blksize], eax
        mov     si, msg_blocks
        call    print
        mov     eax, [blocks]
        call    print_hex32
        mov     si, msg_blksize
        call    print
        mov     ax, [blksize]
        call    print_dec
        call    crlf

        ; READ(10) of one block: LBA 16 for 2048 byte media, else LBA 0
        xor     eax, eax
        cmp     dword [blksize], 2048
        jne     .lba
        mov     al, 16
.lba:   mov     [rd_lba], eax
        cmp     dword [blksize], 2048
        ja      .modesense
        mov     di, cdb_read10
        bswap   eax
        mov     [di+3], eax             ; LBA = CDB bytes 2..5
        mov     ecx, [blksize]
        mov     bx, buf
        mov     al, SRB_DIR_IN
        call    scsi
        mov     si, msg_t_rd10
        call    check_status
        jc      .modesense
        mov     si, msg_data
        call    print
        mov     si, buf
        mov     cx, 16
        call    print_hexdump
        call    crlf

        ; READ(6) of the same block must give the same data
        mov     di, cdb_read6
        mov     eax, [rd_lba]
        mov     [di+4], al              ; LBA = CDB bytes 1..3
        mov     [di+3], ah
        mov     ecx, [blksize]
        mov     bx, buf2
        mov     al, SRB_DIR_IN
        call    scsi
        mov     si, msg_t_rd6
        call    check_status
        jc      .modesense
        mov     si, buf
        mov     di, buf2
        mov     cx, [blksize]
        repe    cmpsb
        mov     si, msg_t_rd6cmp
        call    report_zf

.modesense:
        ; MODE SENSE(6), all pages; devices that do not know page 3Fh
        ; (ILLEGAL REQUEST / invalid field) are asked for page 01h
        mov     byte [cdb_msense6+3], 3Fh
.msretry:
        mov     di, cdb_msense6
        mov     ecx, 255
        mov     bx, buf
        mov     dword [buf], 0FFFFFFFFh
        mov     al, SRB_DIR_IN
        call    scsi
        cmp     byte [srb+SRB_Status], SS_COMP
        je      .msdone
        cmp     byte [cdb_msense6+3], 3Fh
        jne     .msdone
        mov     al, [srb+SRB_CDB+6+2]
        and     al, 0Fh
        cmp     al, 5
        jne     .msdone
        cmp     byte [srb+SRB_CDB+6+12], 24h
        jne     .msdone
        mov     byte [cdb_msense6+3], 01h
        jmp     .msretry
.msdone:
        mov     si, msg_t_msense
        call    check_status
        jc      .auto
        mov     si, msg_mshdr
        call    print
        mov     si, buf
        mov     cx, 8
        call    print_hexdump
        call    crlf
        ; header sanity: length must not exceed the allocation length
        cmp     byte [buf], 0FFh
        mov     si, msg_t_mshdr
        call    report_nz

.auto:  ; direction derived by the manager
        mov     di, cdb_inquiry
        mov     ecx, 36
        mov     bx, buf
        mov     al, SRB_DIR_AUTO
        call    scsi
        mov     si, msg_t_auto
        call    check_status
        jc      .odd
        mov     si, buf+8
        mov     di, inq_save
        mov     cx, 28
        repe    cmpsb
        mov     si, msg_t_autocmp
        call    report_zf

.odd:   ; odd buffer address goes through the bounce buffer
        mov     di, cdb_inquiry
        mov     ecx, 36
        mov     bx, buf+1
        mov     al, SRB_DIR_IN
        call    scsi
        mov     si, msg_t_odd
        call    check_status
        jc      .sense
        mov     si, buf+9
        mov     di, inq_save
        mov     cx, 28
        repe    cmpsb
        mov     si, msg_t_oddcmp
        call    report_zf

.sense: call    test_extras
        ; invalid opcode must give CHECK CONDITION with ILLEGAL REQUEST
        mov     di, cdb_bogus
        xor     ecx, ecx
        xor     bx, bx
        mov     al, SRB_DIR_NONE
        call    scsi
        cmp     byte [srb+SRB_TargStat], 2
        jne     .sense_bad
        mov     al, [srb+SRB_CDB+6+2]
        and     al, 0Fh
        cmp     al, 5
.sense_bad:
        mov     si, msg_t_sense
        call    report_zf
        call    print_sense
        ret

;-----------------------------------------------------------------------------
; residual count, overrun detection, linking
test_extras:
        ; residual: INQUIRY into a 255 byte buffer, devices return less
        mov     di, cdb_inq255
        mov     ecx, 255
        mov     bx, buf
        mov     al, SRB_DIR_IN | SRB_RESIDUAL
        call    scsi
        mov     si, msg_t_resid
        call    check_status
        jc      .over
        mov     si, msg_resid
        call    print
        mov     ax, [srb+SRB_BufLen]
        call    print_dec
        call    crlf
        cmp     dword [srb+SRB_BufLen], 255     ; at most what was asked for
        mov     si, msg_t_residv
        jbe     .resid_ok
        call    report_fail
        jmp     .over
.resid_ok:
        call    report_ok

.over:  ; overrun: the CDB asks for 36 bytes, the buffer holds 8
        mov     di, cdb_inquiry
        mov     ecx, 8
        mov     bx, buf
        mov     al, SRB_DIR_IN
        call    scsi
        cmp     byte [srb+SRB_HaStat], HASTAT_DO_DU
        mov     si, msg_t_over
        je      .over_ok
        cmp     byte [srb+SRB_Status], SS_COMP
        jne     .over_fail
        mov     si, msg_i_over          ; data cut silently: depends on the
        call    print                   ; HBA flagging the overflow (AHCI
        jmp     .link                   ; PxIS.OFS, QEMU does not)
.over_fail:
        call    report_fail
        jmp     .link
.over_ok:
        call    report_ok

.link:
        ; linking is not supported by ATAPI and must be rejected
        mov     di, cdb_tur
        xor     ecx, ecx
        xor     bx, bx
        mov     al, SRB_DIR_NONE | SRB_LINKING
        call    scsi
        cmp     byte [srb+SRB_Status], SS_INVALID_CMD
        mov     si, msg_t_link
        call    report_zf
        ret

;-----------------------------------------------------------------------------
; Execute a SCSI command on [target]
;   DI = CDB (first byte = length), ECX = data length, BX = buffer,
;   AL = direction flags  ->  AL = SRB status
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
        push    di
        mov     di, srb+SRB_CDB
        rep     movsb
        pop     di
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

; print status details and sense data of the last SRB
print_sense:
        mov     si, msg_status
        call    print
        mov     al, [srb+SRB_Status]
        call    print_hex8
        mov     si, msg_hastat
        call    print
        mov     al, [srb+SRB_HaStat]
        call    print_hex8
        mov     si, msg_tgtstat
        call    print
        mov     al, [srb+SRB_TargStat]
        call    print_hex8
        movzx   bx, byte [srb+SRB_CDBLen]
        add     bx, srb+SRB_CDB
        mov     si, msg_sense
        call    print
        mov     al, [bx+2]
        and     al, 0Fh
        call    print_hex8
        mov     al, '/'
        call    putc
        mov     al, [bx+12]
        call    print_hex8
        mov     al, '/'
        call    putc
        mov     al, [bx+13]
        call    print_hex8
        jmp     crlf

; AL = SRB status, SI = test name.  CF set if not SS_COMP.
check_status:
        cmp     al, SS_COMP
        jne     .fail
        call    report_ok
        clc
        ret
.fail:  call    report_fail
        call    print_sense
        stc
        ret

report_zf:
        jnz     report_fail
        jmp     report_ok
report_nz:
        jz      report_fail
report_ok:
        inc     word [n_ok]
        push    si
        mov     si, msg_ok
        call    print
        pop     si
        call    print
        jmp     crlf
report_fail:
        inc     word [n_fail]
        push    si
        mov     si, msg_fail
        call    print
        pop     si
        call    print
        jmp     crlf

wait_tick:
        push    ax
        push    es
        push    word 40h
        pop     es
        mov     ax, [es:6Ch]
.w:     sti
        cmp     ax, [es:6Ch]
        je      .w
        pop     es
        pop     ax
        ret

;-----------------------------------------------------------------------------
parse_args:
        mov     si, 81h
.l:     lodsb
        cmp     al, 0Dh
        je      .done
        cmp     al, '/'
        jne     .l
        lodsb
        and     al, 0DFh
        cmp     al, 'X'
        jne     .e9
        mov     byte [opt_exit], 1
        jmp     .l
.e9:    cmp     al, 'E'
        jne     .l
        mov     byte [opt_e9], 1
        jmp     .l
.done:  ret

putc:   push    ax
        push    dx
        cmp     byte [opt_e9], 0
        je      .dos
        out     0E9h, al
.dos:   mov     dl, al
        mov     ah, 2
        int     21h
        pop     dx
        pop     ax
        ret

crlf:   push    ax
        mov     al, 13
        call    putc
        mov     al, 10
        call    putc
        pop     ax
        ret

print:  push    ax
        push    si
.l:     lodsb
        or      al, al
        jz      .e
        call    putc
        jmp     .l
.e:     pop     si
        pop     ax
        ret

print_n:                                ; SI, CX characters
        push    ax
.l:     lodsb
        cmp     al, ' '
        jae     .p
        mov     al, '.'
.p:     call    putc
        loop    .l
        pop     ax
        ret

print_hexdump:                          ; SI, CX bytes
        lodsb
        call    print_hex8
        mov     al, ' '
        call    putc
        loop    print_hexdump
        ret

print_hex32:
        push    eax
        shr     eax, 16
        call    print_hex16
        pop     eax
print_hex16:
        push    ax
        mov     al, ah
        call    print_hex8
        pop     ax
print_hex8:
        push    ax
        shr     al, 4
        call    .nib
        pop     ax
        push    ax
        and     al, 0Fh
        call    .nib
        pop     ax
        ret
.nib:   add     al, '0'
        cmp     al, '9'
        jbe     .p
        add     al, 7
.p:     jmp     putc

print_dec:
        push    ax
        push    bx
        push    cx
        push    dx
        mov     bx, 10
        xor     cx, cx
.d:     xor     dx, dx
        div     bx
        push    dx
        inc     cx
        or      ax, ax
        jnz     .d
.o:     pop     ax
        add     al, '0'
        call    putc
        loop    .o
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

;-----------------------------------------------------------------------------
dev_name        db      'SCSIMGR$', 0
msg_title       db      'ASPITEST - ASPI manager test', 13, 10, 0
msg_no_mgr      db      'FAIL: SCSIMGR$ not found', 13, 10, 0
msg_entry       db      'ASPI entry point ', 0
msg_ha          db      'HA inquiry: status ', 0
msg_ha_count    db      ', adapters ', 0
msg_ha_id       db      ', HA ID ', 0
msg_ha_mgr      db      ", manager '", 0
msg_ha_ident    db      "', adapter '", 0
msg_target      db      '--- target ', 0
msg_adapter     db      '=== host adapter ', 0
msg_ha_id2      db      "', ID ", 0
msg_type        db      ', device type ', 0
msg_ident       db      "    '", 0
msg_blocks      db      '    blocks ', 0
msg_blksize     db      'h, block size ', 0
msg_data        db      '    data: ', 0
msg_mshdr       db      '    mode header: ', 0
msg_status      db      '    status ', 0
msg_hastat      db      ' ha ', 0
msg_tgtstat     db      ' target ', 0
msg_sense       db      ' sense ', 0
msg_ok          db      'OK   ', 0
msg_fail        db      'FAIL ', 0
msg_summary     db      'Summary: ', 0
msg_sum_ok      db      ' OK, ', 0
msg_sum_fail    db      ' FAIL', 13, 10, 0
msg_t_hainq     db      'host adapter inquiry', 0
msg_ext         db      'extended inquiry: signature ', 0
msg_extflags    db      ', flags ', 0
msg_t_ext       db      'extended inquiry signature AA 55', 0
msg_t_extres    db      'extended inquiry: residual supported', 0
msg_resid       db      '    residual bytes ', 0
msg_t_resid     db      'INQUIRY with residual count', 0
msg_t_residv    db      'residual count plausible', 0
msg_t_over      db      'overrun reported as HaStat 12h', 0
msg_t_link      db      'linking rejected', 0
msg_i_over      db      'INFO overrun not flagged by the host adapter', 13, 10, 0
msg_t_badha     db      'invalid adapter rejected', 0
msg_t_inq       db      'INQUIRY', 0
msg_t_tur       db      'TEST UNIT READY', 0
msg_t_cap       db      'READ CAPACITY', 0
msg_t_rd10      db      'READ(10)', 0
msg_t_rd6       db      'READ(6)', 0
msg_t_rd6cmp    db      'READ(6) data equals READ(10)', 0
msg_t_msense    db      'MODE SENSE(6)', 0
msg_t_mshdr     db      'MODE SENSE(6) header converted', 0
msg_t_auto      db      'INQUIRY with direction auto', 0
msg_t_autocmp   db      'direction auto data', 0
msg_t_odd       db      'INQUIRY into odd buffer', 0
msg_t_oddcmp    db      'odd buffer data', 0
msg_t_sense     db      'invalid opcode gives ILLEGAL REQUEST sense', 0

; CDBs: length byte followed by the CDB
cdb_inquiry     db      6, 12h, 0, 0, 0, 36, 0
cdb_inq255      db      6, 12h, 0, 0, 0, 255, 0
cdb_tur         db      6, 00h, 0, 0, 0, 0, 0
cdb_readcap     db      10, 25h, 0, 0, 0, 0, 0, 0, 0, 0, 0
cdb_read10      db      10, 28h, 0, 0, 0, 0, 0, 0, 0, 1, 0
cdb_read6       db      6, 08h, 0, 0, 0, 1, 0
cdb_msense6     db      6, 1Ah, 0, 3Fh, 0, 255, 0   ; all pages
cdb_bogus       db      6, 0FFh, 0, 0, 0, 0, 0

opt_e9          db      0
opt_exit        db      0
target          db      0
hanum           db      0
ha_count        db      0
ha_id           db      7
devtype         db      0
retries         dw      0
n_ok            dw      0
n_fail          dw      0
blocks          dd      0
blksize         dd      0
rd_lba          dd      0
aspi_entry      dd      0
inq_save        times 28 db 0

                align   16
srb             times SRB_SIZE db 0
                align   16
buf             times 4096 db 0
buf2            times 4096 db 0
