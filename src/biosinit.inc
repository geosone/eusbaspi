;=============================================================================
; biosinit.inc - find the BIOS hard disks that are our USB devices (init)
;
; bd_scan_bios runs before the controllers are taken over: for each BIOS
; hard disk it keeps the geometry, the EDD size and a hash of sector 0.
; Disks EDD 3.0 names as ATA / SATA are skipped.  bd_match runs after the
; scan: a USB disk with 512 byte sectors is a BIOS disk when sector 0 has
; the same hash and the size fits (the EDD size, else the CHS geometry).
; A BIOS disk two devices fit is left alone.  Then INT 13h is hooked for
; the ones found (biosdisk.inc).
;=============================================================================

BC_MAX          equ     8

bc_count        db      0
bc_drive        times BC_MAX db 0
bc_cx           times BC_MAX dw 0
bc_dh           times BC_MAX db 0
bc_total        times BC_MAX dd 0       ; EDD sectors, 0: unknown
bc_hash         times BC_MAX dd 0
bc_cap          times BC_MAX dd 0       ; size of the device that fits
bc_dev          times BC_MAX db 0FFh    ; entry that fits, 0FEh: two do
bs_drive        db      0
bs_left         db      0
bm_cap          dd      0
bm_hash         dd      0
bm_len          dd      0
bm_try          db      0
bm_cdb          times 12 db 0
bd_romtmp       dd      0
bs_seg          dw      0
bs_off          dw      0
bs_vds          db      0

msg_bd1         db      '  BIOS drive ', 0
msg_bd2         db      'h is ID ', 0
msg_bd3         db      ': INT 13h taken over', 13, 10, 0
msg_bd3nd       db      ': INT 13h taken over (no DOS drive)', 13, 10, 0
msg_bdnobuf     db      '  loaded high: BIOS drives on USB stay the BIOS', 39, 's (load EUSBASPI low', 13, 10
                db      '  to take them over)', 13, 10, 0
msg_bdamb       db      'h: more than one device fits, left to the BIOS', 13, 10, 0

; SI -> EAX = hash of the 512 bytes there
bd_hash:
        push    cx
        push    si
        xor     eax, eax
        mov     cx, 128
.h:     rol     eax, 5
        xor     eax, [si]
        add     si, 4
        loop    .h
        pop     si
        pop     cx
        ret

; a buffer for the BIOS's reads: its USB code writes by bus master to the
; linear address, so the buffer must be mapped 1:1.  Ours, unless we are
; loaded high under a memory manager; then its VDS DMA buffer when that is
; in conventional memory.  -> bs_seg:bs_off, CF when there is none
bd_lowbuf:
        mov     byte [bs_vds], 0
        mov     ax, [bounce_ptr]
        mov     ecx, 512
        call    get_phys
        jc      .fail
        mov     edx, eax
        mov     ax, cs
        movzx   eax, ax
        shl     eax, 4
        movzx   ecx, word [bounce_ptr]
        add     eax, ecx
        cmp     eax, edx
        jne     .vds
        mov     [bs_seg], cs
        mov     ax, [bounce_ptr]
        mov     [bs_off], ax
        clc
        ret
.vds:   cmp     byte [vds_ok], 0
        je      .fail
        mov     dword [dds_size], 512
        mov     dword [dds_off], 0
        mov     word [dds_seg], 0
        mov     word [dds_bufid], 0
        push    es
        push    cs
        pop     es
        mov     di, dds
        xor     dx, dx
        mov     ax, 8107h               ; request DMA buffer
        int     4Bh
        pop     es
        jc      .fail
        mov     byte [bs_vds], 1
        mov     eax, [dds_phys]
        lea     edx, [eax+512]
        cmp     edx, 0A0000h
        ja      .free
        mov     dx, ax
        and     dx, 0Fh
        mov     [bs_off], dx
        shr     eax, 4
        mov     [bs_seg], ax
        clc
        ret
.free:  call    bd_lowbuf_free
.fail:  stc
        ret

bd_lowbuf_free:
        cmp     byte [bs_vds], 0
        je      .out
        mov     byte [bs_vds], 0
        push    es
        push    cs
        pop     es
        mov     di, dds
        xor     dx, dx
        mov     ax, 8108h               ; release DMA buffer
        int     4Bh
        pop     es
.out:   ret

bd_scan_bios:
        call    bd_lowbuf
        jnc     .buf
        push    es
        xor     ax, ax
        mov     es, ax
        cmp     byte [es:475h], 0
        pop     es
        je      .nobuf
        cmp     byte [opt_quiet], 0
        jne     .nobuf
        mov     si, msg_bdnobuf
        call    print
.nobuf: ret
.buf:   call    .scan
        jmp     bd_lowbuf_free
.scan:
        push    es
        xor     ax, ax
        mov     es, ax
        mov     al, [es:475h]
        pop     es
        mov     [bs_left], al
        mov     byte [bs_drive], 80h
.drive: cmp     byte [bs_left], 0
        je      .out
        cmp     byte [bc_count], BC_MAX
        jae     .out
        movzx   esi, byte [bc_count]
        mov     dl, [bs_drive]
        mov     ah, 08h
        push    es
        int     13h
        pop     es
        jc      .next
        movzx   esi, byte [bc_count]
        mov     [bc_cx+esi*2], cx
        mov     [bc_dh+esi], dh
        mov     dword [bc_total+esi*4], 0

        mov     ah, 41h                 ; EDD: size and interface
        mov     bx, 55AAh
        mov     dl, [bs_drive]
        int     13h
        jc      .sect
        cmp     bx, 0AA55h
        jne     .sect
        test    cl, 1
        jz      .sect
        mov     si, [bounce_ptr]
        add     si, 200h
        mov     di, si
        push    es
        push    ds
        pop     es
        mov     cx, 42h
        xor     al, al
        rep     stosb
        pop     es
        mov     word [si], 42h
        mov     ah, 48h
        mov     dl, [bs_drive]
        int     13h
        jc      .sect
        mov     si, [bounce_ptr]
        add     si, 200h
        mov     ax, [si+18h]            ; bytes per sector
        or      ax, ax
        jz      .size
        cmp     ax, 512
        jne     .next
.size:  cmp     dword [si+14h], 0       ; beyond 2 TB: not for us
        jne     .next
        mov     eax, [si+10h]
        movzx   edi, byte [bc_count]
        mov     [bc_total+edi*4], eax
        cmp     word [si], 42h          ; EDD 3.0 path: ATA / SATA is not USB
        jb      .sect
        cmp     word [si+1Eh], 0BEDDh
        jne     .sect
        cmp     word [si+30h], 'AT'
        jne     .sata
        cmp     byte [si+32h], 'A'
        je      .next
.sata:  cmp     dword [si+30h], 'SATA'
        je      .next

.sect:  mov     cx, 2                   ; sector 0
.read:  push    cx
        mov     ax, 0201h
        mov     cx, 1
        xor     dh, dh
        mov     dl, [bs_drive]
        mov     bx, [bs_off]
        push    es
        mov     es, [bs_seg]
        int     13h
        pop     es
        pop     cx
        jnc     .got
        loop    .read
        jmp     .next
.got:   push    ds
        mov     si, [bs_off]
        mov     ds, [bs_seg]
        call    bd_hash
        pop     ds
        movzx   esi, byte [bc_count]
        mov     [bc_hash+esi*4], eax
        mov     al, [bs_drive]
        mov     [bc_drive+esi], al
        mov     byte [bc_dev+esi], 0FFh
        inc     byte [bc_count]
.next:  inc     byte [bs_drive]
        dec     byte [bs_left]
        jmp     .drive
.out:   ret

; run cdb_buf on entry BX into the bounce buffer, ECX bytes, 4 tries
; -> CF on failure
bd_cmd:
        mov     [bm_len], ecx
        mov     byte [bm_try], 4
        mov     eax, [cdb_buf]
        mov     [bm_cdb], eax
        mov     eax, [cdb_buf+4]
        mov     [bm_cdb+4], eax
        mov     eax, [cdb_buf+8]
        mov     [bm_cdb+8], eax
.t:     mov     eax, [bm_cdb]
        mov     [cdb_buf], eax
        mov     eax, [bm_cdb+4]
        mov     [cdb_buf+4], eax
        mov     eax, [bm_cdb+8]
        mov     [cdb_buf+8], eax
        mov     ecx, [bm_len]
        xor     dl, dl
        call    packet_bounce
        cmp     al, XR_OK
        je      .ok
        dec     byte [bm_try]
        jnz     .t
        stc
        ret
.ok:    clc
        ret

bd_cdb_clear:
        xor     eax, eax
        mov     [cdb_buf], eax
        mov     [cdb_buf+4], eax
        mov     [cdb_buf+8], eax
        mov     [cdb_buf+12], eax
        ret

; AL = BIOS drive -> AL = 01h when DOS has a drive on it (its drive data
; tables, INT 2Fh AX=0803h; without them: assume it has), else 02h
bd_dos_has:
        push    bx
        push    cx
        push    di
        push    es
        mov     cl, al
        push    ds                      ; no install check: FreeDOS answers
        xor     ax, ax                  ; 0800h with 00h but has 0803h;
        mov     ds, ax                  ; DS still 0 = not supported
        mov     ax, 0803h               ; DS:DI = first drive data table
        int     2Fh
        mov     ax, ds
        pop     ds
        or      ax, ax
        jz      .yes
        mov     es, ax
.ddt:   cmp     di, 0FFFFh
        je      .no
        cmp     [es:di+4], cl           ; its BIOS drive
        je      .yes
        les     di, [es:di]
        jmp     .ddt
.no:    mov     al, 02h
        jmp     .out
.yes:   mov     al, 01h
.out:   pop     es
        pop     di
        pop     cx
        pop     bx
        ret

bd_match:
        cmp     byte [bc_count], 0
        je      .ret
        xor     bx, bx
.dev:   cmp     bl, [dev_count]
        jae     .take
        cmp     byte [dev_trans+bx], TR_USB
        jne     .nextd
        cmp     byte [dev_type+bx], 0
        jne     .nextd
        call    bd_cdb_clear            ; READ CAPACITY(10)
        mov     byte [cdb_buf], 25h
        mov     ecx, 8
        call    bd_cmd
        jc      .nextd
        mov     si, [bounce_ptr]
        cmp     dword [si+4], 00020000h ; 512 byte blocks
        jne     .nextd
        mov     eax, [si]
        xchg    al, ah
        rol     eax, 16
        xchg    al, ah
        inc     eax
        mov     [bm_cap], eax
        call    bd_cdb_clear            ; READ(10) of block 0
        mov     byte [cdb_buf], 28h
        mov     byte [cdb_buf+8], 1
        mov     ecx, 512
        call    bd_cmd
        jc      .nextd
        mov     si, [bounce_ptr]
        call    bd_hash
        mov     [bm_hash], eax
        xor     esi, esi
.cand:  movzx   ax, byte [bc_count]
        cmp     si, ax
        jae     .nextd
        mov     eax, [bm_hash]
        cmp     eax, [bc_hash+esi*4]
        jne     .ncand
        mov     eax, [bc_total+esi*4]
        or      eax, eax
        jz      .chs
        cmp     eax, [bm_cap]
        jne     .ncand
        jmp     .hit
.chs:   mov     cx, [bc_cx+esi*2]       ; cylinders * heads * sectors
        movzx   eax, cl
        shl     eax, 2
        and     ax, 300h
        mov     al, ch
        inc     eax
        movzx   edx, byte [bc_dh+esi]
        inc     edx
        imul    eax, edx
        movzx   edx, cl
        and     dl, 3Fh
        imul    eax, edx
        cmp     eax, [bm_cap]
        ja      .ncand
.hit:   cmp     byte [bc_dev+esi], 0FFh
        je      .set
        mov     byte [bc_dev+esi], 0FEh
        jmp     .ncand
.set:   mov     [bc_dev+esi], bl
        mov     eax, [bm_cap]
        mov     [bc_cap+esi*4], eax
.ncand: inc     si
        jmp     .cand
.nextd: inc     bx
        jmp     .dev

.take:  xor     esi, esi
.tk:    movzx   ax, byte [bc_count]
        cmp     si, ax
        jae     .hook
        mov     al, [bc_dev+esi]
        cmp     al, 0FEh
        jne     .tk1
        push    si                      ; two devices fit
        mov     al, [bc_drive+esi]
        mov     si, msg_bd1
        call    print
        call    print_hex8
        mov     si, msg_bdamb
        call    print
        pop     si
        jmp     .tknext
.tk1:   cmp     al, 0FFh
        je      .tknext
        movzx   bx, al
        cmp     byte [dev_bios+bx], 0FFh        ; one BIOS drive per device
        jne     .tknext
        movzx   edi, byte [bd_count]
        cmp     di, BD_MAX
        jae     .hook
        mov     al, [bc_drive+esi]
        mov     [bd_drive+edi], al
        mov     [dev_bios+bx], al
        mov     [bd_dev+edi], bl
        mov     ax, [bc_cx+esi*2]
        mov     [bd_cx+edi*2], ax
        mov     al, [bc_dh+esi]
        mov     [bd_dh+edi], al
        mov     eax, [bc_cap+esi*4]
        mov     [bd_total+edi*4], eax
        mov     al, [bd_drive+edi]
        call    bd_dos_has
        mov     [bd_dos+edi], al
        inc     byte [bd_count]
        cmp     byte [opt_quiet], 0
        jne     .tknext
        push    si
        mov     si, msg_bd1
        call    print
        mov     al, [bd_drive+edi]
        call    print_hex8
        mov     si, msg_bd2
        call    print
        mov     al, [dev_tgt+bx]
        xor     ah, ah
        call    print_dec
        mov     si, msg_bd3
        cmp     byte [bd_dos+edi], 01h
        je      .tkmsg
        mov     si, msg_bd3nd
.tkmsg: call    print
        pop     si
.tknext:
        inc     si
        jmp     .tk

.hook:  cmp     byte [bd_count], 0
        je      .ret
        push    es
        xor     ax, ax
        mov     es, ax
        cli
        mov     eax, [es:13h*4]
        mov     [bd_old13], eax
        mov     word [es:13h*4], bd_int13_vec
        mov     [es:13h*4+2], cs
        sti
        pop     es
        push    ds                      ; DOS's pointer (INT 2Fh AH=13h)
        push    es
        mov     ah, 13h
        mov     dx, bd_int13_dos
        mov     bx, dx
        push    cs
        pop     es
        int     2Fh                     ; DS:DX, ES:BX = the previous ones
        mov     [cs:bd_dos13], dx
        mov     [cs:bd_dos13+2], ds
        mov     [cs:bd_romtmp], bx
        mov     [cs:bd_romtmp+2], es
        mov     ax, cs
        mov     ds, ax
        cmp     word [bd_dos13], bd_int13_dos   ; not supported: vector only
        jne     .rom
        cmp     [bd_dos13+2], ax
        jne     .rom
        mov     eax, [bd_old13]
        mov     [bd_dos13], eax
        jmp     .hooked
.rom:   mov     ah, 13h                 ; the halt / reboot pointer stays
        mov     dx, bd_int13_dos
        les     bx, [bd_romtmp]
        int     2Fh
        mov     ax, cs
        mov     ds, ax
.hooked:
        pop     es
        pop     ds
.ret:   ret
