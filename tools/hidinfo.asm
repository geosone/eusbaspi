; HIDINFO.COM - lists the USB HID interfaces EUSBASPI keeps for client
; drivers (AMIS "GeosOne EUSBASPI", see src/hid.inc) and reads the report
; descriptor of each through EUSBASPI's control transfer.
;   HIDINFO          the list
;   HIDINFO /R       and the reports of the first mouse for 8 seconds
;   HIDINFO /Q /MOUSE  no output; errorlevel 0 if there is a USB mouse
;                      (boot interface), else 1 (for INSTALL's [Detect])
        org     100h
        mov     si, 81h                 ; options
.opt:   lodsb
        cmp     al, 13
        je      .opte
        cmp     al, '/'
        jne     .opt
        lodsb
        and     al, 0DFh
        cmp     al, 'Q'
        jne     .o2
        mov     byte [quiet], 1
.o2:    cmp     al, 'R'
        jne     .o3
        mov     byte [reports], 1
.o3:    cmp     al, 'M'
        jne     .opt
        mov     byte [wantm], 1
        jmp     .opt
.opte:  xor     ax, ax                  ; AMIS handler at all?
        mov     es, ax
        mov     ax, [es:2Dh*4]
        or      ax, [es:2Dh*4+2]
        jz      none
        xor     bx, bx
.scan:  mov     ah, bh
        xor     al, al
        push    bx
        int     2Dh
        pop     bx
        cmp     al, 0FFh
        jne     .n
        push    ds
        mov     ds, dx
        mov     si, di
        push    cs
        pop     es
        mov     di, sig
        mov     cx, 16
        repe    cmpsb
        pop     ds
        je      found
.n:     inc     bh
        jnz     .scan
none:   mov     dx, msg_none
        call    puts
        mov     ax, 4C01h
        int     21h

found:  mov     ah, bh
        mov     al, 1                   ; private entry point
        int     2Dh
        mov     [api], bx
        mov     [api+2], dx
        xor     ah, ah                  ; info
        call    far [api]
        mov     [count], cl
        mov     dx, msg_cnt
        call    puts
        movzx   ax, byte [count]
        call    putn
        mov     dx, crlf
        call    puts

        xor     bl, bl
.f:     cmp     bl, [count]
        jae     .end
        push    bx
        push    ds
        pop     es
        mov     di, rec
        mov     ah, 1
        call    far [api]
        pop     bx
        mov     dx, msg_f
        call    puts
        movzx   ax, bl
        call    putn
        mov     dx, msg_dev
        call    puts
        movzx   ax, byte [rec+0]
        call    putn
        mov     dx, msg_if
        call    puts
        movzx   ax, byte [rec+1]
        call    putn
        mov     dx, msg_sub
        call    puts
        movzx   ax, byte [rec+2]
        call    putn
        mov     dx, msg_proto
        call    puts
        movzx   ax, byte [rec+3]
        call    putn
        mov     dx, msg_ep
        call    puts
        movzx   ax, byte [rec+4]
        call    putn
        mov     dx, msg_mps
        call    puts
        mov     ax, [rec+6]
        call    putn
        mov     dx, msg_speed
        call    puts
        movzx   ax, byte [rec+12]
        call    putn
        mov     dx, msg_hc
        call    puts
        movzx   ax, byte [rec+13]
        call    putn

        ; GET_DESCRIPTOR(report) on the interface
        mov     word [setup], 0681h
        mov     word [setup+2], 2200h
        movzx   ax, byte [rec+1]
        mov     [setup+4], ax
        mov     word [setup+6], 128
        mov     word [setup+8], buf
        mov     [setup+10], ds
        push    bx
        push    ds
        pop     es
        mov     di, setup
        mov     ah, 2
        call    far [api]
        pop     bx
        push    ax
        mov     dx, msg_rep
        call    puts
        pop     ax
        or      al, al
        jnz     .err
        mov     ax, cx
        call    putn
        mov     dx, msg_bytes
        call    puts
        jmp     .nx
.err:   mov     dx, msg_fail
        call    puts
        movzx   ax, al
        call    putn
        mov     dx, crlf
        call    puts
.nx:    inc     bl
        jmp     .f
.end:   cmp     byte [wantm], 0
        jne     .m0
        cmp     byte [reports], 0
        je      .quit
        ; the first mouse: start its pipe and show its reports for 8 s
.m0:    xor     bl, bl
.m:     cmp     bl, [count]
        jae     .quit
        push    bx
        push    ds
        pop     es
        mov     di, rec
        mov     ah, 1
        call    far [api]
        pop     bx
        cmp     byte [rec+2], 1         ; boot interface
        jne     .mn
        cmp     byte [rec+3], 2
        je      .mouse
.mn:    inc     bl
        jmp     .m
.mouse: cmp     byte [wantm], 0
        je      .show
        mov     ax, 4C00h               ; /MOUSE: there is one
        int     21h
.show:  mov     [mfunc], bl
        mov     ah, 3
        call    far [api]
        push    ax
        mov     dx, msg_start
        call    puts
        pop     ax
        movzx   ax, al
        call    putn
        mov     dx, crlf
        call    puts
        mov     dx, msg_ready
        call    puts
        push    0
        pop     es
        mov     ax, [es:46Ch]
        mov     [t0], ax
.poll:  push    ds
        pop     es
        mov     di, buf
        mov     bl, [mfunc]
        mov     ah, 4
        call    far [api]
        cmp     al, 1
        jne     .nor
        mov     dx, msg_rp
        call    puts
        mov     si, buf
.hex:   lodsb
        call    puthex
        loop    .hex
        mov     dx, crlf
        call    puts
        jmp     .t
.nor:   or      al, al
        jz      .t
        push    ax
        mov     dx, msg_perr
        call    puts
        pop     ax
        movzx   ax, al
        call    putn
        mov     dx, crlf
        call    puts
        jmp     .quit
.t:     push    0
        pop     es
        mov     ax, [es:46Ch]
        sub     ax, [t0]
        cmp     ax, 8 * 18
        jb      .poll
        mov     bl, [mfunc]
        mov     ah, 5
        call    far [api]
.quit:  mov     al, [wantm]         ; /MOUSE and none: 1
        mov     ah, 4Ch
        int     21h

puthex: push    ax
        push    cx
        mov     ah, al
        mov     cl, 4
        shr     al, cl
        call    .dig
        mov     al, ah
        and     al, 0Fh
        call    .dig
        mov     dl, ' '
        mov     ah, 2
        int     21h
        pop     cx
        pop     ax
        ret
.dig:   push    ax
        add     al, '0'
        cmp     al, '9'
        jbe     .o
        add     al, 7
.o:     mov     dl, al
        mov     ah, 2
        int     21h
        pop     ax
        ret

puts:   cmp     byte [quiet], 0
        jne     .q
        mov     ah, 9
        int     21h
.q:     ret
putn:   cmp     byte [quiet], 0
        je      .go
        ret
.go:    push    bx
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
        mov     ah, 2
        int     21h
        loop    .p
        pop     dx
        pop     cx
        pop     bx
        ret

sig       db 'GeosOne EUSBASPI'
msg_none  db 'EUSBASPI with HID devices not found', 13, 10, '$'
msg_cnt   db 'HID interfaces: $'
msg_f     db 'function $'
msg_dev   db ': device $'
msg_if    db ' interface $'
msg_sub   db ' subclass $'
msg_proto db ' protocol $'
msg_ep    db ' endpoint $'
msg_mps   db ' packet $'
msg_speed db ' speed $'
msg_hc    db ' controller type $'
msg_rep   db 13, 10, '  report descriptor: $'
msg_bytes db ' bytes', 13, 10, '$'
msg_fail  db 'failed, $'
crlf      db 13, 10, '$'
msg_start db 'pipe start: $'
msg_ready db 'MREADY', 13, 10, '$'
msg_rp    db 'report $'
msg_perr  db 'poll error $'
api     dd 0
quiet   db 0
reports db 0
wantm   db 0
mfunc   db 0
t0      dw 0
count   db 0
rec     times 16 db 0
setup   times 12 db 0
buf     times 128 db 0
