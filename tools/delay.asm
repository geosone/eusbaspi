; DELAY.SYS - wait about two seconds during CONFIG.SYS, then unload
; (test helper: devices that need time after the BIOS)
        cpu     386
        bits    16
        org     0
        dd      -1
        dw      8000h, strategy, interrupt
        db      'DELAY$$$'
req     dd      0
strategy:
        mov     [cs:req], bx
        mov     [cs:req+2], es
        retf
interrupt:
        pushad
        push    ds
        push    es
        push    word 40h
        pop     ds
        sti
        mov     cx, 37                  ; ~2 s in BIOS ticks
.tick:  mov     ax, [6Ch]
.wait:  cmp     ax, [6Ch]
        je      .wait
        loop    .tick
        les     bx, [cs:req]
        mov     word [es:bx+0Eh], 0     ; unload
        mov     [es:bx+10h], cs
        mov     word [es:bx+3], 0100h
        pop     es
        pop     ds
        popad
        retf
