; SLEEP.COM [n] - wait n seconds (default 5) using the BIOS tick counter
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
        mov     bx, 5
.go:    imul    bx, bx, 18              ; seconds -> ticks
        sti
        push    0
        pop     es
        mov     ax, [es:46Ch]
.wait:  hlt
        mov     dx, [es:46Ch]
        sub     dx, ax
        cmp     dx, bx
        jb      .wait
        mov     ax, 4C00h
        int     21h
