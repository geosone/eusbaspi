; WINMODE.COM - print whether Windows runs in enhanced mode (INT 2Fh 1600h)
        org     100h
        mov     ax, 1600h
        int     2Fh
        test    al, 7Fh                 ; 00h/80h: not enhanced mode
        jz      .no
        cmp     al, 1                   ; 01h/FFh: Windows/386 2.x
        je      .no
        cmp     al, 0FFh
        je      .no
        push    ax
        mov     dx, msg_yes
        mov     ah, 9
        int     21h
        pop     ax
        push    ax
        add     al, '0'
        call    putc
        mov     dl, '.'
        mov     ah, 2
        int     21h
        pop     ax
        mov     al, ah                  ; minor version (decimal, 2 digits)
        aam
        add     ax, '00'
        push    ax
        mov     al, ah
        call    putc
        pop     ax
        call    putc
        mov     dx, msg_crlf
        jmp     .out
.no:    mov     dx, msg_no
.out:   mov     ah, 9
        int     21h
        mov     ax, 4C00h
        int     21h
putc:   mov     dl, al
        mov     ah, 2
        int     21h
        ret
msg_yes  db 'Windows enhanced mode $'
msg_no   db 'not in Windows enhanced mode'
msg_crlf db 13, 10, '$'
