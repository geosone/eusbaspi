;=============================================================================
; hotplug.inc - USB hot plugging for EUSBASPI.SYS (/H)
;
; Only resident with /H, together with the USB initialisation code it uses
; (the driver then ends at hot_end instead of the last DMA page).
;
; The ASPI targets 0..n-1 are slots: an empty slot answers INQUIRY as a
; removable direct access device and everything else with NOT READY /
; MEDIUM NOT PRESENT, so ASPIDISK gives it a drive letter like an empty ZIP
; drive.  Before a command is executed the root ports are looked at (at
; most every 1/2 second): a device that went away empties its slots, a new
; one is reset, enumerated and put into the first empty slot, whose next
; command then gets UNIT ATTENTION / MEDIUM MAY HAVE CHANGED.
;
; Devices behind hubs follow their hub: unplugging the hub empties all of
; them, plugging it in enumerates them.  Changes on the ports of a hub
; that stays connected are not seen.
;=============================================================================

; look at all root ports for connect status changes (rate limited).
; First every port is checked and what went away is removed, then the new
; devices are enumerated: a stick moved to another port gets the slot the
; old one had.
hot_poll:
        pushad
        push    es
        call    get_ticks
        mov     edx, eax
        sub     eax, [hot_last]
        cmp     eax, 9                  ; about 1/2 second (wraps at midnight)
        jb      .out
        mov     [hot_last], edx
        mov     byte [hot_npend], 0
        xor     bx, bx
.hc:    cmp     bl, [usb_hc_count]
        jae     .new
        movzx   cx, byte [hc_nports+bx]
        xor     si, si
.port:  cmp     si, cx
        jae     .nexthc
        push    bx
        push    cx
        push    si
        mov     [init_hc], bl
        mov     [init_port], si
        call    hot_check_port
        pop     si
        pop     cx
        pop     bx
        inc     si
        jmp     .port
.nexthc:
        inc     bx
        jmp     .hc

.new:   cmp     byte [hot_npend], 0     ; enumerate the new devices
        je      .done
        mov     bx, 2                   ; connect debounce (100 ms)
        call    delay_ticks
        xor     di, di
.pend:  movzx   ax, byte [hot_npend]
        cmp     di, ax
        jae     .done
        push    di
        mov     al, [hot_pend_hc+di]
        mov     [init_hc], al
        movzx   ax, byte [hot_pend_port+di]
        mov     [init_port], ax
        call    hot_add_port
        pop     di
        inc     di
        jmp     .pend
.done:  call    get_ticks               ; enumerating took a while
        mov     [hot_last], eax
.out:   pop     es
        popad
        ret

HOT_MAXPEND     equ     16
hot_npend       db      0
hot_pend_hc     times HOT_MAXPEND db 0
hot_pend_port   times HOT_MAXPEND db 0

; root port [init_port] of controller [init_hc]: remove what went away,
; remember the port if something is connected now
hot_check_port:
        mov     al, [init_hc]
        call    hc_select
        call    hot_csc
        jnc     .out                    ; nothing changed
        push    ax
        call    hot_remove_port         ; whatever was there is gone
        pop     ax
        test    al, 1
        jz      .out                    ; nothing connected now
        movzx   bx, byte [hot_npend]
        cmp     bx, HOT_MAXPEND
        jae     .out
        mov     al, [init_hc]
        mov     [hot_pend_hc+bx], al
        mov     al, [init_port]
        mov     [hot_pend_port+bx], al
        inc     byte [hot_npend]
.out:   ret

; reset and enumerate root port [init_port] of controller [init_hc]
hot_add_port:
        mov     al, [init_hc]
        call    hc_select
        mov     byte [init_tier], 0
        movzx   bx, byte [init_hc]
        mov     al, [hc_type+bx]
        cmp     al, HC_UHCI
        je      .uhci
        cmp     al, HC_OHCI
        je      .ohci
        cmp     al, HC_EHCI
        je      .ehci
        jmp     xhci_port
.uhci:  jmp     uhci_port
.ohci:  jmp     ohci_port
.ehci:  jmp     ehci_port

; connect status change of port [init_port] on the selected controller:
; CF if it changed (the change bit is cleared), AL bit 0 = connected
hot_csc:
        movzx   bx, byte [init_hc]
        mov     al, [hc_type+bx]
        mov     si, [init_port]
        cmp     al, HC_UHCI
        je      .uhci
        cmp     al, HC_OHCI
        je      .ohci
        cmp     al, HC_EHCI
        je      .ehci

        call    xh_portreg              ; xHCI
        call    reg_rd
        test    eax, 1 << 17
        jz      .none
        push    eax
        and     eax, XP_KEEP
        or      eax, 1 << 17
        mov     ecx, eax
        mov     ax, [port_reg]
        call    reg_wr
        pop     eax
        jmp     .changed

.ehci:  call    ehci_portreg
        mov     [port_reg], ax
        call    reg_rd
        test    al, EP_CSC
        jz      .none
        push    eax
        and     eax, ~EP_RWC
        or      eax, EP_CSC
        mov     ecx, eax
        mov     ax, [port_reg]
        call    reg_wr
        pop     eax
        test    eax, EP_PO              ; the companion controller's device
        jnz     .none
        jmp     .changed

.ohci:  mov     ax, si
        shl     ax, 2
        add     ax, OHCI_RHPORT
        mov     [port_reg], ax
        call    reg_rd
        test    eax, ORP_CSC
        jz      .none
        push    eax
        mov     ecx, ORP_CSC            ; write 1 to clear, nothing else
        mov     ax, [port_reg]
        call    reg_wr
        pop     eax
        jmp     .changed

.uhci:  mov     dx, si
        add     dx, dx
        add     dx, UHCI_PORTSC
        call    uhci_port_addr
        in      ax, dx
        test    al, UP_CSC
        jz      .none
        push    ax
        and     ax, ~(UP_PR)
        or      ax, UP_CSC | UP_PEC
        out     dx, ax
        pop     ax
.changed:
        and     al, 1
        stc
        ret
.none:  clc
        ret

; forget every device that was behind root port [init_port] of controller
; [init_hc]: its targets become empty slots
hot_remove_port:
        xor     bx, bx
.dev:   cmp     bx, USB_MAX_DEV
        jae     .out
        cmp     byte [usbd_used+bx], 0
        je      .next
        mov     al, [init_hc]
        cmp     [usbd_hc+bx], al
        jne     .next
        mov     al, [init_port]
        cmp     [usbd_rootport+bx], al
        jne     .next
        xor     di, di                  ; its targets
.tgt:   cmp     di, MAX_DEV
        jae     .drop
        cmp     byte [dev_trans+di], TR_USB
        jne     .ntgt
        push    di
        add     di, di
        mov     al, [dev_port+di]       ; low byte: USB device
        pop     di
        cmp     al, bl
        jne     .ntgt
        mov     byte [dev_trans+di], TR_EMPTY
        mov     byte [dev_type+di], 0
        mov     byte [dev_ua+di], 0
.ntgt:  inc     di
        jmp     .tgt
.drop:  push    bx
        call    xhci_drop_dev
        pop     bx
        mov     byte [usbd_used+bx], 0
.next:  inc     bx
        jmp     .dev
.out:   ret
