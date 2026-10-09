;=============================================================================
; usbinit.inc - USB transport of EUSBASPI.SYS (initialisation, discarded)
;
; Finds the USB host controllers on the PCI bus, takes the supported ones
; over from the BIOS, powers and resets the root ports, enumerates the
; devices and registers every LUN of a bulk-only mass storage device as an
; ASPI target.
;=============================================================================

; next free DMA page (cleared, locked) -> AX = offset from ud_base,
; EDX = physical address; CF if the DMA area is used up
alloc_page:
        push    cx
        push    di
        mov     ax, [ud_next]
        mov     cx, [ud_base]
        sub     cx, dma_area
        add     cx, ax
        add     cx, 1000h
        cmp     cx, DMA_RESERVE
        ja      .full
        add     word [ud_next], 1000h
        push    ax
        push    es
        push    cs
        pop     es
        mov     di, [ud_base]
        add     di, ax
        push    di
        mov     cx, 800h
        xor     ax, ax
        rep     stosw
        pop     ax
        pop     es
        mov     ecx, 1000h
        call    get_phys
        mov     edx, eax
        pop     ax
        jmp     .out
.full:  stc
.out:   pop     di
        pop     cx
        ret

; DMA area: 4 KB aligned, bounce page + structure page, each locked alone
usb_setup_dma:
        mov     ax, cs
        movzx   eax, ax
        shl     eax, 4
        add     eax, dma_area
        mov     edx, eax
        add     eax, 0FFFh              ; align to 4 KB
        and     ax, 0F000h
        sub     eax, edx
        add     ax, dma_area
        mov     [ud_base], ax
        mov     bx, ax

        lea     di, [bx+UD_BOUNCE]      ; clear everything
        mov     cx, UD_USED / 2
        xor     ax, ax
        push    es
        push    cs
        pop     es
        rep     stosw
        pop     es

        lea     ax, [bx+UD_BOUNCE]
        mov     [bounce_ptr], ax
        mov     ecx, USB_BOUNCE
        call    get_phys
        jc      .fail
        mov     [phys_bounce], eax
        lea     ax, [bx+UD_HCCA]
        mov     ecx, UD_USED - UD_HCCA
        call    get_phys
        jc      .fail
        mov     [phys_ud], eax
        lea     ax, [bx+UD_SCRATCH]     ; INT 15h register access
        mov     [scratch_ptr], ax
        mov     eax, [phys_ud]
        add     eax, UD_SCRATCH - UD_HCCA
        mov     [phys_scratch], eax

        mov     ax, cs                  ; flat mode GDT
        movzx   eax, ax
        shl     eax, 4
        add     eax, flat_gdt
        mov     [flat_gdtr+2], eax
        clc
.fail:  ret

;-----------------------------------------------------------------------------
; Find the USB host controllers (class 0C03h)
;-----------------------------------------------------------------------------
usb_scan:
        mov     ax, 0B101h
        xor     edi, edi
        int     1Ah
        jc      .out
        cmp     edx, 'PCI '
        jne     .out
        mov     [pci_lastbus], cl
        xor     bx, bx
.dev:   mov     [pci_bdf], bx
        xor     di, di
        call    pci_rd
        cmp     cx, 0FFFFh
        jne     .present
        test    bl, 7
        jnz     .next
        or      bl, 7
        jmp     .next
.present:
        mov     [pci_vendev], ecx
        mov     di, 08h
        call    pci_rd
        shr     ecx, 8
        mov     dl, cl                  ; prog-if
        shr     ecx, 8
        cmp     cx, 0C03h
        jne     .func0
        call    usb_found_hc
.func0: test    bl, 7
        jnz     .next
        mov     di, 0Ch
        call    pci_rd
        test    ecx, 00800000h
        jnz     .next
        or      bl, 7
.next:  inc     bl
        jnz     .dev
        cmp     bh, [pci_lastbus]
        jae     .init
        inc     bh
        jmp     .dev
.init:  ; EHCI first: it hands full and low speed devices to its companion
        ; controllers, which are enumerated afterwards
        mov     al, HC_EHCI
        call    usb_init_type
        mov     al, HC_OHCI
        call    usb_init_type
        mov     al, HC_UHCI
        call    usb_init_type
        mov     al, HC_XHCI
        call    usb_init_type
.out:   ret

; initialise every recorded controller of type AL
usb_init_type:
        xor     bx, bx
.hc:    cmp     bl, [usb_hc_count]
        jae     .done
        cmp     [hc_type+bx], al
        jne     .next
        push    ax
        push    bx
        mov     al, bl
        cmp     byte [hc_type+bx], HC_EHCI
        je      .ehci
        cmp     byte [hc_type+bx], HC_UHCI
        je      .uhci
        cmp     byte [hc_type+bx], HC_XHCI
        je      .xhci
        call    ohci_init
        jmp     .ret
.uhci:  call    uhci_init
        jmp     .ret
.xhci:  call    xhci_init
        jmp     .ret
.ehci:  call    ehci_init
.ret:   pop     bx
        pop     ax
.next:  inc     bx
        jmp     .hc
.done:  ret

; USB controller at [pci_bdf], DL = prog-if
usb_found_hc:
        push    bx
        push    dx
        mov     al, HC_UHCI
        mov     si, msg_uhci
        cmp     dl, 00h
        je      .type
        mov     al, HC_OHCI
        mov     si, msg_ohci
        cmp     dl, 10h
        je      .type
        mov     al, HC_EHCI
        mov     si, msg_ehci
        cmp     dl, 20h
        je      .type
        mov     al, HC_XHCI
        mov     si, msg_xhci
        cmp     dl, 30h
        jne     .out
.type:  mov     [new_hc_type], al
        mov     [new_hc_name], si
        cmp     byte [opt_quiet], 0
        jne     .quiet
        call    print                   ; "OHCI"
        mov     si, msg_hc_at
        call    print
        mov     ax, [pci_vendev]
        call    print_hex16
        mov     al, ':'
        call    putc
        mov     ax, [pci_vendev+2]
        call    print_hex16
        mov     si, msg_ctl3            ; " at PCI "
        call    print
        mov     al, [pci_bdf+1]
        call    print_hex8
        mov     al, ':'
        call    putc
        mov     al, [pci_bdf]
        shr     al, 3
        call    print_hex8
        mov     al, '.'
        call    putc
        mov     al, [pci_bdf]
        and     al, 7
        add     al, '0'
        call    putc
.quiet: cmp     byte [new_hc_type], HC_OHCI
        je      .ohci
        cmp     byte [new_hc_type], HC_EHCI
        je      .ohci
        cmp     byte [new_hc_type], HC_UHCI
        je      .uhci
        cmp     byte [new_hc_type], HC_XHCI
        jne     .later
        cmp     byte [opt_xhci], 0
        jne     .ohci
        cmp     byte [opt_quiet], 0
        jne     .out
        mov     si, msg_xhci_skip
        call    print
        jmp     .out
.later: cmp     byte [opt_quiet], 0
        jne     .out
        mov     si, msg_hc_later
        call    print
        jmp     .out

.uhci:  movzx   bx, byte [usb_hc_count]
        cmp     bx, USB_MAX_HC
        jae     .out
        mov     di, PCI_BAR0 + 16       ; BAR4: I/O ports
        call    pci_rd
        test    cl, 1
        jz      .bad
        and     ecx, 0FFE0h
        jz      .bad
        push    ecx
        mov     di, PCI_CMD             ; I/O decode + bus master on
        call    pci_rd
        or      cl, PCI_CMD_IO | PCI_CMD_MASTER
        call    pci_wr
        pop     ecx
        jmp     .store
.ohci:  movzx   bx, byte [usb_hc_count]
        cmp     bx, USB_MAX_HC
        jae     .out
        mov     di, PCI_BAR0
        call    pci_rd
        test    cl, 1                   ; must be a memory BAR
        jnz     .bad
        mov     al, cl
        and     ecx, 0FFFFFF00h
        jz      .bad
        and     al, 6                   ; 64 bit BAR above 4 GB?
        cmp     al, 4
        jne     .bar32
        push    ecx
        mov     di, PCI_BAR0 + 4
        call    pci_rd
        or      ecx, ecx
        pop     ecx
        jnz     .bad
.bar32:
        push    ecx
        mov     di, PCI_CMD             ; memory decode + bus master on
        call    pci_rd
        or      cl, PCI_CMD_MEM | PCI_CMD_MASTER
        call    pci_wr
        pop     ecx
.store: mov     al, [new_hc_type]
        mov     [hc_type+bx], al
        mov     di, bx
        add     di, di
        mov     ax, [pci_bdf]
        mov     [hc_bdf+di], ax
        add     di, di
        mov     [hc_base+di], ecx
        inc     byte [usb_hc_count]
        cmp     byte [opt_quiet], 0
        jne     .init
        mov     si, msg_ctl4            ; ", ABAR "
        call    print
        mov     eax, ecx
        call    print_hex32
        mov     si, msg_crlf
        call    print
.init:  jmp     .out
.bad:   cmp     byte [opt_quiet], 0
        jne     .out
        mov     si, msg_bad_bar
        call    print
.out:   pop     dx
        pop     bx
        ret

new_hc_type     db      0
new_hc_name     dw      0

;-----------------------------------------------------------------------------
; UHCI: switch off the BIOS legacy support, reset, frame list with one
; queue head in every frame, start, reset the two root ports.
; AL = controller index
;-----------------------------------------------------------------------------
uhci_init:
        mov     [init_hc], al
        mov     byte [cur_hc], 0FFh
        call    hc_select
        movzx   bx, al
        add     bx, bx
        mov     ax, [hc_bdf+bx]
        mov     [pci_bdf], ax
        mov     di, 0C0h                ; LEGSUP: no SMIs, clear status
        call    pci_rd
        mov     cx, 8F00h
        call    pci_wr

        mov     dx, UHCI_USBCMD         ; stop
        call    uhci_port_addr
        xor     ax, ax
        out     dx, ax
        mov     ax, UC_GRESET           ; global reset
        out     dx, ax
        mov     bx, 1
        call    delay_ticks
        xor     ax, ax
        out     dx, ax
        mov     bx, 1
        call    delay_ticks
        mov     ax, UC_HCRESET
        out     dx, ax
        mov     cx, 20
.rst:   mov     bx, 1
        call    delay_ticks
        in      ax, dx
        test    al, UC_HCRESET
        jz      .reset
        loop    .rst
        jmp     .dead
.reset:
        ; two frame lists for all UHCI controllers: every frame -> our
        ; queue head (the controller with a transfer), nothing (the others)
        cmp     dword [phys_fl], 0
        jne     .lists
        call    alloc_page
        jc      .dead
        mov     si, ax
        mov     [phys_fl], edx
        call    alloc_page
        jc      .dead
        mov     [phys_fl_idle], edx
        push    ax
        mov     ax, UD_UQH
        call    ud_addr
        mov     dword [di], 1           ; no next QH
        mov     dword [di+4], 1         ; no TDs
        or      al, 2
        mov     ebx, eax
        mov     di, [ud_base]
        add     di, si
        mov     cx, 1024
.fl:    mov     [di], ebx
        add     di, 4
        loop    .fl
        pop     ax
        mov     di, [ud_base]
        add     di, ax
        mov     cx, 1024
.fli:   mov     dword [di], 1
        add     di, 4
        loop    .fli
.lists:

        mov     dx, UHCI_USBINTR
        call    uhci_port_addr
        xor     ax, ax
        out     dx, ax
        mov     dx, UHCI_FRNUM
        call    uhci_port_addr
        out     dx, ax
        mov     eax, [phys_fl_idle]
        call    uhci_set_fl
        mov     dx, UHCI_SOFMOD
        call    uhci_port_addr
        mov     al, 40h
        out     dx, al
        mov     dx, UHCI_USBSTS         ; clear status
        call    uhci_port_addr
        mov     ax, 3Fh
        out     dx, ax
        mov     dx, UHCI_USBCMD
        call    uhci_port_addr
        mov     ax, UC_RS | UC_CF | UC_MAXP
        out     dx, ax
        mov     bx, 1
        call    delay_ticks

        mov     cl, 2
        push    bx
        movzx   bx, byte [init_hc]
        mov     [hc_nports+bx], cl
        pop     bx
        xor     si, si                  ; two root ports
.port:  mov     [init_port], si
        push    si
        call    uhci_port
        pop     si
        inc     si
        cmp     si, 2
        jb      .port
        ret
.dead:  cmp     byte [opt_quiet], 0
        jne     .out
        mov     si, msg_hc_dead
        call    print
.out:   ret

uhci_port:
        mov     dx, [init_port]
        add     dx, dx
        add     dx, UHCI_PORTSC
        call    uhci_port_addr
        in      ax, dx
        test    al, UP_CCS
        jz      .out
        mov     ax, UP_PR               ; reset >= 50 ms
        out     dx, ax
        mov     bx, 1
        call    delay_ticks
        xor     ax, ax
        out     dx, ax
        mov     cx, 10                  ; enable, until it sticks
.en:    in      ax, dx
        test    al, UP_CCS
        jz      .out
        mov     ax, UP_PE | UP_CSC | UP_PEC
        out     dx, ax
        push    cx
        mov     bx, 1
        call    delay_ticks
        pop     cx
        in      ax, dx
        test    al, UP_PE
        jnz     .on
        loop    .en
        jmp     .out
.on:    xor     cl, cl                  ; full speed
        test    ax, UP_LSDA
        jz      .speed
        inc     cl                      ; low speed
.speed: mov     [init_speed], cl
        call    usb_enum_device
.out:   ret

;-----------------------------------------------------------------------------
; xHCI: BIOS handoff, reset, scratchpads (XMS), rings, start, ports.
; AL = controller index
;-----------------------------------------------------------------------------
xhci_init:
        mov     [init_hc], al
        cmp     byte [xh_nhc], XHCI_MAX
        jb      .first
        mov     si, msg_xhci_one
        cmp     byte [opt_quiet], 0
        je      .msg
        ret
.first: mov     byte [cur_hc], 0FFh
        call    hc_select               ; its (empty) state
        cmp     dword [xh_phys], 0      ; the shared pages, once
        jne     .area
        call    alloc_page
        jc      .dead
        mov     [xh_page], ax
        mov     [xh_phys], edx
        call    alloc_page
        jc      .dead
        mov     [xh_phys+4], edx
        call    alloc_page
        jc      .dead
        mov     [xh_phys+8], edx
.area:  mov     al, [xh_nhc]
        mov     ah, 0
        mov     dx, XH_AREA_SIZE
        mul     dx
        add     ax, XH_AREA0
        mov     [xh_area], ax
        inc     byte [xh_nhc]
        mov     word [xh_cmd_st], 0100h
        mov     byte [xh_evt_deq], 0
        mov     byte [xh_evt_cyc], 1

        xor     ax, ax                  ; capability registers
        call    reg_rd
        movzx   ax, al
        mov     [xh_op], ax
        mov     ax, 4
        call    reg_rd
        mov     [xh_hcs1], eax
        mov     ax, 8
        call    reg_rd
        mov     [xh_hcs2], eax
        mov     ax, 10h
        call    reg_rd
        mov     [xh_hcc], eax
        mov     ax, 14h
        call    reg_rd
        and     ax, ~3
        mov     [xh_db], ax
        mov     ax, 18h
        call    reg_rd
        and     ax, ~1Fh
        mov     [xh_rt], ax
        mov     byte [xh_csh], 5
        test    byte [xh_hcc], 4        ; 64 byte contexts
        jz      .ecp
        mov     byte [xh_csh], 6

.ecp:   ; BIOS handoff: USB legacy support capability
        mov     ax, [xh_hcc+2]
        shl     ax, 2
.cap:   or      ax, ax
        jz      .reset
        mov     [xh_capoff], ax
        call    reg_rd
        cmp     al, 1
        je      .legacy
        movzx   dx, ah
        or      dx, dx
        jz      .reset
        shl     dx, 2
        mov     ax, [xh_capoff]
        add     ax, dx
        jmp     .cap
.legacy:
        test    eax, 1 << 16            ; BIOS owned?
        jz      .smi
        mov     ecx, eax
        or      ecx, 1 << 24            ; OS owned
        mov     ax, [xh_capoff]
        call    reg_wr
        mov     cx, 19
.own:   push    cx
        mov     bx, 1
        call    delay_ticks
        mov     ax, [xh_capoff]
        call    reg_rd
        pop     cx
        test    eax, 1 << 16
        jz      .smi
        loop    .own
        mov     ecx, eax                ; the BIOS does not let go: take it
        and     ecx, ~(1 << 16)
        or      ecx, 1 << 24
        mov     ax, [xh_capoff]
        call    reg_wr
.smi:   mov     ax, [xh_capoff]         ; no SMIs, clear their status
        add     ax, 4
        call    reg_rd
        and     eax, 000E1FEEh
        or      eax, 0E0000000h
        mov     ecx, eax
        mov     ax, [xh_capoff]
        add     ax, 4
        call    reg_wr

.reset: mov     ax, XO_USBCMD           ; stop, reset
        call    xop_rd
        and     al, ~1
        mov     ecx, eax
        mov     ax, XO_USBCMD
        call    xop_wr
        mov     ax, [xh_op]
        add     ax, XO_USBSTS
        mov     ecx, 1
        mov     edx, ecx
        mov     bx, 3
        call    wait_reg
        mov     ecx, 2
        mov     ax, XO_USBCMD
        call    xop_wr
        mov     bx, 1
        call    delay_ticks
        mov     ax, [xh_op]
        add     ax, XO_USBCMD
        mov     ecx, 2
        xor     edx, edx
        mov     bx, 19
        call    wait_reg
        jc      .dead
        mov     ax, [xh_op]             ; controller not ready
        add     ax, XO_USBSTS
        mov     ecx, 800h
        xor     edx, edx
        mov     bx, 19
        call    wait_reg
        jc      .dead

        movzx   ecx, byte [xh_hcs1]     ; device slots
        cmp     cl, XHCI_SLOTS
        jbe     .slots
        mov     cl, XHCI_SLOTS
.slots: mov     ax, XO_CONFIG
        call    xop_wr
        call    xh_scratchpad
        jnc     .dcbaa
        mov     si, msg_xhci_scr
        jmp     .msg
.dcbaa: mov     ax, XH_DCBAA            ; entry 0: the scratchpad array
        add     ax, [xh_area]
        call    xa
        mov     ecx, eax
        mov     eax, [xh_scr_arr]
        mov     [di], eax
        mov     ax, XO_DCBAAP
        call    xop_wr
        xor     ecx, ecx
        mov     ax, XO_DCBAAP + 4
        call    xop_wr

        mov     ax, XH_CMDR             ; command ring, cycle 1
        add     ax, [xh_area]
        call    xa
        mov     ecx, eax
        or      cl, 1
        mov     ax, XO_CRCR
        call    xop_wr
        xor     ecx, ecx
        mov     ax, XO_CRCR + 4
        call    xop_wr

        mov     ax, XH_EVTR             ; event ring: one segment
        add     ax, [xh_area]
        call    xa
        mov     edx, eax
        mov     ax, XH_ERST
        add     ax, [xh_area]
        call    xa
        mov     [di], edx
        mov     dword [di+4], 0
        mov     dword [di+8], XH_EVT_TRBS
        mov     dword [di+12], 0
        mov     ecx, 1
        mov     ax, XR_ERSTSZ
        call    xrt_wr
        mov     ecx, edx
        mov     ax, XR_ERDP
        call    xrt_wr
        xor     ecx, ecx
        mov     ax, XR_ERDP + 4
        call    xrt_wr
        mov     ax, XH_ERST
        add     ax, [xh_area]
        call    xa
        mov     ecx, eax
        mov     ax, XR_ERSTBA
        call    xrt_wr
        xor     ecx, ecx
        mov     ax, XR_ERSTBA + 4
        call    xrt_wr
        mov     ecx, 1                  ; no interrupts, clear pending
        mov     ax, XR_IMAN
        call    xrt_wr

        mov     ecx, 1                  ; run
        mov     ax, XO_USBCMD
        call    xop_wr
        mov     ax, [xh_op]
        add     ax, XO_USBSTS
        mov     ecx, 1
        xor     edx, edx
        mov     bx, 3
        call    wait_reg
        jc      .dead

        ; power all ports, give the links time to come up
        movzx   cx, byte [xh_hcs1+3]
        mov     [xh_ports], cx
        push    bx
        movzx   bx, byte [init_hc]
        mov     [hc_nports+bx], cl
        pop     bx
        xor     si, si
.pp:    cmp     si, [xh_ports]
        jae     .ppw
        call    xh_portreg
        call    reg_rd
        test    ax, 200h
        jnz     .ppn
        and     eax, XP_KEEP
        or      ax, 200h
        mov     ecx, eax
        mov     ax, [port_reg]
        call    reg_wr
.ppn:   inc     si
        jmp     .pp
.ppw:   mov     bx, 6
        call    delay_ticks
        xor     si, si
.port:  cmp     si, [xh_ports]
        jae     .done
        mov     [init_port], si
        push    si
        call    xhci_port
        pop     si
        inc     si
        jmp     .port
.done:  ret
.dead:  mov     si, msg_hc_dead
.msg:   cmp     byte [opt_quiet], 0
        jne     .done
        jmp     print

; port SI -> AX = [port_reg] = its PORTSC
xh_portreg:
        mov     ax, si
        shl     ax, 4
        add     ax, XO_PORTSC
        add     ax, [xh_op]
        mov     [port_reg], ax
        ret

xhci_port:
        mov     si, [init_port]
        call    xh_portreg
        call    reg_rd
        test    al, 1                   ; connected?
        jz      .out
        test    al, 2                   ; USB 3 ports enable themselves
        jnz     .enabled
        mov     byte [xh_warm], 0
        and     eax, XP_KEEP            ; USB 2 (or a stuck USB 3 link):
        or      al, 10h                 ; port reset
.reset: mov     ecx, eax
        mov     ax, [port_reg]
        call    reg_wr
        mov     ax, [port_reg]
        mov     ecx, (1 << 21) | (1 << 19) ; reset done (or warm reset done)
        xor     edx, edx
        mov     bx, 19
        call    xh_wait_any
        jc      .warm
        mov     bx, 1
        call    delay_ticks
        mov     ax, [port_reg]
        call    reg_rd
        test    al, 2
        jnz     .enabled
.warm:  ; a USB 3 link that is not in U0 needs a warm reset (80..120 ms)
        cmp     byte [xh_warm], 0
        jne     .out
        inc     byte [xh_warm]
        mov     ax, [port_reg]
        call    reg_rd
        test    al, 1
        jz      .out
        and     eax, XP_KEEP
        or      eax, XP_CHANGES | (1 << 31)
        jmp     .reset
.enabled:
        mov     ax, [port_reg]          ; clear the change bits
        call    reg_rd
        and     eax, XP_KEEP
        or      eax, XP_CHANGES
        mov     ecx, eax
        mov     ax, [port_reg]
        call    reg_wr
        mov     bx, 1
        call    delay_ticks
        mov     ax, [port_reg]
        call    reg_rd
        test    al, 2
        jz      .out
        shr     eax, 10                 ; port speed
        and     al, 0Fh
        mov     [xh_psi], al
        mov     cl, 3                   ; super speed
        cmp     al, 4
        jae     .speed
        mov     cl, 2
        cmp     al, 3
        je      .speed
        mov     cl, 1
        cmp     al, 2
        je      .speed
        mov     cl, 0
.speed: mov     [init_speed], cl
        call    usb_enum_device
.out:   ret

; wait until any bit of ECX is set in register AX (BX ticks); CF on timeout
xh_wait_any:
        push    eax
        push    esi
        push    di
        mov     di, ax
        pushf
        sti
        call    get_ticks
        mov     esi, eax
.p:     call    poll_pause
        mov     ax, di
        call    reg_rd
        test    eax, ecx
        jnz     .ok
        call    get_ticks
        sub     eax, esi
        cmp     ax, bx
        jbe     .p
        popf
        stc
        jmp     .out
.ok:    popf
        clc
.out:   pop     di
        pop     esi
        pop     eax
        ret

xh_warm         db      0

; scratchpad buffers the controller asks for, from XMS -> xh_scr_arr
; (0 if none are needed); CF if there is no memory for them
xh_scratchpad:
        mov     dword [xh_scr_arr], 0
        mov     eax, [xh_hcs2]
        mov     edx, eax
        shr     eax, 27
        and     ax, 1Fh
        shr     edx, 21
        and     dx, 1Fh
        shl     dx, 5
        or      ax, dx
        mov     [xh_nscr], ax
        or      ax, ax
        jz      .ok
        cmp     dword [xms_entry], 0
        je      .fail
        mov     dx, ax                  ; array page, buffers, alignment
        add     dx, 2
        shl     dx, 2
        mov     ah, 09h
        call    far [xms_entry]
        or      ax, ax
        jz      .fail
        mov     [xh_xms], dx
        mov     ah, 0Ch                 ; lock: DX:BX = physical address
        call    far [xms_entry]
        or      ax, ax
        jz      .fail
        mov     ax, dx
        shl     eax, 16
        mov     ax, bx
        mov     edx, eax
        add     eax, 0FFFh
        and     eax, ~0FFFh
        mov     [xh_scr_arr], eax
        sub     eax, edx
        mov     [xms_mv_dst], eax
        mov     eax, [xh_scr_arr]       ; array in the bounce buffer
        mov     di, [bounce_ptr]
        mov     cx, [xh_nscr]
.e:     add     eax, 1000h
        mov     [di], eax
        mov     dword [di+4], 0
        add     di, 8
        loop    .e
        movzx   eax, word [xh_nscr]     ; copy it into the XMS block
        shl     eax, 3
        mov     [xms_mv_len], eax
        mov     ax, [bounce_ptr]
        mov     [xms_mv_src], ax
        mov     [xms_mv_src+2], cs
        mov     ax, [xh_xms]
        mov     [xms_mv_dh], ax
        mov     si, xms_move
        mov     ah, 0Bh
        call    far [xms_entry]
        or      ax, ax
        jz      .fail
.ok:    clc
        ret
.fail:  stc
        ret

; is device BX on an xHCI controller?  ZF set if so
is_xhci:
        push    bx
        movzx   bx, byte [usbd_hc+bx]
        cmp     byte [hc_type+bx], HC_XHCI
        pop     bx
        ret

; input context entry CL -> DI, EAX = physical (entry cleared)
xh_inentry:
        push    cx
        movzx   ax, cl
        mov     cl, [xh_csh]
        shl     ax, cl
        add     ax, XH_INCTX
        call    xa
        pop     cx
        ret

; clear the input context and fill its slot context: CL = context entries
xh_inslot:
        push    cx
        mov     ax, XH_INCTX
        call    xa
        push    es
        push    cs
        pop     es
        push    di
        mov     cx, 440h / 2
        xor     ax, ax
        rep     stosw
        pop     di
        pop     es
        pop     cx
        mov     cl, 1                   ; entry 1: slot context
        push    cx
        call    xh_inentry
        pop     cx
        movzx   eax, byte [xh_dpsi+si]
        shl     eax, 20
        movzx   edx, ch
        shl     edx, 27
        or      eax, edx
        push    si
        shl     si, 2
        or      eax, [xh_droute+si]
        pop     si
        mov     [di], eax
        movzx   eax, byte [xh_dport+si]
        inc     ax
        shl     eax, 16
        mov     [di+4], eax
        push    si
        add     si, si
        movzx   eax, word [xh_dtt+si]   ; TT hub slot and port
        pop     si
        mov     [di+8], eax
        ret

; ring N (CX): clear it, reset its state -> EAX = physical address | DCS
xh_newring:
        push    di
        push    cx
        mov     di, cx
        add     di, di
        mov     word [xh_ring_st+di], 0100h
        mov     ax, cx
        call    xh_ringoff
        call    xa
        push    eax
        push    es
        push    cs
        pop     es
        mov     cx, 40h
        xor     ax, ax
        rep     stosw
        pop     es
        pop     eax
        or      al, 1
        pop     cx
        pop     di
        ret

; endpoint context for DCI DL: type DH, max packet AX, ring N CX
xh_inep:
        push    eax
        push    cx
        mov     cl, dl
        inc     cl
        call    xh_inentry
        pop     cx
        pop     eax
        movzx   eax, ax
        shl     eax, 16
        movzx   edx, dh
        shl     edx, 3
        or      eax, edx
        or      al, 3 << 1              ; 3 retries
        mov     [di+4], eax
        call    xh_newring
        mov     [di+8], eax
        mov     dword [di+12], 0
        mov     dword [di+16], 8
        ret

; command type EAX for the slot of xHCI device SI -> AL = completion code
xh_slotcmd:
        movzx   edx, byte [xh_slot+si]
        shl     edx, 24
        or      eax, edx
        mov     [xh_trb+12], eax
        jmp     xh_command

; new device BX on [init_port] of the xHCI: Enable Slot, Address Device
xhci_new_dev:
        push    bx
        xor     si, si                  ; first free xHCI device
.free:  cmp     si, XHCI_DEV
        jae     .fail
        cmp     byte [xh_slot+si], 0
        je      .got
        inc     si
        jmp     .free
.got:
        mov     ax, si
        mov     [xh_map+bx], al
        mov     al, [usbd_rootport+bx]
        mov     [xh_dport+si], al
        mov     al, [usbd_speed+bx]     ; port speed id: 1 full, 2 low,
        inc     al                      ; 3 high, 4 super
        mov     [xh_dpsi+si], al
        mov     [xh_psi], al
        push    si
        push    bx
        shl     bx, 2
        mov     eax, [usbd_route+bx]
        shl     si, 2
        mov     [xh_droute+si], eax
        pop     bx
        pop     si
        mov     al, [usbd_tthub+bx]
        mov     ah, [usbd_ttport+bx]
        push    si
        add     si, si
        mov     [xh_dtt+si], ax
        pop     si
        call    xh_clr_trb
        mov     dword [xh_trb+12], XT_ENABLE_SLOT
        call    xh_command
        cmp     al, XC_SUCCESS
        jne     .fail
        mov     al, [xh_ev+15]
        mov     [xh_slot+si], al
        inc     byte [xh_count]

        mov     ax, si                  ; device context, DCBAA entry
        call    xh_devoff
        call    xa
        mov     edx, eax
        push    es
        push    cs
        pop     es
        mov     cx, 200h
        xor     ax, ax
        rep     stosw
        pop     es
        movzx   ax, byte [xh_slot+si]
        shl     ax, 3
        add     ax, XH_DCBAA
        add     ax, [xh_area]
        call    xa
        mov     [di], edx
        mov     dword [di+4], 0

        mov     ch, 1                   ; slot + EP0
        call    xh_inslot
        mov     ax, XH_INCTX
        call    xa
        mov     dword [di+4], 3         ; add slot and EP0
        mov     ax, 8                   ; EP0 max packet until we know
        cmp     byte [xh_psi], 3
        jb      .mps
        mov     ax, 64
        je      .mps
        mov     ax, 512
.mps:   mov     [xh_mps0], ax
        mov     dl, 1
        mov     dh, 4                   ; control
        mov     cx, si
        shl     cx, 2
        call    xh_inep
        call    xh_clr_trb
        mov     ax, XH_INCTX
        call    xa
        mov     [xh_trb], eax
        mov     eax, XT_ADDRESS_DEV
        call    xh_slotcmd
        cmp     al, XC_SUCCESS
        jne     .fail
        mov     bx, 1                   ; SET_ADDRESS recovery
        push    bx
        call    delay_ticks
        pop     bx
        pop     bx
        clc
        ret
.fail:  pop     bx
        stc
        ret

; EP0 max packet size of device BX (from the descriptor in usbd_mps0)
xhci_set_mps0:
        mov     si, bx
        add     si, si
        mov     ax, [usbd_mps0+si]
        cmp     byte [xh_psi], 4        ; super speed: 2^n
        jb      .ls
        mov     cl, al
        mov     ax, 1
        shl     ax, cl
        mov     [usbd_mps0+si], ax
        clc
        ret
.ls:    cmp     ax, [xh_mps0]
        je      .ok
        movzx   si, byte [xh_map+bx]    ; Evaluate Context with the new size
        push    ax
        mov     ch, 1
        call    xh_inslot
        mov     ax, XH_INCTX
        call    xa
        mov     dword [di+4], 2         ; EP0 only
        pop     ax
        push    ax                      ; EP0 context: only the max packet
        mov     cl, 2                   ; size is evaluated, the ring stays
        call    xh_inentry
        pop     ax
        movzx   eax, ax
        shl     eax, 16
        or      eax, (4 << 3) | (3 << 1)
        mov     [di+4], eax
        call    xh_clr_trb
        mov     ax, XH_INCTX
        call    xa
        mov     [xh_trb], eax
        mov     eax, XT_EVAL_CTX
        call    xh_slotcmd
        cmp     al, XC_SUCCESS
        jne     .bad
.ok:    clc
        ret
.bad:   stc
        ret

; bulk endpoints of device BX: Configure Endpoint
xhci_config_eps:
        movzx   si, byte [xh_map+bx]
        mov     al, [usbd_epin+bx]
        and     al, 0Fh
        shl     al, 1
        inc     al
        mov     [xh_dci_in], al
        mov     al, [usbd_epout+bx]
        and     al, 0Fh
        shl     al, 1
        mov     [xh_dci_out], al
        mov     ch, [xh_dci_in]         ; context entries: the highest DCI
        cmp     ch, al
        ja      .ce
        mov     ch, al
.ce:    mov     al, [usbd_epint+bx]
        shl     al, 1
        inc     al
        cmp     byte [usbd_epint+bx], 0
        je      .ce2
        cmp     ch, al
        ja      .ce2
        mov     ch, al
.ce2:   mov     al, 15                  ; room in our contexts
        cmp     byte [xh_csh], 6
        je      .room
        mov     al, 31
.room:  cmp     ch, al
        ja      .bad
        push    bx
        call    xh_inslot
        mov     ax, XH_INCTX
        call    xa
        mov     cl, [xh_dci_in]
        mov     edx, 1
        shl     edx, cl
        mov     cl, [xh_dci_out]
        mov     eax, 1
        shl     eax, cl
        or      eax, edx
        or      al, 1                   ; and the slot context
        mov     cl, [usbd_epint+bx]
        or      cl, cl
        jz      .addf
        shl     cl, 1
        inc     cl
        mov     edx, 1
        shl     edx, cl
        or      eax, edx
.addf:  mov     [di+4], eax
        pop     bx
        push    bx
        mov     di, bx
        add     di, di
        mov     ax, [usbd_mpsout+di]
        mov     dl, [xh_dci_out]
        mov     dh, 2                   ; bulk out
        mov     cx, si
        shl     cx, 2
        inc     cx
        call    xh_inep
        mov     dword [di+16], 1024     ; average TRB length
        pop     bx
        push    bx
        mov     di, bx
        add     di, di
        mov     ax, [usbd_mpsin+di]
        mov     dl, [xh_dci_in]
        mov     dh, 6                   ; bulk in
        mov     cx, si
        shl     cx, 2
        add     cx, 2
        call    xh_inep
        mov     dword [di+16], 1024
        pop     bx
        push    bx
        mov     al, [usbd_epint+bx]     ; CBI: interrupt IN
        or      al, al
        jz      .noint
        shl     al, 1
        inc     al
        mov     dl, al
        mov     di, bx
        add     di, di
        mov     ax, [usbd_mpsint+di]
        mov     dh, 7
        mov     cx, si
        shl     cx, 2
        add     cx, 3
        call    xh_inep
        mov     dword [di], 3 << 16     ; interval 1 ms
        mov     eax, [di+4]             ; max payload per interval: one packet
        and     eax, 0FFFF0000h
        or      al, 2                   ; average TRB length
        mov     [di+16], eax
.noint: call    xh_clr_trb
        mov     ax, XH_INCTX
        call    xa
        mov     [xh_trb], eax
        mov     eax, XT_CONFIG_EP
        call    xh_slotcmd
        pop     bx
        cmp     al, XC_SUCCESS
        jne     .bad
        clc
        ret
.bad:   stc
        ret

; give up device BX (on xHCI): Disable Slot
xhci_drop_dev:
        call    is_xhci
        jne     .out
        movzx   si, byte [xh_map+bx]
        cmp     si, XHCI_DEV
        jae     .out
        cmp     byte [xh_slot+si], 0
        je      .out
        push    bx
        call    xh_clr_trb
        mov     eax, XT_DISABLE_SLOT
        call    xh_slotcmd
        movzx   ax, byte [xh_slot+si]
        shl     ax, 3
        add     ax, XH_DCBAA
        add     ax, [xh_area]
        call    xa
        mov     dword [di], 0
        mov     byte [xh_slot+si], 0
        dec     byte [xh_count]
        pop     bx
        mov     byte [xh_map+bx], 0FFh
.out:   ret

; Hub BX (just configured): power its ports, reset them one by one and
; enumerate what is behind them.  Hubs are only looked at during init:
; devices plugged in later are not seen.
usb_hub_scan:
        cmp     byte [usbd_speed+bx], 3 ; USB 3 hubs: not yet
        jae     .out
        cmp     byte [usbd_tier+bx], 4  ; at most 5 tiers of hubs
        ja      .out
        mov     ax, 06A0h               ; hub descriptor
        mov     dx, 2900h
        xor     cx, cx
        mov     si, 9
        call    usb_control
        cmp     al, US_OK
        jne     .out
        mov     si, [bounce_ptr]
        mov     al, [si+2]
        cmp     al, 15
        jbe     .np
        mov     al, 15
.np:    mov     [hub_ports], al
        mov     al, [si+3]              ; TT think time
        shr     al, 5
        and     al, 3
        mov     [hub_ttt], al
        call    is_xhci                 ; xHCI: high speed hubs get their
        jne     .power                  ; TT described in the slot
        cmp     byte [usbd_speed+bx], 2
        jne     .power
        call    xhci_hub_slot
.power: mov     cl, 1                   ; power on all ports
.pw:    cmp     cl, [hub_ports]
        ja      .pwdone
        push    cx
        movzx   cx, cl
        mov     ax, 0323h               ; SET_FEATURE(PORT_POWER)
        mov     dx, 8
        xor     si, si
        call    usb_control
        pop     cx
        inc     cl
        jmp     .pw
.pwdone:
        push    bx
        mov     bx, 3                   ; power good, devices settle
        call    delay_ticks
        pop     bx
        mov     cl, 1
.port:  cmp     cl, [hub_ports]
        ja      .out
        push    cx
        call    hub_port
        pop     cx
        inc     cl
        jmp     .port
.out:   ret

; port CL of hub BX: reset, enumerate the device behind it
hub_port:
        movzx   cx, cl
        mov     [hub_cur], cx
        call    hub_status
        jc      .out
        test    al, 1                   ; connected?
        jz      .out
        mov     ax, 0323h               ; SET_FEATURE(PORT_RESET)
        mov     dx, 4
        mov     cx, [hub_cur]
        xor     si, si
        call    usb_control
        mov     di, 10
.rst:   push    bx
        mov     bx, 1
        call    delay_ticks
        pop     bx
        call    hub_status
        jc      .out
        test    dl, 10h                 ; C_PORT_RESET
        jnz     .done
        dec     di
        jnz     .rst
        ret
.done:  push    ax
        mov     ax, 0123h               ; CLEAR_FEATURE(C_PORT_RESET)
        mov     dx, 20
        mov     cx, [hub_cur]
        xor     si, si
        call    usb_control
        mov     ax, 0123h               ; CLEAR_FEATURE(C_PORT_CONNECTION)
        mov     dx, 16
        mov     cx, [hub_cur]
        xor     si, si
        call    usb_control
        pop     ax
        test    al, 2                   ; enabled?
        jz      .out
        mov     dl, 0                   ; full speed
        test    ah, 2
        jz      .nls
        mov     dl, 1                   ; low speed
.nls:   test    ah, 4
        jz      .spd
        mov     dl, 2                   ; high speed
.spd:   ; the device inherits the place of the hub, one tier further down
        push    word [init_port]
        push    word [init_speed]
        push    dword [init_route]
        push    word [init_tier]
        push    word [init_tthub]
        push    word [init_rootport]
        push    bx
        mov     [init_speed], dl
        mov     ax, [hub_cur]
        dec     ax
        mov     [init_port], ax
        mov     al, [usbd_rootport+bx]
        mov     [init_rootport], al
        mov     cl, [usbd_tier+bx]
        mov     al, cl
        inc     al
        mov     [init_tier], al
        shl     cl, 2
        movzx   eax, byte [hub_cur]
        shl     eax, cl
        mov     si, bx
        shl     si, 2
        or      eax, [usbd_route+si]
        mov     [init_route], eax
        mov     al, [usbd_tthub+bx]     ; behind the same TT as the hub ...
        mov     ah, [usbd_ttport+bx]
        cmp     byte [usbd_speed+bx], 2 ; ... unless the hub is the TT
        jne     .tt
        cmp     dl, 2
        je      .tt
        mov     al, [usbd_addr+bx]
        call    is_xhci
        jne     .ttp
        push    bx
        movzx   bx, byte [xh_map+bx]
        mov     al, [xh_slot+bx]
        pop     bx
.ttp:   mov     ah, [hub_cur]
.tt:    mov     [init_tthub], ax
        push    word [hub_cur]          ; a hub behind it reuses these
        push    word [hub_ports]
        call    usb_enum_device
        pop     word [hub_ports]
        pop     word [hub_cur]
        pop     bx
        pop     word [init_rootport]
        pop     word [init_tthub]
        pop     word [init_tier]
        pop     dword [init_route]
        pop     word [init_speed]
        pop     word [init_port]
.out:   ret

; GET_STATUS of port [hub_cur] of hub BX -> AX = status, DX = change
hub_status:
        mov     ax, 00A3h
        xor     dx, dx
        mov     cx, [hub_cur]
        mov     si, 4
        call    usb_control
        cmp     al, US_OK
        jne     .err
        mov     si, [bounce_ptr]
        mov     ax, [si]
        mov     dx, [si+2]
        clc
        ret
.err:   stc
        ret

; xHCI: describe high speed hub BX in its slot (hub, ports, TT think time)
xhci_hub_slot:
        push    bx
        movzx   si, byte [xh_map+bx]
        mov     ch, 1
        call    xh_inslot
        or      dword [di], 1 << 26     ; hub
        movzx   eax, byte [hub_ports]
        shl     eax, 24
        or      [di+4], eax
        movzx   eax, byte [hub_ttt]
        shl     eax, 16
        or      [di+8], eax
        mov     ax, XH_INCTX
        call    xa
        mov     dword [di+4], 1         ; slot context only
        call    xh_clr_trb
        mov     ax, XH_INCTX
        call    xa
        mov     [xh_trb], eax
        mov     eax, XT_CONFIG_EP
        call    xh_slotcmd
        pop     bx
        ret

hub_ports       db      0
hub_ttt         db      0
hub_cur         dw      0
new_class       db      0
init_tier       db      0
                db      0
init_rootport   db      0
                db      0
init_route      dd      0
init_tthub      db      0
init_ttport     db      0

xh_hcs1         dd      0
xh_hcs2         dd      0
xh_hcc          dd      0
xh_capoff       dw      0
xh_ports        dw      0
xh_psi          db      0
xh_count        db      0               ; xHCI devices, all controllers
xh_nhc          db      0               ; xHCI controllers
xh_mps0         dw      0
xh_dport        times XHCI_DEV db 0
xh_dpsi         times XHCI_DEV db 0
xh_dci_in       db      0
xh_dci_out      db      0
xh_nscr         dw      0
xh_scr_arr      dd      0
xh_xms          dw      0
xms_move:
xms_mv_len      dd      0
                dw      0               ; source: conventional memory
xms_mv_src      dd      0
xms_mv_dh       dw      0
xms_mv_dst      dd      0

;-----------------------------------------------------------------------------
; EHCI: take over from the BIOS, reset, start, route the ports to EHCI,
; reset them; high speed devices are enumerated here, full / low speed
; devices are handed to the companion controller.  AL = controller index
;-----------------------------------------------------------------------------
ehci_init:
        mov     [init_hc], al
        movzx   bx, al
        mov     di, bx
        add     di, di
        mov     ax, [hc_bdf+di]
        mov     [pci_bdf], ax
        add     di, di
        mov     eax, [hc_base+di]       ; capability registers
        mov     [abar], eax
        mov     byte [cur_hc], 0FFh
        push    di
        xor     ax, ax
        call    reg_rd
        movzx   edx, al                 ; capability length
        mov     ax, 4
        call    reg_rd
        mov     [ehci_hcs], eax         ; ports, port power control
        mov     ax, 8
        call    reg_rd
        mov     [ehci_hcc], eax         ; 64 bit, extended capabilities
        pop     di
        add     [hc_base+di], edx       ; from now on: operational registers
        mov     al, [init_hc]
        call    hc_select

        ; BIOS handoff through USBLEGSUP in PCI config space
        movzx   di, byte [ehci_hcc+1]
        cmp     di, 40h
        jb      .reset
        call    pci_rd
        cmp     cl, 1                   ; USBLEGSUP capability
        jne     .reset
        test    ecx, 1 << 16            ; BIOS owned?
        jz      .smi
        or      ecx, 1 << 24            ; OS owned semaphore
        call    pci_wr
        mov     cx, 19                  ; wait up to 1 s
.own:   push    cx
        mov     bx, 1
        call    delay_ticks
        call    pci_rd
        pop     ax
        test    ecx, 1 << 16
        jz      .smi
        mov     cx, ax
        loop    .own
.smi:   add     di, 4                   ; no SMIs from the controller
        xor     ecx, ecx
        call    pci_wr

.reset: mov     ax, EHCI_USBCMD         ; stop
        call    reg_rd
        mov     ecx, eax
        and     cl, ~EC_RS
        mov     ax, EHCI_USBCMD
        call    reg_wr
        mov     ax, EHCI_USBSTS
        mov     ecx, ES_HALTED
        mov     edx, ecx
        mov     bx, 3
        call    wait_reg
        mov     ecx, EC_HCRESET
        mov     ax, EHCI_USBCMD
        call    reg_wr
        mov     ax, EHCI_USBCMD
        mov     ecx, EC_HCRESET
        xor     edx, edx
        mov     bx, 5
        call    wait_reg
        jc      .dead

        xor     ecx, ecx
        mov     ax, EHCI_USBINTR
        call    reg_wr
        test    byte [ehci_hcc], 1      ; 64 bit structures: upper half 0
        jz      .run
        mov     ax, EHCI_CTRLDSSEG
        call    reg_wr
.run:   mov     ecx, EC_ITC8 | EC_RS
        mov     ax, EHCI_USBCMD
        call    reg_wr
        mov     ax, EHCI_USBSTS
        mov     ecx, ES_HALTED
        xor     edx, edx
        mov     bx, 3
        call    wait_reg
        mov     ecx, 1                  ; ports to EHCI
        mov     ax, EHCI_CONFIGFLAG
        call    reg_wr
        mov     bx, 1
        call    delay_ticks

        ; port power, then each port
        movzx   cx, byte [ehci_hcs]
        and     cl, 0Fh
        mov     [ehci_ports], cx
        push    bx
        movzx   bx, byte [init_hc]
        mov     [hc_nports+bx], cl
        pop     bx
        test    byte [ehci_hcs], 10h    ; port power control
        jz      .powered
        xor     si, si
.pp:    cmp     si, [ehci_ports]
        jae     .ppw
        call    ehci_portreg
        call    reg_rd
        and     eax, ~EP_RWC
        or      eax, EP_PP
        mov     ecx, eax
        call    ehci_portreg
        call    reg_wr
        inc     si
        jmp     .pp
.ppw:   mov     bx, 1
        call    delay_ticks
.powered:
        xor     si, si
.port:  cmp     si, [ehci_ports]
        jae     .done
        mov     [init_port], si
        push    si
        call    ehci_port
        pop     si
        inc     si
        jmp     .port
.done:  ret
.dead:  cmp     byte [opt_quiet], 0
        jne     .done
        mov     si, msg_hc_dead
        jmp     print

; AX = PORTSC register of port SI
ehci_portreg:
        mov     ax, si
        shl     ax, 2
        add     ax, EHCI_PORTSC
        ret

; reset port [init_port]; high speed device: enumerate, else companion
ehci_port:
        mov     si, [init_port]
        call    ehci_portreg
        mov     [port_reg], ax
        call    reg_rd
        test    al, EP_CCS
        jz      .out
        mov     edx, eax
        and     edx, EP_LS_MASK
        cmp     edx, EP_LS_K            ; low speed: companion
        je      .release
        and     eax, ~(EP_RWC | EP_PE)
        or      eax, EP_PR
        mov     ecx, eax
        mov     ax, [port_reg]
        call    reg_wr
        mov     bx, 1                   ; >= 50 ms
        call    delay_ticks
        mov     ax, [port_reg]
        call    reg_rd
        and     eax, ~(EP_RWC | EP_PR)
        mov     ecx, eax
        mov     ax, [port_reg]
        call    reg_wr
        mov     ax, [port_reg]
        mov     ecx, EP_PR
        xor     edx, edx
        mov     bx, 2
        call    wait_reg
        mov     bx, 1
        call    delay_ticks
        mov     ax, [port_reg]
        call    reg_rd
        test    al, EP_PE
        jz      .release                ; full speed device: companion
        mov     byte [init_speed], 2
        call    usb_enum_device
.out:   ret
.release:
        mov     ax, [port_reg]
        call    reg_rd
        and     eax, ~EP_RWC
        or      eax, EP_PO
        mov     ecx, eax
        mov     ax, [port_reg]
        call    reg_wr
        cmp     byte [opt_verbose], 0
        je      .out
        mov     si, msg_usbport
        call    print
        mov     ax, [init_port]
        call    print_dec
        mov     si, msg_companion
        jmp     print

ehci_hcs        dd      0
ehci_hcc        dd      0
ehci_ports      dw      0

;-----------------------------------------------------------------------------
; OHCI: take over, reset, start, power the root ports, enumerate them
; AL = controller index
;-----------------------------------------------------------------------------
ohci_init:
        mov     [init_hc], al
        mov     byte [cur_hc], 0FFh
        call    hc_select
        mov     ax, OHCI_REVISION
        call    reg_rd
        cmp     al, 10h
        jne     .dead

        mov     ax, OHCI_CONTROL        ; SMM BIOS driver active?
        call    reg_rd
        test    eax, OC_IR
        jz      .owned
        mov     ecx, OCS_OCR            ; ownership change request
        mov     ax, OHCI_CMDSTATUS
        call    reg_wr
        mov     ax, OHCI_CONTROL
        mov     ecx, OC_IR
        xor     edx, edx
        mov     bx, 19
        call    wait_reg
.owned:
        mov     ax, OHCI_FMINTERVAL     ; keep the frame interval
        call    reg_rd
        and     eax, 3FFFh
        jnz     .fi
        mov     ax, 2EDFh
.fi:    mov     [ohci_fi], eax
        mov     ecx, OCS_HCR            ; host controller reset
        mov     ax, OHCI_CMDSTATUS
        call    reg_wr
        mov     ax, OHCI_CMDSTATUS
        mov     ecx, OCS_HCR
        xor     edx, edx
        mov     bx, 2
        call    wait_reg
        jc      .dead

        ; HCCA of this controller, empty lists, no interrupts
        movzx   ax, byte [init_hc]
        shl     ax, 8
        add     ax, UD_HCCA
        call    ud_addr
        mov     ecx, eax
        mov     ax, OHCI_HCCA
        call    reg_wr
        xor     ecx, ecx
        mov     ax, OHCI_CTRLHEAD
        call    reg_wr
        mov     ax, OHCI_CTRLCUR
        call    reg_wr
        mov     ax, OHCI_BULKHEAD
        call    reg_wr
        mov     ax, OHCI_BULKCUR
        call    reg_wr
        mov     ecx, 0C000007Fh
        mov     ax, OHCI_INTDISABLE
        call    reg_wr
        mov     ecx, -1
        mov     ax, OHCI_INTSTATUS
        call    reg_wr

        ; frame interval, periodic start, operational
        mov     eax, [ohci_fi]
        sub     eax, 210
        imul    eax, eax, 6
        xor     edx, edx
        mov     ecx, 7
        div     ecx
        shl     eax, 16
        or      eax, [ohci_fi]
        or      eax, 80000000h          ; FIT
        mov     ecx, eax
        mov     ax, OHCI_FMINTERVAL
        call    reg_wr
        mov     eax, [ohci_fi]
        imul    eax, eax, 9
        xor     edx, edx
        mov     ecx, 10
        div     ecx
        mov     ecx, eax
        mov     ax, OHCI_PERSTART
        call    reg_wr
        mov     ecx, OC_CBSR3 | OC_OPER
        mov     ax, OHCI_CONTROL
        call    reg_wr

        ; root hub power
        mov     ax, OHCI_RHDESCA
        call    reg_rd
        mov     [ohci_rha], eax
        test    eax, 200h               ; no power switching
        jnz     .powered
        mov     ecx, 10000h             ; set global power
        mov     ax, OHCI_RHSTATUS
        call    reg_wr
        movzx   cx, byte [ohci_rha]
        xor     si, si
.pps:   jcxz    .ppw
        mov     ax, si
        shl     ax, 2
        add     ax, OHCI_RHPORT
        push    ecx
        mov     ecx, ORP_PPS
        call    reg_wr
        pop     ecx
        inc     si
        dec     cx
        jmp     .pps
.ppw:   mov     eax, [ohci_rha]         ; power on to power good
        shr     eax, 24
        shl     ax, 1                   ; 2 ms units
        mov     bx, 55
        xor     dx, dx
        div     bx
        inc     ax
        mov     bx, ax
        call    delay_ticks
.powered:
        mov     bx, 2                   ; devices settle after power up
        call    delay_ticks

        ; ports
        movzx   cx, byte [ohci_rha]
        cmp     cx, 15
        jbe     .np
        mov     cx, 15
.np:
        push    bx
        movzx   bx, byte [init_hc]
        mov     [hc_nports+bx], cl
        pop     bx
        xor     si, si
.port:  jcxz    .done
        push    cx
        push    si
        mov     [init_port], si
        call    ohci_port
        pop     si
        pop     cx
        inc     si
        dec     cx
        jmp     .port
.done:  ret
.dead:  cmp     byte [opt_quiet], 0
        jne     .done
        mov     si, msg_hc_dead
        call    print
        ret

; reset and enumerate root port [init_port] of controller [init_hc]
ohci_port:
        mov     ax, [init_port]
        shl     ax, 2
        add     ax, OHCI_RHPORT
        mov     [port_reg], ax
        call    reg_rd
        test    al, ORP_CCS
        jz      .out
        mov     ecx, ORP_PRS
        mov     ax, [port_reg]
        call    reg_wr
        mov     ax, [port_reg]
        mov     ecx, ORP_PRSC
        mov     edx, ecx
        mov     bx, 19
        call    wait_reg
        mov     ecx, ORP_CHANGES
        mov     ax, [port_reg]
        call    reg_wr
        mov     ax, [port_reg]
        call    reg_rd
        test    al, ORP_PES
        jz      .out
        xor     dl, dl                  ; full speed
        test    eax, ORP_LSDA
        jz      .speed
        inc     dl                      ; low speed
.speed: mov     [init_speed], dl
        mov     bx, 1                   ; reset recovery
        call    delay_ticks
        call    usb_enum_device
.out:   ret

init_hc         db      0
init_port       dw      0
init_speed      db      0
port_reg        dw      0
ohci_fi         dd      0
ohci_rha        dd      0

;-----------------------------------------------------------------------------
; Enumerate the device behind the port just reset (controller init_hc,
; speed init_speed): address, descriptors, configuration, mass storage
;-----------------------------------------------------------------------------
usb_enum_device:
        xor     bx, bx                  ; first free entry
.free:  cmp     bx, USB_MAX_DEV
        jae     .full
        cmp     byte [usbd_used+bx], 0
        je      .got
        inc     bx
        jmp     .free
.got:
        mov     al, [init_hc]
        mov     [usbd_hc+bx], al
        mov     al, [init_port]
        mov     [usbd_port+bx], al
        mov     al, [init_speed]
        mov     [usbd_speed+bx], al
        mov     byte [usbd_addr+bx], 0
        mov     byte [usbd_hub+bx], 0
        cmp     byte [init_tier], 0     ; on a root port
        jne     .topo
        mov     al, [init_port]
        mov     [init_rootport], al
        mov     dword [init_route], 0
        mov     word [init_tthub], 0
.topo:  mov     al, [init_tier]
        mov     [usbd_tier+bx], al
        mov     al, [init_rootport]
        mov     [usbd_rootport+bx], al
        mov     al, [init_tthub]
        mov     [usbd_tthub+bx], al
        mov     al, [init_ttport]
        mov     [usbd_ttport+bx], al
        mov     si, bx
        shl     si, 2
        mov     eax, [init_route]
        mov     [usbd_route+si], eax
        mov     si, bx
        add     si, si
        mov     word [usbd_mps0+si], 8
        call    is_xhci
        jne     .desc8
        call    xhci_new_dev            ; slot + address
        jc      .fail

.desc8: ; first 8 bytes of the device descriptor: max packet size of EP0
        mov     ax, 0680h
        mov     dx, 0100h
        xor     cx, cx
        mov     si, 8
        call    usb_control
        cmp     al, US_OK
        jne     .fail
        mov     si, [bounce_ptr]
        movzx   ax, byte [si+7]
        or      al, al
        jz      .fail
        mov     si, bx
        add     si, si
        mov     [usbd_mps0+si], ax
        call    is_xhci
        jne     .setaddr
        call    xhci_set_mps0
        jc      .fail
        jmp     .devdesc

.setaddr:
        ; SET_ADDRESS
        mov     al, bl
        inc     al
        add     al, [usb_addr_base]
        mov     [new_addr], al
        mov     ax, 0500h
        movzx   dx, byte [new_addr]
        xor     cx, cx
        xor     si, si
        call    usb_control
        cmp     al, US_OK
        jne     .fail
        mov     al, [new_addr]
        mov     [usbd_addr+bx], al
        push    bx
        mov     bx, 1
        call    delay_ticks
        pop     bx

.devdesc:
        ; whole device descriptor
        mov     ax, 0680h
        mov     dx, 0100h
        xor     cx, cx
        mov     si, 18
        call    usb_control
        cmp     al, US_OK
        jne     .fail
        mov     si, [bounce_ptr]
        mov     eax, [si+8]             ; vendor / product
        mov     [new_vidpid], eax
        mov     al, [si+4]              ; device class 9: hub
        mov     [new_class], al

        ; configuration descriptor: first the header, then all of it
        mov     ax, 0680h
        mov     dx, 0200h
        xor     cx, cx
        mov     si, 9
        call    usb_control
        cmp     al, US_OK
        jne     .fail
        mov     si, [bounce_ptr]
        mov     ax, [si+2]
        cmp     ax, 1024
        jbe     .tl
        mov     ax, 1024
.tl:    mov     [cfg_len], ax
        mov     si, ax
        mov     ax, 0680h
        mov     dx, 0200h
        xor     cx, cx
        call    usb_control
        cmp     al, US_OK
        jne     .fail
        cmp     byte [new_class], 09h
        je      .hub
        call    find_msc_interface
        jc      .notmsc
        call    is_xhci
        jne     .setcfg
        call    xhci_config_eps
        jc      .fail

.setcfg:
        ; SET_CONFIGURATION
        mov     si, [bounce_ptr]
        movzx   dx, byte [si+5]
        mov     ax, 0900h
        xor     cx, cx
        xor     si, si
        call    usb_control
        cmp     al, US_OK
        jne     .fail
        mov     byte [usbd_togin+bx], 0
        mov     byte [usbd_togout+bx], 0

        ; GET_MAX_LUN (a stall means one LUN; bulk-only devices only)
        mov     byte [usbd_maxlun+bx], 0
        cmp     byte [usbd_proto+bx], 50h
        jne     .onelun
        mov     ax, 0FEA1h
        xor     dx, dx
        movzx   cx, byte [usbd_iface+bx]
        mov     si, 1
        call    usb_control
        cmp     al, US_OK
        jne     .onelun
        mov     si, [bounce_ptr]
        mov     al, [si]
        cmp     al, 15
        ja      .onelun
        mov     [usbd_maxlun+bx], al
.onelun:
        call    usbd_take               ; entry BX stays in use

        ; one ASPI target per LUN
        xor     cl, cl
.lun:   cmp     cl, [usbd_maxlun+bx]
        ja      .out
        push    bx
        push    cx
        mov     ch, cl
        mov     cl, bl
        call    usb_add_target
        pop     cx
        pop     bx
        inc     cl
        jmp     .lun
.out:   ret

.hub:   mov     si, [bounce_ptr]        ; SET_CONFIGURATION
        movzx   dx, byte [si+5]
        mov     ax, 0900h
        xor     cx, cx
        xor     si, si
        call    usb_control
        cmp     al, US_OK
        jne     .fail
        mov     byte [usbd_hub+bx], 1
        call    usbd_take               ; entry BX stays in use
        jmp     usb_hub_scan

.notmsc:
        inc     byte [usb_addr_base]    ; keep its address in use
        call    xhci_drop_dev
        cmp     byte [opt_verbose], 0
        je      .out
        mov     si, msg_usb_other
        call    print_usb_dev
        ret
.fail:  call    xhci_drop_dev
        cmp     byte [opt_verbose], 0
        je      .out
        mov     si, msg_usb_fail
        call    print_usb_dev
        ret
.full:  mov     si, msg_too_many
        jmp     print

; mark USB device entry BX as used, usbd_count = highest used entry + 1
usbd_take:
        mov     byte [usbd_used+bx], 1
        push    ax
        mov     al, bl
        inc     al
        cmp     al, [usbd_count]
        jbe     .out
        mov     [usbd_count], al
.out:   pop     ax
        ret

; message SI for the device on [init_port]
print_usb_dev:
        push    si
        mov     si, msg_usbport
        call    print
        mov     ax, [init_port]
        call    print_dec
        mov     si, msg_vidpid
        call    print
        mov     ax, [new_vidpid]
        call    print_hex16
        mov     al, ':'
        call    putc
        mov     ax, [new_vidpid+2]
        call    print_hex16
        pop     si
        jmp     print

; mass storage interface (class 8, bulk-only protocol 50h) in the
; configuration descriptor in the bounce buffer -> usbd_iface / endpoints of
; device BX.  CF if there is none.
find_msc_interface:
        push    bx
        mov     si, [bounce_ptr]
        mov     di, si
        add     di, [cfg_len]           ; end
        mov     byte [usbd_epin+bx], 0
        mov     byte [usbd_epout+bx], 0
        mov     byte [usbd_epint+bx], 0
        mov     byte [usbd_togint+bx], 0
        xor     dl, dl                  ; inside a suitable interface?
        xor     dh, dh                  ; found one
.walk:  cmp     si, di
        jae     .end
        movzx   ax, byte [si]
        or      al, al
        jz      .end
        mov     cl, [si+1]
        cmp     cl, 4                   ; interface
        jne     .ep
        or      dh, dh                  ; the next interface ends ours
        jnz     .end
        xor     dl, dl
        cmp     byte [si+5], 08h        ; mass storage
        jne     .adv
        mov     cl, [si+7]              ; bulk-only, CBI, CB
        cmp     cl, 50h
        je      .proto
        cmp     cl, 01h
        ja      .adv
.proto: mov     [usbd_proto+bx], cl
        mov     cl, [si+6]
        mov     [usbd_sub+bx], cl
        mov     cl, [si+2]
        mov     [usbd_iface+bx], cl
        mov     dl, 1
        mov     dh, 1
        jmp     .adv
.ep:    cmp     cl, 5                   ; endpoint of our interface
        jne     .adv
        or      dl, dl
        jz      .adv
        mov     cl, [si+3]
        and     cl, 3
        cmp     cl, 3                   ; interrupt IN: CBI status
        je      .intr
        cmp     cl, 2                   ; bulk
        jne     .adv
        push    si
        mov     cl, [si+2]
        mov     cx, [si+4]
        and     cx, 07FFh
        push    cx
        mov     cl, [si+2]
        mov     si, bx
        add     si, si
        test    cl, 80h
        jz      .out_ep
        and     cl, 0Fh
        mov     [usbd_epin+bx], cl
        pop     word [usbd_mpsin+si]
        jmp     .ep_done
.out_ep:
        mov     [usbd_epout+bx], cl
        pop     word [usbd_mpsout+si]
.ep_done:
        pop     si
        jmp     .adv
.intr:  test    byte [si+2], 80h
        jz      .adv
        mov     cl, [si+2]
        and     cl, 0Fh
        mov     [usbd_epint+bx], cl
        push    si
        mov     cx, [si+4]
        and     cx, 07FFh
        mov     si, bx
        add     si, si
        mov     [usbd_mpsint+si], cx
        pop     si
.adv:   add     si, ax
        jmp     .walk
.end:   pop     bx
        cmp     byte [usbd_proto+bx], 00h ; CBI without interrupt pipe: CB
        jne     .eps
        cmp     byte [usbd_epint+bx], 0
        jne     .eps
        mov     byte [usbd_proto+bx], 01h
.eps:   cmp     byte [usbd_epin+bx], 0
        je      .none
        cmp     byte [usbd_epout+bx], 0
        je      .none
        clc
        ret
.none:  stc
        ret

; register LUN CH of USB device CL as the next ASPI target
usb_add_target:
        movzx   bx, byte [dev_count]
        cmp     byte [hot_slots], 0     ; hot plug: first empty slot
        je      .new
        xor     bx, bx
.slot:  cmp     bl, [dev_count]
        jae     .new
        cmp     byte [dev_trans+bx], TR_EMPTY
        je      .take
        inc     bx
        jmp     .slot
.new:   cmp     bx, MAX_DEV
        jae     .out
.take:  mov     di, bx
        add     di, di
        mov     [dev_port+di], cx
        mov     byte [dev_trans+bx], TR_USB
        mov     byte [dev_flags+bx], DF_CDB16
        mov     al, [init_port]
        mov     [dev_pnum+bx], al
        mov     byte [dev_type+bx], 1Fh
        mov     byte [id_model], 0
        call    inquiry_dev
        mov     si, [bounce_ptr]        ; vendor and product for the listing
        mov     di, id_model
        push    es
        push    cs
        pop     es
        add     si, 8
        mov     cx, 24
        rep     movsb
        pop     es
        mov     byte [di], 0
        call    probe_mode6
        cmp     bl, [dev_count]         ; a new target (not a slot)
        jb      .slotted
        inc     byte [dev_count]
.slotted:
        mov     al, [hot_active]        ; plugged in later: unit attention
        mov     [dev_ua+bx], al
        cmp     byte [opt_quiet], 0
        jne     .out
        call    print_dev
.out:   ret

; does target BX take MODE SENSE(6)?  If it rejects the opcode (sense
; 05/20, typical for ATAPI devices behind a USB bridge) MODE SENSE/SELECT(6)
; are translated to their 10 byte forms
probe_mode6:
        call    .clear
        mov     byte [cdb_buf], SCSI_MODE_SENSE6
        mov     byte [cdb_buf+2], 3Fh
        mov     byte [cdb_buf+4], 4
        mov     ecx, 4
        xor     dl, dl
        call    packet_bounce
        cmp     al, XR_CHECK
        jne     .out
        call    .clear
        mov     byte [cdb_buf], SCSI_REQUEST_SENSE
        mov     byte [cdb_buf+4], 18
        mov     ecx, 18
        xor     dl, dl
        call    packet_bounce
        cmp     al, XR_OK
        jne     .out
        mov     si, [bounce_ptr]
        mov     al, [si+2]
        and     al, 0Fh
        cmp     al, SENSE_ILLEGAL_REQUEST
        jne     .out
        cmp     byte [si+12], 20h       ; invalid command operation code
        jne     .out
        or      byte [dev_flags+bx], DF_MS10
.out:   ret
.clear: xor     eax, eax
        mov     [cdb_buf], eax
        mov     [cdb_buf+4], eax
        mov     [cdb_buf+8], eax
        mov     [cdb_buf+12], eax
        ret

usb_addr_base   db      0
new_addr        db      0
new_vidpid      dd      0
cfg_len         dw      0

msg_uhci        db      '  UHCI ', 0
msg_ohci        db      '  OHCI ', 0
msg_ehci        db      '  EHCI ', 0
msg_xhci        db      '  xHCI ', 0
msg_hc_at       db      'controller ', 0
msg_hc_later    db      ' (not supported yet)', 13, 10, 0
msg_hc_dead     db      '    controller does not respond', 13, 10, 0
msg_usbport     db      '    port ', 0
msg_vidpid      db      ': USB device ', 0
msg_usb_other   db      ' is not a bulk-only mass storage device', 13, 10, 0
msg_usb_fail    db      ' does not answer', 13, 10, 0
msg_xhci_skip   db      ' skipped (/X takes it over; a USB keyboard or mouse on it', 13, 10
                db      '    then only works again after a reboot)', 13, 10, 0
msg_xhci_one    db      '    too many xHCI controllers', 13, 10, 0
msg_xhci_scr    db      '    needs scratchpad memory from XMS (HIMEM.SYS)', 13, 10, 0
msg_companion   db      ': full / low speed device, handed to the companion', 13, 10, 0
