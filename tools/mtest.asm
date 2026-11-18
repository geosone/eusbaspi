; MTEST.COM [n] - mouse test through INT 33h: resets the driver, then for n
; seconds (default 10) prints every change of position and buttons, and at
; the end "mouse moved" / "left button" / "right button" for what it saw.
; "MREADY" tells the test (test/run.sh SWAP_ON) to send the events.
        org     100h
        mov     si, 81h
        xor     bx, bx
.skip:  lodsb
        cmp     al, ' '
        je      .skip
.dig:   sub     al, '0'
        cmp     al, 9
        ja      .end
        imul    bx, bx, 10
        add     bl, al
        adc     bh, 0
        lodsb
        jmp     .dig
.end:   or      bx, bx
        jnz     .go
        mov     bx, 10
.go:    imul    bx, bx, 18              ; seconds -> ticks
        mov     [ticks], bx

        xor     ax, ax                  ; INT 33h vector set at all?
        mov     es, ax
        mov     ax, [es:33h*4]
        or      ax, [es:33h*4+2]
        jz      .none
        xor     ax, ax                  ; reset
        int     33h
        cmp     ax, 0FFFFh
        jne     .none
        mov     dx, msg_drv
        call    puts
        mov     ax, bx
        call    putn
        mov     dx, msg_btn
        call    puts

        mov     dx, msg_ready
        call    puts
        sti
        push    0
        pop     es
        mov     ax, [es:46Ch]
        mov     [start], ax
        mov     ax, 3                   ; the start, not counted as a move
        int     33h
        mov     [lx], cx
        mov     [ly], dx
        mov     [lb], bx
.loop:  mov     ax, 3
        int     33h
        cmp     cx, [lx]
        jne     .chg
        cmp     dx, [ly]
        jne     .chg
        cmp     bx, [lb]
        je      .time
.chg:   cmp     cx, [lx]
        jne     .mv
        cmp     dx, [ly]
        je      .nomv
.mv:    mov     byte [moved], 1
.nomv:  or      [seen], bl
        mov     [lx], cx
        mov     [ly], dx
        mov     [lb], bx
        push    dx
        mov     dx, msg_pos
        call    puts
        mov     ax, cx
        call    putn
        mov     dl, ' '
        call    putc
        pop     ax
        call    putn
        mov     dx, msg_b
        call    puts
        mov     ax, [lb]
        call    putn
        mov     dx, crlf
        call    puts
.time:  hlt
        push    0
        pop     es
        mov     ax, [es:46Ch]
        sub     ax, [start]
        cmp     ax, [ticks]
        jb      .loop

        cmp     byte [moved], 0
        je      .nm
        mov     dx, msg_moved
        call    puts
.nm:    test    byte [seen], 1
        jz      .nl
        mov     dx, msg_left
        call    puts
.nl:    test    byte [seen], 2
        jz      .nr
        mov     dx, msg_right
        call    puts
.nr:    mov     dx, msg_done
        call    puts
        mov     ax, 4C00h
        int     21h
.none:  mov     dx, msg_none
        call    puts
        mov     ax, 4C01h
        int     21h

puts:   mov     ah, 9
        int     21h
        ret
putc:   mov     ah, 2
        int     21h
        ret
putn:   ; AX unsigned decimal
        push    cx
        push    dx
        xor     cx, cx
        mov     bx, 10
.d:     xor     dx, dx
        div     bx
        push    dx
        inc     cx
        or      ax, ax
        jnz     .d
.p:     pop     dx
        add     dl, '0'
        call    putc
        loop    .p
        pop     dx
        pop     cx
        ret

msg_drv   db 'mouse driver, $'
msg_btn   db ' buttons', 13, 10
msg_ready db 'MREADY', 13, 10, '$'
msg_pos   db 'pos $'
msg_b     db ' buttons $'
msg_moved db 'mouse moved', 13, 10, '$'
msg_left  db 'left button', 13, 10, '$'
msg_right db 'right button', 13, 10, '$'
msg_done  db 'mouse test done', 13, 10, '$'
msg_none  db 'no mouse driver', 13, 10, '$'
crlf      db 13, 10, '$'
ticks   dw 0
start   dw 0
lx      dw 0FFFFh
ly      dw 0FFFFh
lb      dw 0FFFFh
moved   db 0
seen    db 0
