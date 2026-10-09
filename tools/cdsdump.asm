;=============================================================================
; CDSDUMP.COM - show DOS drive letter tables (MS-DOS 4.0 - 7.x)
;
; For every drive letter up to LASTDRIVE: CDS flags, the drive parameter
; block it points to (drive and unit number) and the device driver header
; that DOS sends requests to.  Useful to check drive letter changes made by
; DLMANIP.
;
; Usage: CDSDUMP
;=============================================================================
        cpu     386
        bits    16
        org     100h

CDS_SIZE        equ     58h             ; DOS 4.0 and later
CDS_FLAGS       equ     43h
CDS_DPB         equ     45h
LOL_CDS         equ     16h
LOL_LASTDRIVE   equ     21h
DPB_DRIVE       equ     00h
DPB_UNIT        equ     01h
DPB_DRIVER      equ     13h

start:
        mov     ah, 52h                 ; list of lists
        int     21h
        mov     al, [es:bx+LOL_LASTDRIVE]
        mov     [lastdrive], al
        les     di, [es:bx+LOL_CDS]
        mov     si, msg_head
        call    print
        xor     cx, cx
.drive: cmp     cl, [lastdrive]
        jae     .done
        mov     al, cl
        add     al, 'A'
        call    putc
        mov     si, msg_colon
        call    print
        mov     ax, [es:di+CDS_FLAGS]
        call    hex16
        test    ax, 0C000h              ; network or physical drive?
        jz      .next
        call    space
        push    es
        push    di
        les     di, [es:di+CDS_DPB]
        mov     ax, es
        or      ax, di
        jz      .nodpb
        mov     al, [es:di+DPB_DRIVE]
        add     al, 'A'
        call    putc
        call    space
        mov     al, [es:di+DPB_UNIT]
        call    hex8
        call    space
        les     di, [es:di+DPB_DRIVER]
        mov     ax, es
        call    hex16
        mov     al, ':'
        call    putc
        mov     ax, di
        call    hex16
        call    space
        test    byte [es:di+5], 80h     ; character device has a name
        jnz     .nodpb
        mov     al, [es:di+10]          ; block device: number of units
        call    hex8
.nodpb: pop     di
        pop     es
.next:  call    crlf
        add     di, CDS_SIZE
        inc     cx
        jmp     .drive
.done:  mov     ax, 4C00h
        int     21h

space:  mov     al, ' '
putc:   push    ax
        push    dx
        mov     dl, al
        mov     ah, 2
        int     21h
        pop     dx
        pop     ax
        ret

crlf:   mov     al, 13
        call    putc
        mov     al, 10
        jmp     putc

print:  push    ax
.l:     mov     al, [cs:si]
        or      al, al
        jz      .e
        call    putc
        inc     si
        jmp     .l
.e:     pop     ax
        ret

hex16:  push    ax
        mov     al, ah
        call    hex8
        pop     ax
hex8:   push    ax
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
        jbe     putc
        add     al, 7
        jmp     putc

msg_head        db      'Drv Flags DPB-drive Unit Driver    Units', 13, 10, 0
msg_colon       db      ': ', 0
lastdrive       db      0
