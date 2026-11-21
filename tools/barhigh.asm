; BARHIGH.SYS - test helper: move the 64-bit BAR0 of every xHCI controller
; 4 GB up (upper half 1), as firmware with "Above 4G Decoding" places it,
; then unload.  For test/pci4g.sh (PCI4G.SYS moves it back below 4 GB).
        cpu     386
        bits    16
        org     0
        dd      -1
        dw      8000h, strategy, interrupt
        db      'BARHIGH$'
req     dd      0
strategy:
        mov     [cs:req], bx
        mov     [cs:req+2], es
        retf
interrupt:
        pushad
        push    ds
        push    es
        push    cs
        pop     ds
        xor     si, si                  ; index
.find:  mov     ax, 0B103h              ; find class code
        mov     ecx, 0C0330h
        int     1Ah
        jc      .done
        mov     di, 10h
        mov     ax, 0B10Ah
        int     1Ah
        and     cl, 6
        cmp     cl, 4                   ; 64 bit memory BAR
        jne     .next
        mov     di, 14h
        mov     ecx, 1
        mov     ax, 0B10Dh
        int     1Ah
        mov     dx, msg
        mov     ah, 9
        int     21h
.next:  inc     si
        jmp     .find
.done:  les     bx, [req]
        mov     word [es:bx+0Eh], 0
        mov     [es:bx+10h], cs
        mov     word [es:bx+3], 0100h
        pop     es
        pop     ds
        popad
        retf
msg     db      'BARHIGH: xHCI BAR0 moved above 4 GB', 13, 10, '$'
