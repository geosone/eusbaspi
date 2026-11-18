;=============================================================================
; hid.inc - USB devices for other drivers (USBHID.SYS): resident part
;
; EUSBASPI keeps the HID interfaces it finds (mouse, keyboard, joystick ...)
; configured and lends them to client drivers through an AMIS interface
; (INT 2Dh, "GeosOne " "EUSBASPI").  The private entry point (AMIS function
; 01h) is called far with AH = function:
;
;   00h  info:    -> AL = 0, AH = API version, CL = number of HID functions
;   01h  function BL -> ES:DI (HIDF_SIZE bytes, see below); AL = 0 or 1
;   02h  control transfer on the device of function BL: ES:DI -> setup
;        packet (8 bytes) and a far pointer to the data (dword after it);
;        -> AL = 0 done (CX bytes), 1 bad function, 2 busy, 3 stall,
;        4 error.  Not from interrupt handlers.
;   03h  start the interrupt IN pipe of function BL -> AL = 0 / error
;   04h  poll the pipe of function BL (interrupt handlers may call it):
;        -> AL = 0 nothing new, 1 a report at ES:DI (CX bytes, at most the
;        endpoint's packet size), else error
;   05h  stop the pipe of function BL
;
; A function's record (01h):
;   +0 device  +1 interface  +2 subclass  +3 protocol (1 keyboard, 2 mouse)
;   +4 endpoint  +5 interval  +6 packet size (word)  +8 vendor  +10 product
;   +12 speed (0 full, 1 low, 2 high, 3 super)  +13 controller type
;=============================================================================

HID_MAX         equ     8               ; HID interfaces (functions)
HID_API         equ     1
HIDF_SIZE       equ     14
AMIS_VERSION    equ     0100h

hid_count       db      0
hidf_dev        times HID_MAX db 0      ; USB device entry
hidf_iface      times HID_MAX db 0
hidf_sub        times HID_MAX db 0
hidf_proto      times HID_MAX db 0
hidf_ep         times HID_MAX db 0      ; interrupt IN endpoint number
hidf_ival       times HID_MAX db 0
hidf_mps        times HID_MAX dw 0
hidf_vid        times HID_MAX dw 0
hidf_pid        times HID_MAX dw 0
hidf_tog        times HID_MAX db 0      ; data toggle of the pipe
hidf_run        times HID_MAX db 0      ; pipe started
hidf_lnk        times HID_MAX db 0      ; its QH / ED is in the schedule
hidf_ring       times HID_MAX db 0      ; xHCI: transfer ring (1-3), 0 none
hidf_dci        times HID_MAX db 0      ;   endpoint (device context index)
hidf_xq         times HID_MAX db 0      ;   0 idle, 1 TRB queued, 2 event
hidf_xcc        times HID_MAX db 0      ;   its completion code
hidf_xres       times HID_MAX dw 0      ;   and the bytes not moved

;-----------------------------------------------------------------------------
; AMIS handler (with the header of the IBM interrupt sharing protocol)
;-----------------------------------------------------------------------------
amis_int2d:
        jmp     short .go
.old    dd      0
                dw      424Bh           ; "KB"
                db      0
                jmp     short .hw       ; hardware reset entry
                times 7 db 0
.hw:    retf
.go:    cmp     ah, [cs:amis_mux]
        je      .ours
        jmp     far [cs:.old]
.ours:  cmp     al, 0
        je      .check
        cmp     al, 1
        je      .entry
        xor     al, al                  ; not implemented
        iret
.check: mov     al, 0FFh
        mov     cx, AMIS_VERSION
        mov     dx, cs
        mov     di, amis_sig
        iret
.entry: mov     al, 0FFh
        mov     dx, cs
        mov     bx, hid_api
        iret

amis_mux        db      0
amis_iret:      iret
amis_sig        db      'GeosOne EUSBASPI'
                db      'USB HID devices for client drivers', 0

;-----------------------------------------------------------------------------
; private entry point
;-----------------------------------------------------------------------------
hid_api:
        push    ds
        push    cs
        pop     ds
        cmp     ah, 0
        je      .info
        cmp     bl, [hid_count]
        jae     .bad
        cmp     ah, 1
        je      .finfo
        cmp     ah, 2
        je      .ctl
        cmp     ah, 3
        je      .start
        cmp     ah, 4
        je      .poll
        cmp     ah, 5
        je      .stop
        mov     al, 1                   ; unknown function
        pop     ds
        retf
.bad:   mov     al, 1
        pop     ds
        retf
.info:  xor     al, al
        mov     ah, HID_API
        mov     cl, [hid_count]
        pop     ds
        retf

.finfo: push    bx
        push    di
        movzx   bx, bl
        mov     al, [hidf_dev+bx]
        stosb
        mov     al, [hidf_iface+bx]
        stosb
        mov     al, [hidf_sub+bx]
        stosb
        mov     al, [hidf_proto+bx]
        stosb
        mov     al, [hidf_ep+bx]
        stosb
        mov     al, [hidf_ival+bx]
        stosb
        push    bx
        add     bx, bx
        mov     ax, [hidf_mps+bx]
        stosw
        mov     ax, [hidf_vid+bx]
        stosw
        mov     ax, [hidf_pid+bx]
        stosw
        pop     bx
        movzx   bx, byte [hidf_dev+bx]
        mov     al, [usbd_speed+bx]
        stosb
        mov     al, [usbd_hc+bx]
        movzx   bx, al
        mov     al, [hc_type+bx]
        stosb
        pop     di
        pop     bx
        xor     al, al
        pop     ds
        retf

.ctl:   ; control transfer through the bounce buffer (not reentrant: in_aspi)
        cmp     byte [in_aspi], 0
        je      .free
        mov     al, 2
        pop     ds
        retf
.free:  inc     byte [in_aspi]
        push    bx
        push    dx
        push    si
        push    di
        push    es
        mov     [hid_req], di
        mov     [hid_req+2], es
        movzx   bx, bl
        mov     bl, [hidf_dev+bx]
        mov     cx, [es:di+6]           ; wLength
        cmp     cx, 4096
        jbe     .len
        mov     cx, 4096
.len:   mov     [hid_len], cx
        test    byte [es:di], 80h       ; OUT: data into the bounce buffer
        jnz     .noout
        jcxz    .noout
        push    ds
        lds     si, [es:di+8]
        push    cs
        pop     es
        mov     di, [cs:bounce_ptr]
        rep     movsb
        pop     ds
        les     di, [hid_req]
.noout: mov     ax, [es:di]             ; bmRequestType, bRequest
        mov     dx, [es:di+2]           ; wValue
        mov     cx, [es:di+4]           ; wIndex
        mov     si, [hid_len]           ; wLength
        call    usb_control
        cmp     al, US_OK
        je      .ok
        mov     ah, al
        mov     al, 4
        cmp     ah, US_STALL
        jne     .ret
        mov     al, 3
        jmp     .ret
.ok:    mov     cx, [ux_actual]
        cmp     cx, [hid_len]
        jbe     .act
        mov     cx, [hid_len]
.act:   les     di, [hid_req]
        test    byte [es:di], 80h       ; IN: data out of the bounce buffer
        jz      .done
        jcxz    .done
        push    cx
        les     di, [es:di+8]
        mov     si, [bounce_ptr]
        rep     movsb
        pop     cx
.done:  xor     al, al
.ret:   pop     es
        pop     di
        pop     si
        pop     dx
        pop     bx
        dec     byte [in_aspi]
        pop     ds
        retf

.start: call    hid_pipe_start
        pop     ds
        retf
.poll:  call    hid_pipe_poll
        pop     ds
        retf
.stop:  call    hid_pipe_stop
        pop     ds
        retf

hid_req         dd      0
hid_len         dw      0

;-----------------------------------------------------------------------------
; Interrupt IN pipes.  The HID page (hid_page, from ud_base) holds per
; function 256 bytes: +0 queue head / ED, +64 TD, +96 OHCI tail TD,
; +128 the data (up to 128 bytes); from +2048 per controller 32 bytes
; (UHCI: head and tail queue head of its own frame list).
;-----------------------------------------------------------------------------
HP_QH           equ     0
HP_TD           equ     64
HP_DATA         equ     128
HP_HC           equ     2048
HID_MAXPKT      equ     128

hid_page        dw      0               ; 0: no pipes
phys_hid        dd      0
uhc_fl          times USB_MAX_HC dd 0   ; UHCI: own frame list (physical)
phys_uqh        dd      0               ; our transfer queue head (UHCI)

; function BX -> DI = its 256 bytes, EAX = their physical address
hid_pp:
        mov     di, bx
        shl     di, 8
        movzx   eax, di
        add     eax, [phys_hid]         ; the page is locked on its own
        add     di, [hid_page]
        add     di, [ud_base]
        ret

; controller index of function BX -> SI (and AL = its type)
hid_hc: movzx   si, byte [hidf_dev+bx]
        mov     al, [usbd_hc+si]
        movzx   si, al
        mov     al, [hc_type+si]
        ret

; controller SI -> DI = its 64 bytes in the HID page, EAX = physical
hid_hcp:
        mov     di, si
        shl     di, 6
        add     di, HP_HC
        movzx   eax, di
        add     eax, [phys_hid]
        add     di, [hid_page]
        add     di, [ud_base]
        ret

hid_pipe_start:
        cmp     word [hid_page], 0
        je      .nosup
        push    bx
        push    si
        push    di
        movzx   bx, bl
        mov     al, 0
        cmp     byte [hidf_run+bx], 0
        jne     .out
        call    hid_hc
        cmp     al, HC_OHCI
        jne     .notohci
        call    ohci_pipe_start
        jmp     .out
.notohci:
        cmp     al, HC_EHCI
        jne     .notehci
        call    ehci_pipe_start
        jmp     .out
.notehci:
        cmp     al, HC_XHCI
        jne     .notxhci
        call    xhci_pipe_start
        jmp     .out
.notxhci:
        cmp     al, HC_UHCI
        jne     .nosup2
        call    uhci_pipe_start
.out:   pop     di
        pop     si
        pop     bx
        ret
.nosup2:
        pop     di
        pop     si
        pop     bx
.nosup: mov     al, 5                   ; not on this controller (yet)
        ret

hid_pipe_poll:
        push    bx
        push    si
        movzx   bx, bl
        mov     al, 5
        cmp     byte [hidf_run+bx], 0
        je      .out
        call    hid_hc
        cmp     al, HC_OHCI
        jne     .nooh
        call    ohci_pipe_poll
        jmp     .out
.nooh:  cmp     al, HC_EHCI
        jne     .noeh
        call    ehci_pipe_poll
        jmp     .out
.noeh:  cmp     al, HC_XHCI
        jne     .noxh
        call    xhci_pipe_poll
        jmp     .out
.noxh:  cmp     al, HC_UHCI
        jne     .no
        call    uhci_pipe_poll
        jmp     .out
.no:    mov     al, 5
.out:   pop     si
        pop     bx
        ret

hid_pipe_stop:
        push    bx
        push    si
        push    di
        movzx   bx, bl
        cmp     byte [hidf_run+bx], 0
        je      .out
        call    hid_hc
        push    ax
        call    hid_pp
        pop     ax
        cmp     al, HC_OHCI
        jne     .notoh
        or      dword [di+HP_QH], 1 << 14 ; OHCI: the ED is skipped
.notoh: cmp     al, HC_EHCI
        jne     .noteh
        mov     dword [di+HP_TD+8], 0   ; EHCI: the qTD goes inactive
.noteh: cmp     al, HC_UHCI
        jne     .noth
        mov     dword [di+HP_QH+4], 1   ; UHCI: the queue head goes idle
.noth:  mov     byte [hidf_run+bx], 0
.out:   xor     al, al
        pop     di
        pop     si
        pop     bx
        ret

;-----------------------------------------------------------------------------
; UHCI: own frame list per controller with HID functions; every entry
; points to its head queue head -> the pipes' queue heads -> its tail queue
; head, whose link is our transfer queue head while this controller runs
; a transfer (uhci_set_fl) and "terminate" otherwise.
;-----------------------------------------------------------------------------
; TD of function BX at DI (pipe area) armed with its toggle
uhci_pipe_arm:
        push    eax
        push    edx
        push    si
        mov     dword [di+HP_TD], 1     ; no next TD
        mov     edx, UT_CERR3 | UT_ACTIVE
        movzx   si, byte [hidf_dev+bx]
        cmp     byte [usbd_speed+si], 1
        jne     .fs
        or      edx, UT_LS
.fs:    mov     [di+HP_TD+4], edx
        movzx   eax, byte [usbd_addr+si]
        shl     eax, 8
        movzx   edx, byte [hidf_ep+bx]
        shl     edx, 15
        or      eax, edx
        or      eax, UPID_IN
        test    byte [hidf_tog+bx], 1
        jz      .t0
        or      eax, 1 << 19
.t0:    mov     si, bx
        add     si, si
        movzx   edx, word [hidf_mps+si]
        cmp     dx, HID_MAXPKT
        jbe     .len
        mov     dx, HID_MAXPKT
.len:   dec     edx
        shl     edx, 21
        or      eax, edx
        mov     [di+HP_TD+8], eax
        pop     si
        pop     edx
        pop     eax
        ret

uhci_pipe_start:
        mov     al, 5
        shl     si, 2
        cmp     dword [uhc_fl+si], 0    ; no own frame list (no memory)
        je      .ret
        shr     si, 2
        push    si
        call    hid_pp
        mov     edx, eax
        add     eax, HP_DATA
        mov     [di+HP_TD+12], eax
        call    uhci_pipe_arm
        mov     eax, edx
        add     eax, HP_TD
        mov     [di+HP_QH+4], eax       ; element: the TD
        pop     si
        cmp     byte [hidf_lnk+bx], 0
        jne     .run
        push    di
        call    hid_hcp                 ; head QH of the controller
        pop     si                      ; SI = pipe area
        mov     eax, [di]               ; pipe QH -> what the head linked
        mov     [si+HP_QH], eax
        or      edx, 2                  ; head -> pipe QH
        mov     [di], edx
        mov     byte [hidf_lnk+bx], 1
.run:   mov     byte [hidf_run+bx], 1
        xor     al, al
.ret:   ret

; ES:DI = client buffer -> AL = 0 nothing, 1 report (CX bytes), 3 stalled
; (one caller at a time: the client's poll is not reentrant)
uhci_pipe_poll:
        mov     [hid_buf], di
        mov     [hid_buf+2], es
        push    di
        push    es
        push    edx
        call    hid_pp                  ; DI = pipe area
        mov     edx, [di+HP_TD+4]
        test    edx, UT_ACTIVE
        jz      .done
        xor     al, al
        jmp     .out
.done:  test    edx, UT_STALLED
        jz      .nost
        mov     byte [hidf_run+bx], 0   ; halted: the client restarts it
        mov     al, 3
        jmp     .out
.nost:  test    edx, UT_ERRORS
        jz      .data
        call    uhci_pipe_rearm         ; transient error: again
        xor     al, al
        jmp     .out
.data:  inc     dx                      ; actual length - 1 in bits 10..0
        and     dx, 7FFh
        mov     cx, dx
        cmp     cx, HID_MAXPKT
        jbe     .cp
        mov     cx, HID_MAXPKT
.cp:    push    si
        push    cx
        lea     si, [di+HP_DATA]
        les     di, [hid_buf]
        cld
        rep     movsb
        pop     cx
        pop     si
        xor     byte [hidf_tog+bx], 1
        call    hid_pp
        call    uhci_pipe_rearm
        mov     al, 1
.out:   pop     edx
        pop     es
        pop     di
        ret

hid_buf         dd      0

; arm the TD again and hang it into the pipe's queue head (DI = pipe area)
uhci_pipe_rearm:
        push    eax
        call    uhci_pipe_arm
        call    hid_pp
        add     eax, HP_TD
        mov     [di+HP_QH+4], eax
        pop     eax
        ret

;-----------------------------------------------------------------------------
; OHCI: the 32 entries of the controller's interrupt table (HCCA) point to
; its head ED (skipped), which links the pipes' EDs.  Each pipe has two TDs
; at +64 and +96: one queued, the other the empty tail; the controller
; keeps the data toggle in the ED.
;-----------------------------------------------------------------------------
HP_TD2          equ     96
OED_SKIP        equ     1 << 14

OTD_FLAGS       equ     0F0000000h | (7 << 21) | (2 << 19) | (1 << 18)
                ; not accessed, no interrupt, IN, short packets allowed

; the TD at offset AX of pipe area DI (physical EDX) for one IN packet of
; function BX into the data, next TD at offset CX
ohci_fill_td:
        push    eax
        push    ecx
        push    si
        movzx   ecx, cx
        add     ecx, edx
        mov     [oh_next], ecx
        mov     si, ax
        mov     eax, edx
        add     eax, HP_DATA
        mov     [oh_buf], eax
        push    bx
        add     bx, bx
        movzx   ecx, word [hidf_mps+bx]
        pop     bx
        cmp     cx, HID_MAXPKT
        jbe     .l
        mov     cx, HID_MAXPKT
.l:     dec     ecx
        add     ecx, eax
        push    di
        add     di, si
        mov     dword [di], OTD_FLAGS
        mov     [di+4], eax             ; current buffer pointer
        mov     eax, [oh_next]
        mov     [di+8], eax             ; next TD
        mov     [di+12], ecx            ; buffer end
        pop     di
        pop     si
        pop     ecx
        pop     eax
        ret

ohci_pipe_start:
        push    dx
        call    hid_pp
        mov     edx, eax
        push    si
        movzx   si, byte [hidf_dev+bx]  ; ED: address, endpoint, IN, speed,
        movzx   eax, byte [usbd_addr+si] ; packet size
        movzx   ecx, byte [hidf_ep+bx]
        shl     ecx, 7
        or      eax, ecx
        or      eax, 2 << 11
        cmp     byte [usbd_speed+si], 1
        jne     .fs
        or      eax, 1 << 13
.fs:    mov     si, bx
        add     si, si
        movzx   ecx, word [hidf_mps+si]
        shl     ecx, 16
        or      eax, ecx
        mov     [di+HP_QH], eax
        mov     ax, HP_TD               ; TD 1 queued, TD 2 the tail
        mov     cx, HP_TD2
        call    ohci_fill_td
        mov     eax, edx
        add     eax, HP_TD2
        mov     [di+HP_QH+4], eax       ; tail
        mov     eax, edx
        add     eax, HP_TD
        mov     [di+HP_QH+8], eax       ; head (toggle DATA0)
        pop     si
        cmp     byte [hidf_lnk+bx], 0
        jne     .run
        push    di
        call    hid_hcp                 ; DI = the controller's head ED
        mov     eax, [di+12]
        pop     si                      ; SI = pipe area
        mov     [si+HP_QH+12], eax      ; pipe ED -> what the head linked
        mov     [di+12], edx            ; head -> pipe ED
        mov     byte [hidf_lnk+bx], 1
.run:   mov     byte [hidf_run+bx], 1
        xor     al, al
        pop     dx
        ret

; ES:DI = client buffer -> AL = 0 nothing, 1 report (CX bytes), 3 stalled
ohci_pipe_poll:
        mov     [hid_buf], di
        mov     [hid_buf+2], es
        push    di
        push    es
        push    edx
        push    si
        call    hid_pp
        mov     edx, eax
        mov     eax, [di+HP_QH+4]       ; tail -> the other TD is the one
        sub     eax, edx                ; that ran
        mov     [oh_tail], ax
        mov     cx, HP_TD + HP_TD2
        sub     cx, ax
        mov     [oh_done], cx
        mov     eax, [di+HP_QH+8]
        test    al, 1
        jnz     .halt
        and     al, 0F0h
        sub     eax, edx
        cmp     ax, [oh_tail]
        je      .done
        xor     al, al                  ; still queued
        jmp     .out
.halt:  call    oh_cc
        cmp     al, 4                   ; stall: the client restarts it
        jne     .clr
        mov     byte [hidf_run+bx], 0
        mov     al, 3
        jmp     .out
.clr:   mov     eax, [di+HP_QH+8]       ; halt off, the toggle stays
        and     eax, 2
        movzx   ecx, word [oh_tail]
        add     ecx, edx
        or      eax, ecx
        mov     [di+HP_QH+8], eax
        call    ohci_rearm
        xor     al, al
        jmp     .out
.done:  call    oh_cc
        or      al, al
        jz      .ok
        cmp     al, 9                   ; data underrun: a short report
        je      .ok
        call    ohci_rearm              ; other errors: again
        xor     al, al
        jmp     .out
.ok:    push    di
        add     di, [oh_done]
        mov     eax, [di+4]             ; bytes: buffer pointer moved
        or      eax, eax
        jnz     .part
        mov     eax, [di+12]            ; all: up to the end
        inc     eax
.part:  pop     di
        sub     eax, edx
        sub     eax, HP_DATA
        mov     cx, ax
        cmp     cx, HID_MAXPKT
        jbe     .cp
        mov     cx, HID_MAXPKT
.cp:    push    cx
        lea     si, [di+HP_DATA]
        les     di, [hid_buf]
        cld
        rep     movsb
        pop     cx
        call    hid_pp
        mov     edx, eax
        call    ohci_rearm
        mov     al, 1
.out:   pop     si
        pop     edx
        pop     es
        pop     di
        ret

; the tail TD becomes the queued one, the TD that ran the new tail
ohci_rearm:
        push    eax
        push    cx
        mov     ax, [oh_tail]
        mov     cx, [oh_done]
        call    ohci_fill_td
        movzx   eax, word [oh_done]
        add     eax, edx
        mov     [di+HP_QH+4], eax       ; tail
        pop     cx
        pop     eax
        ret

; condition code of the TD that ran -> AL
oh_cc:  push    di
        add     di, [oh_done]
        mov     eax, [di]
        pop     di
        shr     eax, 28
        ret

oh_next         dd      0
oh_buf          dd      0
oh_tail         dw      0
oh_done         dw      0

;-----------------------------------------------------------------------------
; EHCI: own periodic frame list per controller with HID functions; every
; entry points to its head QH (never active), which links the pipes' QHs.
; A pipe QH has one qTD; the controller keeps the data toggle in the QH.
; Full / low speed devices behind a high speed hub: split transactions,
; start in micro-frame 0, complete in 2..4.
;-----------------------------------------------------------------------------
eh_ok           db      0               ; EHCI periodic lists (bit per HC)
                db      0
eh_rem          dw      0

; the qTD of pipe area DI (physical EDX) armed for one IN packet of BX
ehci_arm:
        push    eax
        push    si
        mov     dword [di+HP_TD], 1     ; next, alternate next: none
        mov     dword [di+HP_TD+4], 1
        mov     eax, edx
        add     eax, HP_DATA
        mov     [di+HP_TD+12], eax      ; buffer page 0
        mov     si, bx
        add     si, si
        movzx   eax, word [hidf_mps+si]
        cmp     ax, HID_MAXPKT
        jbe     .l
        mov     ax, HID_MAXPKT
.l:     mov     [eh_rem], ax
        shl     eax, 16
        or      eax, QT_PID_IN | QT_CERR3 | QT_ACTIVE
        mov     [di+HP_TD+8], eax
        mov     eax, edx                ; the QH goes on with it
        add     eax, HP_TD
        mov     [di+HP_QH+16], eax      ; (overlay: next qTD)
        pop     si
        pop     eax
        ret

ehci_pipe_start:
        push    si
        mov     al, 5
        mov     cx, si
        mov     si, 1
        shl     si, cl
        test    [eh_ok], si
        jz      .ret
        call    hid_pp
        mov     edx, eax
        push    bx
        movzx   si, byte [hidf_dev+bx]
        movzx   eax, byte [usbd_addr+si]
        movzx   ecx, byte [hidf_ep+bx]
        shl     ecx, 8
        or      eax, ecx
        movzx   ecx, byte [usbd_speed+si] ; 0 full, 1 low, 2 high
        shl     ecx, 12
        or      eax, ecx
        mov     cx, bx
        add     bx, bx
        movzx   ecx, word [hidf_mps+bx]
        shl     ecx, 16
        or      eax, ecx
        mov     [di+HP_QH+4], eax       ; DTC 0: toggle in the QH
        mov     eax, (1 << 30) | 01h    ; one per micro-frame, S-mask 0
        cmp     byte [usbd_speed+si], 2
        je      .hs
        movzx   ecx, byte [usbd_ttport+si]
        shl     ecx, 23
        or      eax, ecx
        movzx   ecx, byte [usbd_tthub+si]
        shl     ecx, 16
        or      eax, ecx
        or      eax, 1Ch << 8           ; C-mask: micro-frames 2, 3, 4
.hs:    mov     [di+HP_QH+8], eax
        pop     bx
        xor     eax, eax
        mov     [di+HP_QH+12], eax      ; overlay: current qTD,
        mov     dword [di+HP_QH+20], 1  ; alternate,
        mov     [di+HP_QH+24], eax      ; token (DATA0, idle)
        mov     [di+HP_QH+28], eax
        mov     [di+HP_QH+32], eax
        mov     [di+HP_QH+36], eax
        mov     [di+HP_QH+40], eax
        mov     [di+HP_QH+44], eax
        cmp     byte [hidf_lnk+bx], 0
        jne     .linked
        mov     dword [di+HP_QH+16], 1
        pop     si
        push    si
        push    di
        call    hid_hcp                 ; DI = the controller's head QH
        pop     si                      ; SI = pipe area
        mov     eax, [di]
        mov     [si+HP_QH], eax         ; pipe QH -> what the head linked
        mov     eax, edx
        or      al, 2
        mov     [di], eax               ; head -> pipe QH
        mov     di, si
        mov     byte [hidf_lnk+bx], 1
.linked:
        call    ehci_arm
        mov     byte [hidf_run+bx], 1
        xor     al, al
.ret:   pop     si
        ret

; ES:DI = client buffer -> AL = 0 nothing, 1 report (CX bytes), 3 stalled
ehci_pipe_poll:
        mov     [hid_buf], di
        mov     [hid_buf+2], es
        push    di
        push    es
        push    edx
        push    si
        call    hid_pp
        mov     edx, eax
        mov     eax, [di+HP_TD+8]
        test    al, QT_ACTIVE
        jz      .ran
        xor     al, al
        jmp     .out
.ran:   test    al, QT_HALTED
        jz      .ok
        test    al, QT_ERRORS
        jnz     .err
        mov     byte [hidf_run+bx], 0   ; stall: the client restarts it
        mov     al, 3
        jmp     .out
.err:   mov     eax, [di+HP_QH+24]      ; halt off, the toggle stays
        and     eax, QT_DT
        mov     [di+HP_QH+24], eax
        call    ehci_arm
        xor     al, al
        jmp     .out
.ok:    shr     eax, 16                 ; bytes left
        and     ax, 7FFFh
        mov     cx, [eh_rem]
        sub     cx, ax
        jae     .cp
        xor     cx, cx
.cp:    push    cx
        lea     si, [di+HP_DATA]
        les     di, [hid_buf]
        cld
        rep     movsb
        pop     cx
        call    hid_pp
        call    ehci_arm
        mov     al, 1
.out:   pop     si
        pop     edx
        pop     es
        pop     di
        ret

;-----------------------------------------------------------------------------
; xHCI: the HID functions' interrupt IN endpoints are configured at
; enumeration (rings 1-3 of their device).  One Normal TRB is queued at a
; time.  The event ring is shared with EUSBASPI's own transfers: xh_event
; puts the events of these endpoints aside (hid_xstash), the poll takes
; the waiting events when EUSBASPI is idle.
;-----------------------------------------------------------------------------
; BX = function, SI = controller
xhci_pipe_start:
        mov     al, 5
        cmp     byte [hidf_ring+bx], 0
        je      .ret
        mov     al, 2                   ; busy: try again
        cmp     byte [in_aspi], 0
        jne     .ret
        inc     byte [in_aspi]
        mov     ax, si
        call    hc_select
        cmp     byte [hidf_xq+bx], 0    ; (still one queued from before)
        jne     .run
        call    xhci_hid_queue
.run:   mov     byte [hidf_run+bx], 1
        dec     byte [in_aspi]
        xor     al, al
.ret:   ret

; queue the TRB of function BX and ring its doorbell (controller selected)
xhci_hid_queue:
        pushad
        call    hid_pp
        add     eax, HP_DATA
        mov     [xh_trb], eax
        mov     dword [xh_trb+4], 0
        mov     di, bx
        add     di, di
        movzx   eax, word [hidf_mps+di]
        cmp     ax, HID_MAXPKT
        jbe     .l
        mov     ax, HID_MAXPKT
.l:     mov     [xh_trb+8], eax
        mov     dword [xh_trb+12], XT_NORMAL | 20h | 4 ; event when done, short
        mov     byte [hidf_xq+bx], 1
        movzx   di, byte [hidf_dev+bx]
        movzx   di, byte [xh_map+di]    ; xHCI device
        mov     dl, [hidf_dci+bx]
        mov     dh, [xh_slot+di]
        mov     ax, di
        shl     ax, 2
        add     al, [hidf_ring+bx]
        push    ax
        call    xh_ringoff
        mov     si, ax
        pop     bx
        add     bx, bx
        add     bx, xh_ring_st
        mov     cl, XH_RING_TRBS
        call    xh_put
        movzx   ax, dh                  ; doorbell of the slot: the endpoint
        shl     ax, 2
        add     ax, [xh_db]
        movzx   ecx, dl
        call    reg_wr
        popad
        ret

; the event in xh_ev: one of a HID pipe?  Then put aside, CF (all kept)
hid_xstash:
        push    eax
        push    bx
        push    di
        call    xh_ev_type
        cmp     al, XE_TRANSFER
        jne     .no
        xor     bx, bx
.f:     cmp     bl, [hid_count]
        jae     .no
        cmp     byte [hidf_xq+bx], 1
        jne     .n
        movzx   di, byte [hidf_dev+bx]
        movzx   di, byte [xh_map+di]
        mov     al, [xh_slot+di]
        cmp     al, [xh_ev+15]
        jne     .n
        mov     al, [xh_ev+14]
        and     al, 1Fh
        cmp     al, [hidf_dci+bx]
        jne     .n
        mov     al, [xh_ev+11]
        mov     [hidf_xcc+bx], al
        mov     eax, [xh_ev+8]
        mov     di, bx
        add     di, di
        mov     [hidf_xres+di], ax
        mov     byte [hidf_xq+bx], 2
        pop     di
        pop     bx
        pop     eax
        stc
        ret
.n:     inc     bx
        jmp     .f
.no:    pop     di
        pop     bx
        pop     eax
        clc
        ret

; ES:DI = client buffer -> AL = 0 nothing, 1 report (CX bytes), 3 stalled
xhci_pipe_poll:
        mov     [hid_buf], di
        mov     [hid_buf+2], es
        push    di
        push    es
        push    si
        cmp     byte [in_aspi], 0       ; EUSBASPI busy: it keeps our events
        jne     .look
        inc     byte [in_aspi]
        push    si
        mov     ax, si
        call    hc_select
        pop     si
.drain: call    xh_peek                 ; the events waiting
        jc      .drained
        call    xh_take
        call    hid_xstash
        jmp     .drain
.drained:
        cmp     byte [hidf_xq+bx], 0    ; one left to queue from before
        jne     .q
        call    xhci_hid_queue
.q:     dec     byte [in_aspi]
.look:  xor     al, al
        cmp     byte [hidf_xq+bx], 2
        jne     .out
        mov     byte [hidf_xq+bx], 0
        mov     al, [hidf_xcc+bx]
        cmp     al, XC_SUCCESS
        je      .ok
        cmp     al, XC_SHORT
        je      .ok
        cmp     al, XC_STALL
        jne     .again
        mov     byte [hidf_run+bx], 0
        mov     al, 3
        jmp     .out
.again: call    xhci_requeue
        xor     al, al
        jmp     .out
.ok:    mov     si, bx
        add     si, si
        mov     cx, [hidf_mps+si]
        cmp     cx, HID_MAXPKT
        jbe     .l
        mov     cx, HID_MAXPKT
.l:     sub     cx, [hidf_xres+si]
        jae     .cp
        xor     cx, cx
.cp:    push    cx
        call    hid_pp
        lea     si, [di+HP_DATA]
        les     di, [hid_buf]
        cld
        rep     movsb
        pop     cx
        call    xhci_requeue
        mov     al, 1
.out:   pop     si
        pop     es
        pop     di
        ret

; the next TRB of function BX now, if EUSBASPI is idle (else at the next
; poll); SI = controller on the stack frame of the caller is not needed
xhci_requeue:
        cmp     byte [in_aspi], 0
        jne     .ret
        inc     byte [in_aspi]
        push    ax
        push    si
        call    hid_hc
        mov     ax, si
        call    hc_select
        call    xhci_hid_queue
        pop     si
        pop     ax
        dec     byte [in_aspi]
.ret:   ret
