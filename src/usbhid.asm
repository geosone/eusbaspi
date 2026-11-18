;=============================================================================
; USBHID.SYS - USB mouse for DOS through EUSBASPI.SYS
;
;   DEVICE=EUSBASPI.SYS
;   DEVICE=USBHID.SYS [/V]
;
; EUSBASPI keeps the USB HID devices configured and lends them through its
; AMIS interface (src/hid.inc).  USBHID takes all boot mice (buttons and
; movements of all of them together), reads their reports from the timer
; interrupt and offers them as the BIOS PS/2 pointing device (INT 15h
; AX=C200h..C207h), which the DOS mouse drivers use: Microsoft MOUSE
; (packets of 1 byte), CuteMouse (3 bytes), Logitech MOUSE ...  The packets
; go through the keyboard controller (8042 command D3h), so they arrive with
; a real IRQ 12 on port 60h, also for drivers that read it themselves
; (Logitech).  Without an 8042 USBHID calls INT 74h itself.
; A BIOS that drove the USB mouse itself (SeaBIOS) lost it when EUSBASPI
; took the controllers over; this gives it back.
;
; Copyright (C) 2026 GeosOne.  GNU General Public License version 2 or later.
;=============================================================================
        cpu     386
        org     0

RQ_CMD          equ     2
RQ_STATUS       equ     3
RQ_END          equ     0Eh
RQ_ARGS         equ     12h

hdr_next        dd      -1
hdr_attr        dw      8000h           ; character device
hdr_strat       dw      strategy
hdr_intr        dw      interrupt
hdr_name        db      'USBHID$ '

req_ptr         dd      0

strategy:
        mov     [cs:req_ptr], bx
        mov     [cs:req_ptr+2], es
        retf

interrupt:
        push    bx
        push    ds
        lds     bx, [cs:req_ptr]
        cmp     byte [bx+RQ_CMD], 0
        jne     .other
        push    ax
        push    cx
        push    dx
        push    si
        push    di
        push    es
        push    bp
        call    word [cs:init_vec]
        pop     bp
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     ax
        jmp     .out
.other: mov     word [bx+RQ_STATUS], 0100h ; done (nothing to do)
.out:   pop     ds
        pop     bx
        retf

init_vec        dw      init

;-----------------------------------------------------------------------------
; resident data
;-----------------------------------------------------------------------------
api             dd      0               ; EUSBASPI's HID entry point
MICE            equ     8
mice            times MICE db 0         ; the mice's HID functions
mbtn            times MICE db 0         ; their buttons
mcount          db      0
                db      0
mcount_w        equ     mcount
mcur            dw      0
old_08          dd      0
old_15          dd      0
old_74          dd      0
pending         db      0               ; a packet for our INT 74h
kbc_ok          db      0               ; packets through the 8042 (D3h)
icnt            db      0               ; bytes of the packet so far
ibuf            db      0, 0, 0
busy            db      0
rbuf            times 128 db 0          ; one report

; PS/2 pointing device state (INT 15h C2xx)
ps_enabled      db      0
ps_handler      dd      0
ps_size         db      3               ; packet size (C205h)
ps_rate         db      100             ; samples per second (C202h)
ps_res          db      2               ; resolution (C203h): 4 counts/mm
ps_scale        db      0               ; 1: 2:1 scaling
ps_buttons      db      0
pkt             db      0, 0, 0         ; status, X, Y

rates           db      10, 20, 40, 60, 80, 100, 200

;-----------------------------------------------------------------------------
; timer: chain, then read the mouse and call the handler
;-----------------------------------------------------------------------------
int08:
        pushf
        call    far [cs:old_08]         ; BIOS tick, EOI
        cmp     byte [cs:busy], 0
        jne     .ret
        mov     byte [cs:busy], 1
        pushad
        push    ds
        push    es
        push    cs
        pop     ds
        sti
        xor     si, si                  ; every mouse
.mouse: cmp     si, [mcount_w]
        jae     .done
        mov     [mcur], si
.again: push    cs
        pop     es
        mov     di, rbuf
        mov     bl, [mice+si]
        mov     ah, 4
        push    si
        call    far [api]
        pop     si
        cmp     al, 1
        jne     .next
        cmp     cx, 3                   ; boot report: buttons, X, Y (, wheel)
        jb      .again
        push    si
        call    report
        pop     si
        jmp     .again
.next:  inc     si
        jmp     .mouse
.done:  cli
        pop     es
        pop     ds
        popad
        mov     byte [cs:busy], 0
.ret:   iret

; the boot report in rbuf -> PS/2 packet -> handler
report:
        mov     al, [rbuf]
        and     al, 7                   ; left, right, middle: as PS/2
        mov     si, [mcur]
        mov     [mbtn+si], al
        xor     si, si                  ; the buttons of all mice together
        xor     al, al
.or:    or      al, [mbtn+si]
        inc     si
        cmp     si, MICE
        jb      .or
        mov     [ps_buttons], al
        cmp     byte [ps_enabled], 0
        je      .out
        mov     eax, [ps_handler]
        or      eax, eax
        jz      .out
        mov     al, [ps_buttons]
        or      al, 08h                 ; always 1
        mov     ah, [rbuf+1]            ; X
        test    ah, 80h
        jz      .xp
        or      al, 10h
.xp:    mov     dl, [rbuf+2]            ; Y: USB down, PS/2 up
        neg     dl
        jz      .yp                     ; (0 stays 0, no sign)
        test    dl, 80h
        jz      .yp
        or      al, 20h
.yp:    cmp     byte [rbuf+2], 80h      ; -128 down: +128 up, overflows
        jne     .ok
        mov     dl, 7Fh
        and     al, ~20h
.ok:
%ifdef E9LOG
        push    ax
        mov     al, 'P'
        out     0E9h, al
        pop     ax
        call    e9hex
        push    ax
        mov     al, 10
        out     0E9h, al
        pop     ax
%endif
        mov     [pkt], al
        mov     [pkt+1], ah
        mov     [pkt+2], dl
        ; through the keyboard controller: "write mouse output buffer"
        ; (D3h) puts each byte there as if a PS/2 mouse had sent it, with
        ; IRQ 12.  Drivers that read port 60h in their own IRQ 12 handler
        ; (Logitech) get it like the BIOS callback of the others.
        cmp     byte [kbc_ok], 0
        je      .soft
        mov     si, pkt
        mov     cx, 3
.k:     lodsb
        call    kbc_inject
        loop    .k
        ret
.soft:  ; no 8042: through INT 74h, as IRQ 12 would come
        mov     byte [pending], 1
        int     74h
        push    cs
        pop     ds
.out:   ret

; byte AL into the 8042's mouse output buffer; waits until it was read
kbc_inject:
        push    cx
        mov     ah, al
        mov     cx, 0FFFFh
.w1:    in      al, 64h                 ; input and output buffer empty
        test    al, 3
        loopnz  .w1
        cli
        mov     al, 0D3h
        out     64h, al
        mov     cx, 0FFFFh
.w2:    in      al, 64h
        test    al, 2
        loopnz  .w2
        mov     al, ah
        out     60h, al
        sti
        mov     cx, 0FFFFh              ; IRQ 12 takes it
.w3:    in      al, 64h
        test    al, 1
        loopnz  .w3
        pop     cx
        ret

; INT 74h (IRQ 12): with the 8042 the bytes from port 60h, three to a
; packet; else our software packet, or the BIOS's (a real PS/2 mouse)
int74:
        cmp     byte [cs:kbc_ok], 0
        je      .soft
        push    ax
        in      al, 64h
        and     al, 21h                 ; mouse data there?
        cmp     al, 21h
        jne     .eoi1
        pushad
        push    ds
        push    es
        push    cs
        pop     ds
        in      al, 60h
        movzx   bx, byte [icnt]
        or      bx, bx
        jnz     .st
        test    al, 08h                 ; the first byte has bit 3 set
        jz      .done
.st:    mov     [ibuf+bx], al
        inc     bx
        cmp     bx, 3
        jb      .more
        mov     ax, [ibuf]
        mov     [pkt], ax
        mov     al, [ibuf+2]
        mov     [pkt+2], al
        xor     bx, bx
        cmp     byte [ps_enabled], 0
        je      .more
        cmp     dword [ps_handler], 0
        je      .more
        push    bx
        call    deliver
        pop     bx
.more:  mov     [icnt], bl
.done:  pop     es
        pop     ds
        popad
.eoi1:  mov     al, 20h
        out     0A0h, al
        out     20h, al
        pop     ax
        iret
.soft:  cmp     byte [cs:pending], 0
        jne     .ours
        jmp     far [cs:old_74]
.ours:  mov     byte [cs:pending], 0
        pushad
        push    ds
        push    es
        push    cs
        pop     ds
        call    deliver
        pop     es
        pop     ds
        popad
        iret

; the packet in pkt to the handler (size 1: one call per byte)
deliver:
%ifdef E9LOG
        push    ax
        mov     al, 'D'
        out     0E9h, al
        mov     al, 10
        out     0E9h, al
        pop     ax
%endif
        cmp     byte [ps_size], 1
        je      .bytes
        movzx   ax, byte [pkt]          ; one call: status, X, Y, 0
        push    ax
        mov     al, [pkt+1]
        push    ax
        mov     al, [pkt+2]
        push    ax
        push    word 0
        call    far [cs:ps_handler]
        add     sp, 8
        push    cs
        pop     ds
        ret
.bytes: ; packets of 1 byte: one call per byte, the byte as status
        mov     si, pkt
        mov     cx, 3
.b:     push    cx
        push    si
        movzx   ax, byte [si]
        push    ax
        push    word 0
        push    word 0
        push    word 0
        call    far [cs:ps_handler]
        add     sp, 8
        push    cs
        pop     ds
        pop     si
        pop     cx
        inc     si
        loop    .b
        ret

;-----------------------------------------------------------------------------
; INT 15h AH=C2h: the PS/2 pointing device services
;-----------------------------------------------------------------------------
int15:
        cmp     ah, 0C2h
        je      .ours
        jmp     far [cs:old_15]
.ours:  push    bp
        mov     bp, sp
        push    ds
        push    cs
        pop     ds
%ifdef E9LOG
        push    ax
        push    ax
        mov     al, 'C'
        out     0E9h, al
        pop     ax
        call    e9hex
        mov     al, bh
        call    e9hex
        mov     al, 10
        out     0E9h, al
        pop     ax
%endif
        cmp     al, 7
        ja      .inval
        push    si
        movzx   si, al
        add     si, si
        mov     si, [c2_tab+si]
        mov     [c2_target], si
        pop     si
        call    [c2_target]
        jc      .fail
        xor     ah, ah
        and     byte [bp+6], 0FEh       ; CF off in the caller's flags
        pop     ds
        pop     bp
        iret
.inval: mov     ah, 1                   ; invalid function
.fail:  or      byte [bp+6], 1
        pop     ds
        pop     bp
        iret

c2_target       dw      0
%ifdef E9LOG
e9hex:  push    ax
        shr     al, 4
        call    .d
        pop     ax
        push    ax
        and     al, 0Fh
        call    .d
        pop     ax
        ret
.d:     add     al, '0'
        cmp     al, '9'
        jbe     .o
        add     al, 7
.o:     out     0E9h, al
        ret
%endif
c2_tab          dw      c2_enable, c2_reset, c2_rate, c2_res
                dw      c2_type, c2_init, c2_ext, c2_handler

c2_enable:                              ; BH = 0 disable, 1 enable
        cmp     bh, 1
        ja      .inv
        jb      .off
        cmp     dword [ps_handler], 0
        je      .noh
        mov     byte [ps_enabled], 1
        clc
        ret
.off:   mov     byte [ps_enabled], 0
        clc
        ret
.noh:   mov     ah, 5                   ; no far call installed
        stc
        ret
.inv:   mov     ah, 1
        stc
        ret

c2_reset:
        call    defaults
        mov     bx, 00AAh               ; BH = device ID 0, BL = AAh
        clc
        ret

c2_rate:                                ; BH = 0..6
        cmp     bh, 6
        ja      c2_badin
        push    bx
        movzx   bx, bh
        mov     bl, [rates+bx]
        mov     [ps_rate], bl
        pop     bx
        clc
        ret

c2_res:                                 ; BH = 0..3
        cmp     bh, 3
        ja      c2_badin
        mov     [ps_res], bh
        clc
        ret

c2_type:
        xor     bh, bh                  ; standard PS/2 mouse
        clc
        ret

c2_init:                                ; BH = packet size 1..8
        or      bh, bh
        jz      c2_badin
        cmp     bh, 8
        ja      c2_badin
        call    defaults
        mov     [ps_size], bh
        clc
        ret

c2_ext:
        or      bh, bh
        jnz     .scale
        mov     bl, [ps_buttons]        ; status: bit 0 right, 1 middle, 2 left
        xor     cl, cl
        test    bl, 1
        jz      .nl
        or      cl, 4
.nl:    test    bl, 2
        jz      .nr
        or      cl, 1
.nr:    test    bl, 4
        jz      .nm
        or      cl, 2
.nm:    mov     bl, cl
        cmp     byte [ps_scale], 0
        je      .s1
        or      bl, 10h
.s1:    cmp     byte [ps_enabled], 0
        je      .en
        or      bl, 20h
.en:    mov     cl, [ps_res]
        mov     dl, [ps_rate]
        clc
        ret
.scale: cmp     bh, 2
        ja      c2_badin
        dec     bh                      ; 1: 1:1, 2: 2:1
        mov     [ps_scale], bh
        inc     bh
        clc
        ret

c2_handler:                             ; ES:BX = handler (0:0 none)
        mov     [ps_handler], bx
        mov     [ps_handler+2], es
        clc
        ret

c2_badin:
        mov     ah, 2                   ; invalid input
        stc
        ret

defaults:
        mov     byte [ps_enabled], 0
        mov     byte [ps_rate], 100
        mov     byte [ps_res], 2
        mov     byte [ps_scale], 0
        ret

resident_end:

;=============================================================================
; initialisation
;=============================================================================
opt_verbose     db      0
frec            times 16 db 0

init:
        push    cs
        pop     ds
        les     bx, [req_ptr]
        mov     word [es:bx+RQ_STATUS], 0100h
        les     si, [es:bx+RQ_ARGS]     ; options: /V
.arg:   mov     al, [es:si]
        inc     si
        cmp     al, 13
        je      .args
        cmp     al, 10
        je      .args
        or      al, al
        jz      .args
        cmp     al, '/'
        jne     .arg
        mov     al, [es:si]
        and     al, 0DFh
        cmp     al, 'V'
        jne     .arg
        mov     byte [opt_verbose], 1
        jmp     .arg
.args:  mov     dx, msg_banner
        call    puts

        call    find_eusbaspi
        jc      .noeusb
        xor     ah, ah
        call    far [api]
        mov     ch, cl                  ; CH = functions
        xor     bl, bl
.f:     cmp     bl, ch
        jae     .done
        push    bx
        push    cx
        push    cs
        pop     es
        mov     di, frec
        mov     ah, 1
        call    far [api]
        pop     cx
        pop     bx
        cmp     byte [frec+2], 1        ; boot interface
        jne     .nf
        cmp     byte [frec+3], 2        ; mouse
        jne     .nf
        cmp     byte [mcount], MICE
        jae     .nf
        push    bx
        push    cx
        mov     ah, 3                   ; start its pipe
        call    far [api]
        push    ax
        mov     dx, msg_mouse
        call    puts
        mov     ax, [frec+8]
        call    puthex16
        mov     dl, ':'
        call    putc
        mov     ax, [frec+10]
        call    puthex16
        mov     dx, msg_on
        call    puts
        movzx   bx, byte [frec+13]
        cmp     bl, 4
        jbe     .hct
        xor     bl, bl
.hct:   add     bx, bx
        mov     dx, [hc_names+bx]
        call    puts
        pop     ax
        pop     cx
        pop     bx
        mov     dx, msg_nopipe
        or      al, al
        jnz     .say
        movzx   si, byte [mcount]
        mov     [mice+si], bl
        inc     byte [mcount]
        mov     dx, crlf
.say:   push    bx
        push    cx
        call    puts
        pop     cx
        pop     bx
.nf:    inc     bl
        jmp     .f
.done:  cmp     byte [mcount], 0
        je      .nomouse
        mov     dx, msg_int15
        call    puts

        mov     ax, 3508h               ; hook the timer and INT 15h
        int     21h
        mov     [old_08], bx
        mov     [old_08+2], es
        mov     ax, 3515h
        int     21h
        mov     [old_15], bx
        mov     [old_15+2], es
        call    kbc_probe
        mov     ax, 3574h
        int     21h
        mov     [old_74], bx
        mov     [old_74+2], es
        mov     ax, 2574h
        mov     dx, int74
        int     21h
        mov     ax, 2508h
        mov     dx, int08
        int     21h
        mov     ax, 2515h
        mov     dx, int15
        int     21h
        les     bx, [req_ptr]
        mov     word [es:bx+RQ_END], resident_end
        mov     [es:bx+RQ_END+2], cs
        ret

.noeusb:
        mov     dx, msg_noeusb
        jmp     .fail
.nomouse:
        mov     dx, msg_nomouse
        jmp     .fail
.nopipe:
        mov     dx, msg_nopipe
.fail:  call    puts
        mov     dx, msg_notinst
        call    puts
        les     bx, [req_ptr]
        mov     word [es:bx+RQ_END], 0
        mov     [es:bx+RQ_END+2], cs
        mov     word [es:bx+RQ_STATUS], 810Ch ; error, general failure
        ret

; AMIS: "GeosOne " "EUSBASPI" -> api; CF if not there
find_eusbaspi:
        xor     ax, ax
        mov     es, ax
        mov     ax, [es:2Dh*4]
        or      ax, [es:2Dh*4+2]
        jz      .no
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
        mov     di, amis_sig
        mov     cx, 16
        repe    cmpsb
        pop     ds
        je      .found
.n:     inc     bh
        jnz     .scan
.no:    stc
        ret
.found: mov     ah, bh
        mov     al, 1
        int     2Dh
        mov     [api], bx
        mov     [api+2], dx
        clc
        ret

; does the 8042 take D3h (a byte into its mouse output buffer)?  Then
; the mouse port is switched on with IRQ 12 -> kbc_ok
kbc_probe:
        pushf
        cli
        in      al, 0A1h                ; IRQ 12 masked while we ask
        mov     [pic2], al
        or      al, 10h
        out     0A1h, al
        call    kbc_wi
        mov     al, 0A8h                ; mouse port on
        out     64h, al
        call    kbc_wi
        mov     al, 20h                 ; command byte
        out     64h, al
        call    kbc_wo
        jc      .no
        in      al, 60h
        mov     [kcmd], al
        mov     cx, 16                  ; nothing left in the buffer
.dr:    in      al, 64h
        test    al, 1
        jz      .empty
        in      al, 60h
        loop    .dr
.empty: call    kbc_wi
        mov     al, 0D3h
        out     64h, al
        call    kbc_wi
        mov     al, 5Ah
        out     60h, al
        call    kbc_wo
        jc      .no
        in      al, 64h
        mov     ah, al
        in      al, 60h
        cmp     al, 5Ah
        jne     .no
        test    ah, 20h                 ; it came as mouse data
        jz      .no
        call    kbc_wi
        mov     al, 60h                 ; IRQ 12 on, mouse clock on
        out     64h, al
        call    kbc_wi
        mov     al, [kcmd]
        or      al, 02h
        and     al, ~20h
        out     60h, al
        mov     byte [kbc_ok], 1
        mov     al, [pic2]              ; IRQ 12 and the cascade unmasked
        and     al, ~10h
        out     0A1h, al
        in      al, 21h
        and     al, ~04h
        out     21h, al
        popf
        ret
.no:    mov     al, [pic2]
        out     0A1h, al
        popf
        ret

kbc_wi: push    cx                      ; input buffer empty
        mov     cx, 0FFFFh
.w:     in      al, 64h
        test    al, 2
        loopnz  .w
        pop     cx
        ret
kbc_wo: push    cx                      ; output buffer full (CF: timeout)
        mov     cx, 0FFFFh
.w:     in      al, 64h
        test    al, 1
        jnz     .ok
        loop    .w
        pop     cx
        stc
        ret
.ok:    pop     cx
        clc
        ret

pic2            db      0
kcmd            db      0

puts:   mov     ah, 9
        int     21h
        ret
putc:   mov     ah, 2
        int     21h
        ret
puthex16:
        push    cx
        mov     cx, 4
.d:     rol     ax, 4
        push    ax
        and     al, 0Fh
        add     al, '0'
        cmp     al, '9'
        jbe     .o
        add     al, 7
.o:     mov     dl, al
        call    putc
        pop     ax
        loop    .d
        pop     cx
        ret

amis_sig        db      'GeosOne EUSBASPI'
hc_names        dw      hc_unk, hc_ohci, hc_ehci, hc_uhci, hc_xhci
hc_unk          db      'USB$'
hc_ohci         db      'OHCI$'
hc_uhci         db      'UHCI$'
hc_ehci         db      'EHCI$'
hc_xhci         db      'xHCI$'
msg_banner      db      'USBHID 0.1 - USB mouse through EUSBASPI', 13, 10, '$'
msg_mouse       db      '  USB mouse $'
msg_on          db      ' on $'
msg_int15       db      '  as the PS/2 mouse of the BIOS (INT 15h)', 13, 10, '$'
crlf            db      13, 10, '$'
msg_noeusb      db      'EUSBASPI.SYS with HID devices not found', 13, 10, '$'
msg_nomouse     db      'No USB mouse to use', 13, 10, '$'
msg_nopipe      db      ': controller not supported yet', 13, 10, '$'
msg_notinst     db      'USBHID not installed', 13, 10, '$'
