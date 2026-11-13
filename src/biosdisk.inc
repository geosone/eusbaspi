;=============================================================================
; biosdisk.inc - BIOS hard disks on USB devices we took over (resident part)
;
; A BIOS with USB legacy support (SeaBIOS, VMware, most PC BIOSes) offers a
; stick plugged in at boot as an INT 13h hard disk, and DOS has it as C: or
; later.  Its INT 13h code drives the USB controller itself, so once
; EUSBASPI owns the controller, the BIOS loses the disk and both trample
; on each other's controller state.  init (biosinit.inc) finds out which of
; our devices the BIOS drives are; for those, EUSBASPI answers INT 13h
; itself, through its own ASPI entry, with the geometry the BIOS reported.
; INT 13h is hooked as a vector and as DOS's pointer (INT 2Fh AH=13h); DOS's
; pointer for the halt / reboot case stays the BIOS's.  GET DISK INFO
; reports them as INT 13h drives, DOS's when DOS has a drive letter on
; them: ASPI disk drivers then give them no second one.
;=============================================================================

BD_MAX          equ     4

bd_count        db      0
bd_drive        times BD_MAX db 0       ; BIOS drive number
bd_dev          times BD_MAX db 0       ; our entry
bd_cx           times BD_MAX dw 0       ; BIOS geometry (AH=08h CX, DH)
bd_dh           times BD_MAX db 0
bd_total        times BD_MAX dd 0       ; sectors (READ CAPACITY)
bd_dos          times BD_MAX db 0       ; GET DISK INFO flag: 01h DOS has a
                                        ; drive on it, 02h INT 13h only
bd_old13        dd      0               ; INT 13h vector before us
bd_dos13        dd      0               ; DOS's INT 13h pointer before us
dev_bios        times MAX_DEV db 0FFh   ; entry -> BIOS drive, 0FFh: none

bd_op           db      0               ; READ(10) / WRITE(10)
bd_try          db      0
bd_lba          dd      0
bd_buf          dd      0
bd_left         dw      0
bd_done         dw      0
bd_n            dw      0
bd_srb          times SRB_CDB + 10 + 14 db 0

BD_CHUNK        equ     64              ; sectors per SRB (32 KB)

; INT 13h status codes
BS_OK           equ     00h
BS_BADCMD       equ     01h
BS_WPROT        equ     03h
BS_NOSECT       equ     04h
BS_CHANGED      equ     06h
BS_ECC          equ     10h
BS_CTRL         equ     20h
BS_TIMEOUT      equ     80h
BS_NOTREADY     equ     0AAh
BS_UNDEF        equ     0BBh

; caller's registers in bd_serve's frame (BP)
F_FLAGS         equ     6
F_AX            equ     -4
F_CX            equ     -8
F_DX            equ     -12
F_BX            equ     -16
F_SI            equ     -28
F_DI            equ     -32
F_DS            equ     -34
F_ES            equ     -36

; the INT 13h vector
bd_int13_vec:
        call    bd_lookup
        jnc     bd_serve
        jmp     far [cs:bd_old13]

; DOS's INT 13h pointer (called with PUSHF + CALL FAR)
bd_int13_dos:
        call    bd_lookup
        jnc     bd_serve
        jmp     far [cs:bd_dos13]

; CF clear when DL is one of our BIOS drives (all registers kept)
bd_lookup:
        push    si
        push    ax
        xor     si, si
        mov     al, [cs:bd_count]
.l:     cmp     al, 0
        je      .no
        cmp     [cs:bd_drive+si], dl
        je      .yes
        inc     si
        dec     al
        jmp     .l
.yes:   pop     ax
        pop     si
        clc
        ret
.no:    pop     ax
        pop     si
        stc
        ret

bd_serve:
        sti
        push    bp
        mov     bp, sp
        pushad
        push    ds
        push    es
        cld
        mov     ax, cs
        mov     ds, ax
        xor     di, di                  ; our table entry -> DI
.find:  cmp     [bd_drive+di], dl
        je      .found
        inc     di
        jmp     .find
.found: movzx   bx, byte [bd_dev+di]
        mov     al, BS_NOTREADY         ; the device was unplugged
        cmp     [dev_bios+bx], dl
        jne     .done
        mov     ah, [bp+F_AX+1]
        cmp     ah, 00h
        je      .ok
        cmp     ah, 01h
        je      .status
        cmp     ah, 02h
        je      .chs
        cmp     ah, 03h
        je      .chs
        cmp     ah, 04h                 ; verify
        je      .ok
        cmp     ah, 08h
        je      .params
        cmp     ah, 09h                 ; set geometry, seek, reset, ...
        je      .ok
        cmp     ah, 0Ch
        je      .ok
        cmp     ah, 0Dh
        je      .ok
        cmp     ah, 10h
        je      .ok
        cmp     ah, 11h
        je      .ok
        cmp     ah, 14h
        je      .ok
        cmp     ah, 15h
        je      .type
        cmp     ah, 41h
        je      .extchk
        cmp     ah, 42h
        je      .ext
        cmp     ah, 43h
        je      .ext
        cmp     ah, 44h                 ; extended verify, seek
        je      .ok
        cmp     ah, 47h
        je      .ok
        cmp     ah, 48h
        je      .extpar
        mov     al, BS_BADCMD
        jmp     .done

.ok:    xor     al, al
.done:  ; AL = status: caller's AH, CF and the BIOS's last status
        mov     [bp+F_AX+1], al
        push    40h
        pop     es
        mov     [es:74h], al
        and     byte [bp+F_FLAGS], 0FEh
        or      al, al
        jz      .ret
        or      byte [bp+F_FLAGS], 1
.ret:   pop     es
        pop     ds
        popad
        pop     bp
        iret
.retah: ; AH = AL, CF clear, last status untouched
        mov     [bp+F_AX+1], al
        and     byte [bp+F_FLAGS], 0FEh
        jmp     .ret

.status:
        push    40h
        pop     es
        mov     al, [es:74h]
        jmp     .done

.chs:   ; AH=02h/03h: AL sectors from CH/CL/DH to ES:BX
        mov     byte [bd_op], 28h
        cmp     ah, 03h
        jne     .chsrd
        mov     byte [bd_op], 2Ah
.chsrd: mov     cx, [bp+F_CX]
        mov     dx, [bp+F_DX]
        mov     si, di
        add     si, si
        mov     ax, [bd_cx+si]          ; sectors per track
        and     ax, 3Fh
        mov     si, cx
        and     si, 3Fh                 ; sector, 1 based
        jz      .nosect
        cmp     si, ax
        ja      .nosect
        cmp     dh, [bd_dh+di]          ; head
        ja      .nosect
        push    ax                      ; cylinder
        movzx   eax, cl
        shl     eax, 2
        and     ax, 300h
        mov     al, ch
        movzx   ecx, byte [bd_dh+di]    ; * heads + head
        inc     ecx
        imul    eax, ecx
        movzx   ecx, dh
        add     eax, ecx
        pop     cx                      ; * sectors per track + sector - 1
        movzx   ecx, cx
        imul    eax, ecx
        dec     si
        movzx   esi, si
        add     eax, esi
        movzx   cx, byte [bp+F_AX]      ; count
        mov     dx, [bp+F_ES]
        shl     edx, 16
        mov     dx, [bp+F_BX]
        call    bd_rw
        mov     [bp+F_AX], cl           ; sectors done
        jmp     .done
.nosect:
        mov     byte [bp+F_AX], 0
        mov     al, BS_NOSECT
        jmp     .done

.ext:   ; AH=42h/43h: disk address packet at DS:SI
        mov     byte [bd_op], 28h
        cmp     ah, 43h
        jne     .extrd
        mov     byte [bd_op], 2Ah
.extrd: mov     es, [bp+F_DS]
        mov     si, [bp+F_SI]
        mov     al, BS_BADCMD
        cmp     byte [es:si], 10h
        jb      .done
        mov     edx, [es:si+4]          ; buffer; FFFF:FFFF = 64 bit address
        cmp     edx, 0FFFFFFFFh
        je      .done
        mov     al, BS_NOSECT
        cmp     dword [es:si+12], 0
        jne     .extbad
        mov     eax, [es:si+8]
        mov     cx, [es:si+2]
        call    bd_rw
        mov     es, [bp+F_DS]
        mov     si, [bp+F_SI]
        mov     [es:si+2], cx           ; blocks done
        jmp     .done
.extbad:
        mov     word [es:si+2], 0
        jmp     .done

.params: ; AH=08h: the BIOS's answer
        mov     si, di
        add     si, si
        mov     ax, [bd_cx+si]
        mov     [bp+F_CX], ax
        mov     al, [bd_dh+di]
        mov     [bp+F_DX+1], al
        push    0
        pop     es
        mov     al, [es:475h]           ; number of hard disks
        mov     [bp+F_DX], al
        mov     byte [bp+F_AX], 0
        jmp     .ok

.type:  ; AH=15h: hard disk, CX:DX sectors
        mov     si, di
        shl     si, 2
        mov     eax, [bd_total+si]
        mov     [bp+F_DX], ax
        shr     eax, 16
        mov     [bp+F_CX], ax
        mov     al, 03h
        jmp     .retah

.extchk: ; AH=41h: EDD 1.1, fixed disk access subset
        mov     al, BS_BADCMD
        cmp     word [bp+F_BX], 55AAh
        jne     .done
        mov     word [bp+F_BX], 0AA55h
        mov     word [bp+F_CX], 0001h
        mov     al, 21h
        jmp     .retah

.extpar: ; AH=48h: drive parameters at DS:SI
        mov     es, [bp+F_DS]
        mov     si, [bp+F_SI]
        mov     ax, [es:si]
        cmp     ax, 1Ah
        mov     al, BS_BADCMD
        jb      .done
        cmp     word [es:si], 1Eh
        mov     word [es:si], 1Ah
        jb      .epar
        mov     word [es:si], 1Eh
        mov     dword [es:si+1Ah], 0FFFFFFFFh   ; no DPTE
.epar:  mov     word [es:si+2], 0002h   ; CHS information valid
        push    si
        mov     si, di
        add     si, si
        mov     cx, [bd_cx+si]
        pop     si
        movzx   eax, cl                 ; cylinders
        shl     eax, 2
        and     ax, 300h
        mov     al, ch
        inc     eax
        mov     [es:si+4], eax
        movzx   eax, byte [bd_dh+di]    ; heads
        inc     eax
        mov     [es:si+8], eax
        movzx   eax, cl                 ; sectors per track
        and     al, 3Fh
        mov     [es:si+12], eax
        push    di
        shl     di, 2
        mov     eax, [bd_total+di]
        pop     di
        mov     [es:si+16], eax
        mov     dword [es:si+20], 0
        mov     word [es:si+24], 512
        jmp     .ok

; EAX = LBA, CX = sectors, EDX = buffer (segment:offset), BX = entry,
; [bd_op] = READ(10) / WRITE(10) -> AL = INT 13h status, CX = sectors done
bd_rw:
        mov     [bd_lba], eax
        mov     [bd_buf], edx
        mov     [bd_left], cx
        mov     word [bd_done], 0
.chunk: mov     cx, [bd_left]
        jcxz    .ok
        cmp     cx, BD_CHUNK
        jbe     .n
        mov     cx, BD_CHUNK
.n:     mov     [bd_n], cx
        mov     byte [bd_try], 3
.try:   call    bd_exec
        or      al, al
        jz      .good
        cmp     al, BS_WPROT            ; no use trying again
        je      .out
        cmp     al, BS_NOSECT
        je      .out
        dec     byte [bd_try]
        jnz     .try
        jmp     .out
.good:  mov     cx, [bd_n]
        add     [bd_done], cx
        sub     [bd_left], cx
        movzx   eax, cx
        add     [bd_lba], eax
        shl     cx, 5                   ; 512 bytes = 32 paragraphs
        add     [bd_buf+2], cx
        jmp     .chunk
.ok:    xor     al, al
.out:   mov     cx, [bd_done]
        ret

; one READ(10) / WRITE(10) of [bd_n] sectors through our ASPI entry
; -> AL = INT 13h status
bd_exec:
        push    es
        push    di
        push    ds
        pop     es
        mov     di, bd_srb
        mov     cx, SRB_CDB + 10 + 14
        xor     al, al
        rep     stosb
        mov     byte [bd_srb+SRB_Cmd], SC_EXEC_SCSI_CMD
        mov     al, [prev_count]
        mov     [bd_srb+SRB_HaId], al
        mov     al, SRB_DIR_IN
        cmp     byte [bd_op], 2Ah
        jne     .dir
        mov     al, SRB_DIR_OUT
.dir:   mov     [bd_srb+SRB_Flags], al
        mov     al, [dev_tgt+bx]
        mov     [bd_srb+SRB_Target], al
        mov     al, [dev_lun+bx]
        mov     [bd_srb+SRB_Lun], al
        movzx   eax, word [bd_n]
        shl     eax, 9
        mov     [bd_srb+SRB_BufLen], eax
        mov     byte [bd_srb+SRB_SenseLen], 14
        mov     eax, [bd_buf]
        mov     [bd_srb+SRB_BufPointer], eax
        mov     byte [bd_srb+SRB_CDBLen], 10
        mov     al, [bd_op]
        mov     [bd_srb+SRB_CDB], al
        mov     eax, [bd_lba]           ; big endian
        xchg    al, ah
        rol     eax, 16
        xchg    al, ah
        mov     [bd_srb+SRB_CDB+2], eax
        mov     ax, [bd_n]
        xchg    al, ah
        mov     [bd_srb+SRB_CDB+7], ax
        push    bx
        push    ds                      ; ASPI call: far, SRB pointer pushed
        push    word bd_srb
        push    cs
        call    aspi_entry
        add     sp, 4
        pop     bx
        mov     al, [bd_srb+SRB_Status]
        cmp     al, SS_COMP
        jne     .err
        xor     al, al
        jmp     .out
.err:   mov     ah, al
        mov     al, BS_NOTREADY
        cmp     ah, SS_NO_DEVICE
        je      .out
        mov     al, BS_TIMEOUT
        cmp     byte [bd_srb+SRB_TargStat], STATUS_CHKCOND
        jne     .out
        mov     ah, [bd_srb+SRB_CDB+10+2]       ; sense key
        and     ah, 0Fh
        mov     al, BS_NOTREADY
        cmp     ah, 2
        je      .out
        cmp     ah, 6                   ; unit attention: try again
        je      .out
        mov     al, BS_ECC
        cmp     ah, 3
        je      .out
        mov     al, BS_CTRL
        cmp     ah, 4
        je      .out
        mov     al, BS_NOSECT
        cmp     ah, 5
        je      .out
        mov     al, BS_WPROT
        cmp     ah, 7
        je      .out
        mov     al, BS_UNDEF
.out:   pop     di
        pop     es
        ret

; GET DISK INFO for entry BX: is it a BIOS drive?  ES:SI = SRB
bd_disk_info:
        mov     al, [dev_bios+bx]
        cmp     al, 0FFh
        je      .out
        push    di
        xor     di, di
.f:     cmp     di, BD_MAX
        jae     .nf
        cmp     [bd_drive+di], al
        je      .got
        inc     di
        jmp     .f
.got:   mov     ah, [bd_dos+di]         ; INT 13h drive, DOS's or not
        mov     [es:si+SRB_DriveFlags], ah
        mov     [es:si+SRB_Int13HDriveInfo], al
        mov     al, [bd_dh+di]
        inc     al
        mov     [es:si+SRB_Heads], al
        add     di, di
        mov     al, [bd_cx+di]
        and     al, 3Fh
        mov     [es:si+SRB_Sectors], al
.nf:    pop     di
.out:   ret
