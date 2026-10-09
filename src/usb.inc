;=============================================================================
; usb.inc - USB transport of EUSBASPI.SYS (resident part)
;
; USB mass storage devices (bulk-only and CBI / CB transport, SCSI
; transparent / ATAPI / SFF-8070i / UFI command sets) on USB host controllers, polled, one
; transfer at a time.  The data of every transfer goes through one 4 KB page
; (the bounce buffer); the bulk-only data phase is a byte stream, so large
; requests are simply split into several bulk transfers.
;
; Host controllers: OHCI, EHCI, UHCI, xHCI (up to 4 xHCI controllers, /X).
; Every controller has its own registers, so any mix works (chipset
; UHCI/OHCI plus a PCI card with EHCI and companions, ...).
;=============================================================================

USB_MAX_HC      equ     12
USB_MAX_DEV     equ     10              ; mass storage devices and hubs

HC_OHCI         equ     1
HC_EHCI         equ     2
HC_UHCI         equ     3
HC_XHCI         equ     4

; result of a USB transfer
US_OK           equ     0
US_STALL        equ     1
US_TIMEOUT      equ     2
US_ERROR        equ     3

; OHCI registers
OHCI_REVISION   equ     00h
OHCI_CONTROL    equ     04h
OHCI_CMDSTATUS  equ     08h
OHCI_INTSTATUS  equ     0Ch
OHCI_INTDISABLE equ     14h
OHCI_HCCA       equ     18h
OHCI_CTRLHEAD   equ     20h
OHCI_CTRLCUR    equ     24h
OHCI_BULKHEAD   equ     28h
OHCI_BULKCUR    equ     2Ch
OHCI_FMINTERVAL equ     34h
OHCI_FMNUMBER   equ     3Ch
OHCI_PERSTART   equ     40h
OHCI_RHDESCA    equ     48h
OHCI_RHSTATUS   equ     50h
OHCI_RHPORT     equ     54h

OC_CBSR3        equ     0003h           ; control/bulk service ratio 4:1
OC_CLE          equ     0010h           ; control list enable
OC_BLE          equ     0020h           ; bulk list enable
OC_OPER         equ     0080h           ; HCFS = operational
OC_IR           equ     0100h           ; SMM driver owns the controller
OCS_HCR         equ     01h
OCS_CLF         equ     02h
OCS_BLF         equ     04h
OCS_OCR         equ     08h

ORP_CCS         equ     00000001h       ; port: device connected
ORP_PES         equ     00000002h       ; port enabled
ORP_PRS         equ     00000010h       ; port reset (write)
ORP_PPS         equ     00000100h       ; port power (write)
ORP_LSDA        equ     00000200h       ; low speed device
ORP_PRSC        equ     00100000h       ; port reset done
ORP_CHANGES     equ     001F0000h
ORP_CSC         equ     00010000h       ; connect status change

; EHCI operational registers (offsets from the capability length)
EHCI_USBCMD     equ     00h
EHCI_USBSTS     equ     04h
EHCI_USBINTR    equ     08h
EHCI_CTRLDSSEG  equ     10h
EHCI_ASYNCLIST  equ     18h
EHCI_CONFIGFLAG equ     40h
EHCI_PORTSC     equ     44h

EC_RS           equ     00000001h       ; run
EC_HCRESET      equ     00000002h
EC_ASE          equ     00000020h       ; async schedule enable
EC_ITC8         equ     00080000h       ; interrupt threshold 8 micro-frames
ES_HALTED       equ     00001000h
ES_ASS          equ     00008000h       ; async schedule status

EP_CCS          equ     00000001h       ; port: device connected
EP_CSC          equ     00000002h       ; connect status change
EP_PE           equ     00000004h       ; port enabled (high speed device)
EP_RWC          equ     0000002Ah       ; write-1-to-clear change bits
EP_PR           equ     00000100h       ; port reset
EP_LS_MASK      equ     00000C00h       ; line status
EP_LS_K         equ     00000400h       ; K state: low speed device
EP_PP           equ     00001000h       ; port power
EP_PO           equ     00002000h       ; port owner: companion controller

; EHCI qTD token
QT_ACTIVE       equ     00000080h
QT_HALTED       equ     00000040h
QT_ERRORS       equ     00000038h       ; buffer error, babble, transaction
QT_PID_OUT      equ     00000000h
QT_PID_IN       equ     00000100h
QT_PID_SETUP    equ     00000200h
QT_CERR3        equ     00000C00h
QT_DT           equ     80000000h

; UHCI registers (I/O ports)
UHCI_USBCMD     equ     00h
UHCI_USBSTS     equ     02h
UHCI_USBINTR    equ     04h
UHCI_FRNUM      equ     06h
UHCI_FRBASE     equ     08h
UHCI_SOFMOD     equ     0Ch
UHCI_PORTSC     equ     10h

UC_RS           equ     0001h
UC_HCRESET      equ     0002h
UC_GRESET       equ     0004h
UC_CF           equ     0040h
UC_MAXP         equ     0080h
US_HALTED       equ     0020h

UP_CCS          equ     0001h           ; port: device connected
UP_CSC          equ     0002h
UP_PE           equ     0004h
UP_PEC          equ     0008h
UP_LSDA         equ     0100h           ; low speed device
UP_PR           equ     0200h           ; port reset

; UHCI TD control / status
UT_ACTIVE       equ     00800000h
UT_STALLED      equ     00400000h
UT_ERRORS       equ     00360000h       ; buffer error, babble, CRC, bitstuff
UT_LS           equ     04000000h
UT_CERR3        equ     18000000h
UT_SPD          equ     20000000h       ; short packet detect
UPID_SETUP      equ     2Dh
UPID_IN         equ     69h
UPID_OUT        equ     0E1h

; OHCI transfer descriptor dword 0
OTD_ROUND       equ     00040000h       ; short packets are fine
OTD_SETUP       equ     00000000h
OTD_OUT         equ     00080000h
OTD_IN          equ     00100000h
OTD_NOINT       equ     00E00000h
OTD_DATA0       equ     02000000h
OTD_DATA1       equ     03000000h
OTD_CC_NONE     equ     0F0000000h      ; condition code "not accessed"

; USB DMA layout (offsets from a 4 KB aligned base in our segment)
UD_BOUNCE       equ     0000h           ; 4 KB data page
UD_HCCA         equ     1000h           ; OHCI HCCA, 256 bytes per controller
UD_ED           equ     1600h           ; OHCI endpoint descriptor
UD_TD           equ     1640h           ; 4 OHCI transfer descriptors
UD_SETUP        equ     1700h           ; setup packet (8 bytes)
UD_CBW          equ     1720h           ; command block wrapper (31 bytes)
UD_CSW          equ     1740h           ; command status wrapper (13 bytes)
UD_SCRATCH      equ     17C0h           ; dword for INT 15h register access
UD_QH           equ     1800h           ; EHCI queue head (32 byte aligned)
UD_QTD          equ     1840h           ; 3 EHCI qTDs (32 bytes each)
UD_UQH          equ     1900h           ; UHCI queue head (16 byte aligned)
UD_UTD          equ     1940h           ; UHCI transfer descriptors
UHCI_TDS        equ     64              ; 16 bytes each
UD_USED         equ     1D40h
; further 4 KB pages are handed out at init (alloc_page) and only stay
; resident when used: two UHCI frame lists, two xHCI pages

; xHCI: three pages from xh_page on, shared by all xHCI controllers
;   page 0: input context, then one area per controller (DCBAA, event ring
;           segment table, command ring, event ring)
;   page 1, 2: two devices each: device context (400h), then four transfer
;           rings of 8 TRBs (control, bulk out, bulk in, interrupt)
XHCI_MAX        equ     4               ; xHCI controllers
XHCI_DEV        equ     4               ; xHCI devices (all controllers)
XHCI_SLOTS      equ     8
XH_INCTX        equ     000h            ; input context (17 entries of 64)
XH_AREA0        equ     480h            ; controller areas
XH_AREA_SIZE    equ     2C0h
XH_DCBAA        equ     000h            ; (offsets in a controller area)
XH_ERST         equ     080h
XH_CMDR         equ     0C0h            ; command ring, 16 TRBs
XH_CMD_TRBS     equ     16
XH_EVTR         equ     1C0h            ; event ring, 16 TRBs
XH_EVT_TRBS     equ     16
XH_RING_TRBS    equ     8
XH_DEV_SIZE     equ     600h            ; device context + 4 rings

; xHCI registers
XO_USBCMD       equ     00h
XO_USBSTS       equ     04h
XO_CRCR         equ     18h
XO_DCBAAP       equ     30h
XO_CONFIG       equ     38h
XO_PORTSC       equ     400h
XR_IMAN         equ     20h             ; interrupter 0 (runtime registers)
XR_ERSTSZ       equ     28h
XR_ERSTBA       equ     30h
XR_ERDP         equ     38h
XP_KEEP         equ     4E00FFE9h       ; PORTSC bits written back unchanged
XP_CHANGES      equ     00FE0000h

; xHCI TRB types (<< 10)
XT_NORMAL       equ     1 << 10
XT_SETUP        equ     2 << 10
XT_DATA         equ     3 << 10
XT_STATUS       equ     4 << 10
XT_LINK         equ     6 << 10
XT_ENABLE_SLOT  equ     9 << 10
XT_DISABLE_SLOT equ     10 << 10
XT_ADDRESS_DEV  equ     11 << 10
XT_CONFIG_EP    equ     12 << 10
XT_EVAL_CTX     equ     13 << 10
XT_RESET_EP     equ     14 << 10
XT_STOP_EP      equ     15 << 10
XT_SET_DEQ      equ     16 << 10
XE_TRANSFER     equ     32
XE_COMMAND      equ     33
XC_SUCCESS      equ     1
XC_STALL        equ     6
XC_SHORT        equ     13
USB_BOUNCE      equ     1000h

;-----------------------------------------------------------------------------
; USB data
;-----------------------------------------------------------------------------
ud_base         dw      0               ; offset of the 4 KB aligned DMA base
phys_ud         dd      0               ; physical address of UD_HCCA page
usb_hc_count    db      0
hc_type         times USB_MAX_HC db 0
hc_base         times USB_MAX_HC dd 0   ; register base (MMIO or I/O port)
hc_bdf          times USB_MAX_HC dw 0
cur_hc          db      0FFh

usbd_count      db      0
usbd_used       times USB_MAX_DEV db 0  ; entry in use (hot plug reuses free ones)
hc_nports       times USB_MAX_HC db 0   ; root ports per controller
usbd_hc         times USB_MAX_DEV db 0
usbd_port       times USB_MAX_DEV db 0
usbd_addr       times USB_MAX_DEV db 0
ud_next         dw      2000h           ; next free DMA page (from ud_base)
phys_fl         dd      0               ; UHCI frame list: our QH everywhere
phys_fl_idle    dd      0               ; UHCI frame list: nothing

; xHCI
xh_page         dw      0               ; first xHCI page (from ud_base)
xh_phys         times 3 dd 0
xh_cur          db      0FFh            ; controller whose state is loaded
; state of the current xHCI controller (swapped by hc_select)
xh_state:
xh_op           dw      0               ; operational registers
xh_rt           dw      0               ; runtime registers
xh_db           dw      0               ; doorbells
xh_area         dw      0               ; its area in page 0
xh_cmd_st       dw      0100h           ; command ring: enqueue, cycle
xh_evt_deq      db      0               ; event ring: dequeue, cycle
xh_evt_cyc      db      1
xh_csh          db      5               ; context entry size: 1 << xh_csh
XH_STATE_SIZE   equ     $ - xh_state
xh_save         times USB_MAX_HC * XH_STATE_SIZE db 0
xh_ring_st      times XHCI_DEV * 4 dw 0100h
xh_map          times USB_MAX_DEV db 0  ; USB device -> xHCI device
xh_slot         times XHCI_DEV db 0
xh_droute       times XHCI_DEV dd 0     ; route string
xh_dtt          times XHCI_DEV dw 0     ; TT hub slot, port
xh_trb          times 4 dd 0
xh_ev           times 4 dd 0            ; the last event
xh_cmd_ptr      dd      0
xh_d            db      0               ; xhci_xfer: xHCI device
xh_dci          db      0               ;   endpoint (device context index)
xh_rst          dw      0               ;   ring state
xh_rbase        dw      0               ;   ring
xh_dptr         dd      0               ;   data TRB
xh_lptr         dd      0               ;   last TRB
xh_got          db      0
usbd_speed      times USB_MAX_DEV db 0  ; 0 full, 1 low, 2 high, 3 super
usbd_mps0       times USB_MAX_DEV dw 0
usbd_epin       times USB_MAX_DEV db 0
usbd_epout      times USB_MAX_DEV db 0
usbd_mpsin      times USB_MAX_DEV dw 0
usbd_mpsout     times USB_MAX_DEV dw 0
usbd_togin      times USB_MAX_DEV db 0
usbd_togout     times USB_MAX_DEV db 0
usbd_iface      times USB_MAX_DEV db 0
usbd_maxlun     times USB_MAX_DEV db 0
usbd_hub        times USB_MAX_DEV db 0  ; 1: a hub
usbd_tier       times USB_MAX_DEV db 0  ; hubs between the root port and it
usbd_rootport   times USB_MAX_DEV db 0
usbd_route      times USB_MAX_DEV dd 0  ; xHCI route string
usbd_tthub      times USB_MAX_DEV db 0  ; full / low speed behind a high speed
usbd_ttport     times USB_MAX_DEV db 0  ; hub: its address (xHCI: slot), port
usbd_sub        times USB_MAX_DEV db 0  ; interface subclass (command set)
usbd_proto      times USB_MAX_DEV db 0  ; 50h bulk-only, 00h CBI, 01h CB
usbd_epint      times USB_MAX_DEV db 0  ; CBI: interrupt IN endpoint
usbd_mpsint     times USB_MAX_DEV dw 0
usbd_togint     times USB_MAX_DEV db 0
usb_tag         dd      1

; parameters of usb_xfer
ux_dev          db      0               ; USB device index
ux_ep           db      0               ; endpoint number, 0 = control
ux_in           db      0               ; data direction: 1 = device to host
ux_phys         dd      0               ; data buffer (physical)
ux_len          dw      0               ; data length
ux_actual       dw      0               ; bytes transferred
ux_res          db      0

; debug marker on the QEMU debug console (test builds only)
%macro DBG 1
%ifdef E9LOG
        push    ax
        mov     al, %1
        out     0E9h, al
        pop     ax
%endif
%endmacro

;-----------------------------------------------------------------------------
; Short pause between two polls of a controller.  Tight MMIO polling loops
; keep emulators (QEMU) from running their timers - the frame counter and
; the PC timer then stand still - and gain nothing on real hardware.
;-----------------------------------------------------------------------------
poll_pause:
        push    cx
        mov     cx, 2000
.l:     loop    .l
        pop     cx
        ret

;-----------------------------------------------------------------------------
; Controller selection and register access (MMIO through reg_rd / reg_wr)
;-----------------------------------------------------------------------------
; AL = controller index
hc_select:
        cmp     al, [cur_hc]
        je      .out
        push    bx
        mov     [cur_hc], al
        movzx   bx, al
        shl     bx, 2
        push    eax
        mov     eax, [hc_base+bx]
        mov     [abar], eax
        pop     eax
        movzx   bx, al
        cmp     byte [hc_type+bx], HC_XHCI
        pop     bx
        jne     .out
        call    xh_switch
.out:   ret

; load the state of xHCI controller AL (keep the one loaded so far)
xh_switch:
        cmp     al, [xh_cur]
        je      .out
        push    cx
        push    si
        push    di
        push    es
        push    ds
        pop     es
        push    ax
        cmp     byte [xh_cur], 0FFh
        je      .load
        mov     al, [xh_cur]
        mov     ah, XH_STATE_SIZE
        mul     ah
        mov     di, xh_save
        add     di, ax
        mov     si, xh_state
        mov     cx, XH_STATE_SIZE
        rep     movsb
.load:  pop     ax
        push    ax
        mov     [xh_cur], al
        mov     ah, XH_STATE_SIZE
        mul     ah
        mov     si, xh_save
        add     si, ax
        mov     di, xh_state
        mov     cx, XH_STATE_SIZE
        rep     movsb
        pop     ax
        pop     es
        pop     di
        pop     si
        pop     cx
.out:   ret

; offset of DMA structure AX -> DI (in our segment), EAX = physical address
ud_addr:
        movzx   eax, ax
        mov     di, [ud_base]
        add     di, ax
        sub     eax, UD_HCCA            ; structures live in the second page
        add     eax, [phys_ud]
        ret

; wait for one more USB frame (1 ms) on the current OHCI controller
ohci_frame:
        push    eax
        push    bx
        push    esi
        mov     ax, OHCI_FMNUMBER
        call    reg_rd
        mov     bx, ax
        pushf
        sti
        call    get_ticks
        mov     esi, eax
.w:     call    poll_pause
        mov     ax, OHCI_FMNUMBER
        call    reg_rd
        cmp     ax, bx
        jne     .done
        call    get_ticks
        sub     eax, esi
        cmp     eax, 2
        jb      .w
.done:  popf
        pop     esi
        pop     bx
        pop     eax
        ret

;-----------------------------------------------------------------------------
; usb_xfer: one control or bulk transfer
;   ux_dev, ux_ep (0 = control: setup packet at UD_SETUP), ux_in, ux_phys,
;   ux_len -> AL = US_*, ux_actual
;-----------------------------------------------------------------------------
usb_xfer:
        push    bx
        movzx   bx, byte [ux_dev]
        mov     al, [usbd_hc+bx]
        call    hc_select
        movzx   bx, al
        mov     al, [hc_type+bx]
        pop     bx
        cmp     al, HC_OHCI
        je      ohci_xfer
        cmp     al, HC_EHCI
        je      ehci_xfer
        cmp     al, HC_UHCI
        je      uhci_xfer
        cmp     al, HC_XHCI
        je      xhci_xfer
        mov     al, US_ERROR
        ret

;-----------------------------------------------------------------------------
; OHCI transfer.  Control: SETUP, optional DATA, STATUS; bulk: one data TD
; (the controller splits it into packets).  The data toggle of bulk
; endpoints is kept per device and handed to the ED's toggle carry.
;-----------------------------------------------------------------------------
ohci_xfer:
        pushad
        mov     word [ux_actual], 0
        movzx   bx, byte [ux_dev]

        ; TD chain: TD0.. and an empty tail TD
        mov     ax, UD_TD
        call    ud_addr
        mov     esi, eax                ; physical address of TD0
        push    di
        mov     cx, 4 * 16 / 4          ; clear the TDs
        xor     eax, eax
.clr:   mov     [di], eax
        add     di, 4
        loop    .clr
        pop     di

        cmp     byte [ux_ep], 0
        jne     .bulk

        ; control: TD0 setup
        mov     dword [di], OTD_SETUP | OTD_NOINT | OTD_DATA0 | OTD_CC_NONE
        mov     ax, UD_SETUP
        push    di
        call    ud_addr
        pop     di
        mov     [di+4], eax
        add     eax, 7
        mov     [di+12], eax
        lea     eax, [esi+16]
        mov     [di+8], eax
        add     di, 16
        cmp     word [ux_len], 0
        je      .status
        mov     eax, OTD_ROUND | OTD_NOINT | OTD_DATA1 | OTD_CC_NONE | OTD_OUT
        cmp     byte [ux_in], 0
        je      .cdout
        mov     eax, OTD_ROUND | OTD_NOINT | OTD_DATA1 | OTD_CC_NONE | OTD_IN
.cdout: mov     [di], eax
        call    .databuf
        lea     eax, [esi+32]
        mov     [di+8], eax
        add     di, 16
.status:
        ; status stage: opposite direction of the data stage, DATA1
        mov     eax, OTD_NOINT | OTD_DATA1 | OTD_CC_NONE | OTD_IN
        cmp     word [ux_len], 0
        je      .st
        cmp     byte [ux_in], 0
        je      .st
        mov     eax, OTD_NOINT | OTD_DATA1 | OTD_CC_NONE | OTD_OUT
.st:    mov     [di], eax
        mov     ax, di
        sub     ax, [ud_base]
        sub     ax, UD_TD
        movzx   eax, ax
        add     eax, esi
        add     eax, 16                 ; next = tail
        mov     [di+8], eax
        mov     edx, eax                ; tail
        xor     ecx, ecx                ; toggle carry irrelevant
        jmp     .ed

.bulk:  mov     eax, OTD_ROUND | OTD_NOINT | OTD_CC_NONE | OTD_OUT
        cmp     byte [ux_in], 0
        je      .bout
        mov     eax, OTD_ROUND | OTD_NOINT | OTD_CC_NONE | OTD_IN
.bout:  mov     [di], eax               ; toggle from the ED
        call    .databuf
        lea     edx, [esi+16]           ; tail
        mov     [di+8], edx
        movzx   ecx, byte [usbd_togout+bx]
        cmp     byte [ux_in], 0
        je      .ed
        movzx   ecx, byte [usbd_togin+bx]

.ed:    ; endpoint descriptor: address, endpoint, speed, max packet size
        push    edx
        mov     ax, UD_ED
        call    ud_addr
        mov     ebp, eax                ; physical address of the ED
        pop     edx
        movzx   eax, byte [usbd_addr+bx]
        movzx   ebx, byte [ux_ep]
        shl     ebx, 7
        or      eax, ebx
        movzx   bx, byte [ux_dev]
        cmp     byte [usbd_speed+bx], 1
        jne     .fs
        or      eax, 1 << 13            ; low speed
.fs:    push    bx
        push    si
        mov     si, bx
        add     si, si
        movzx   ebx, word [usbd_mps0+si]
        cmp     byte [ux_ep], 0
        je      .mps
        movzx   ebx, word [usbd_mpsout+si]
        cmp     byte [ux_in], 0
        je      .mps
        movzx   ebx, word [usbd_mpsin+si]
.mps:   shl     ebx, 16
        or      eax, ebx
        pop     si
        pop     bx
        mov     [di], eax
        mov     [di+4], edx             ; tail
        mov     eax, esi
        and     cl, 1
        shl     cl, 1
        or      al, cl                  ; toggle carry
        mov     [di+8], eax             ; head
        mov     dword [di+12], 0

        ; put it on the control or bulk list and start the list
        mov     ecx, ebp
        mov     ax, OHCI_CTRLHEAD
        mov     si, OHCI_CTRLCUR
        mov     dx, OC_CLE | (OCS_CLF << 8)
        cmp     byte [ux_ep], 0
        je      .list
        mov     ax, OHCI_BULKHEAD
        mov     si, OHCI_BULKCUR
        mov     dx, OC_BLE | (OCS_BLF << 8)
.list:  call    reg_wr
        mov     ax, si
        xor     ecx, ecx
        call    reg_wr
        mov     ax, OHCI_CONTROL
        call    reg_rd
        mov     ecx, eax
        or      cl, dl
        mov     ax, OHCI_CONTROL
        call    reg_wr
        movzx   ecx, dh
        mov     ax, OHCI_CMDSTATUS
        call    reg_wr

        ; wait until the head reaches the tail or the ED halts
        mov     ecx, [tmo_ticks]
        cmp     byte [ux_ep], 0
        jne     .poll0
        mov     ecx, 3 * 18             ; control: 3 s
.poll0: pushf
        sti
        call    get_ticks
        push    eax
.poll:  call    poll_pause
        mov     eax, [di+8]
        test    al, 1
        jnz     .halted
        and     al, 0F0h
        cmp     eax, [di+4]
        je      .done
        call    get_ticks
        sub     eax, [esp]
        cmp     eax, ecx
        jb      .poll
        pop     eax
        popf
        mov     byte [ux_res], US_TIMEOUT
        jmp     .stop
.halted:
        pop     eax
        popf
        mov     byte [ux_res], US_ERROR
        jmp     .stop
.done:  pop     eax
        popf
        mov     byte [ux_res], US_OK

.stop:  ; take the list down again and let the controller finish the frame
        mov     ax, OHCI_CONTROL
        call    reg_rd
        mov     ecx, eax
        and     cl, ~(OC_CLE | OC_BLE)
        mov     ax, OHCI_CONTROL
        call    reg_wr
        call    ohci_frame
        call    ohci_frame
        mov     eax, [di+8]             ; save the bulk data toggle
        shr     al, 1
        and     al, 1
        movzx   bx, byte [ux_dev]
        cmp     byte [ux_ep], 0
        je      .result
        cmp     byte [ux_in], 0
        je      .togo
        mov     [usbd_togin+bx], al
        jmp     .result
.togo:  mov     [usbd_togout+bx], al

.result:
        ; condition codes of the TDs: STALL, short data, bytes moved
        mov     ax, UD_TD
        call    ud_addr
        mov     cx, 3
.td:    mov     eax, [di]
        shr     eax, 28
        cmp     al, 0Fh                 ; not processed
        je      .next
        cmp     al, 4
        jne     .nostall
        mov     byte [ux_res], US_STALL
.nostall:
        or      al, al
        jz      .next
        cmp     byte [ux_res], US_OK
        jne     .next
        mov     byte [ux_res], US_ERROR
.next:  test    dword [di], OTD_IN | OTD_OUT     ; data TD: count the bytes
        jz      .skip
        cmp     dword [di+12], 0
        je      .skip
        mov     eax, [di+12]            ; buffer end
        mov     edx, [di+4]             ; current pointer, 0 when complete
        or      edx, edx
        jz      .full
        sub     edx, [ux_phys]
        mov     [ux_actual], dx
        jmp     .skip
.full:  mov     dx, [ux_len]
        mov     [ux_actual], dx
.skip:  add     di, 16
        loop    .td
        popad
        mov     al, [ux_res]
        ret

; data buffer of the TD at DI: ux_phys, ux_len
.databuf:
        mov     eax, [ux_phys]
        mov     [di+4], eax
        movzx   edx, word [ux_len]
        add     eax, edx
        dec     eax
        mov     [di+12], eax
        ret

;-----------------------------------------------------------------------------
; EHCI transfer.  One queue head that links to itself is the whole async
; schedule while the transfer runs; the schedule is switched off again
; afterwards, so the structures can simply be rebuilt for the next one.
; Data toggles come from the qTDs (DTC = 1); the bulk toggle is kept per
; device like with OHCI.  Only high speed devices are run on EHCI: full and
; low speed ones were handed to the companion controller.
;-----------------------------------------------------------------------------
ehci_xfer:
        pushad
        mov     word [ux_actual], 0
        movzx   bx, byte [ux_dev]
        mov     ax, UD_QH               ; clear QH and qTDs
        call    ud_addr
        mov     ebp, eax                ; physical address of the QH
        push    di
        mov     cx, (UD_QTD + 3 * 32 - UD_QH) / 4
        xor     eax, eax
.clr:   mov     [di], eax
        add     di, 4
        loop    .clr
        pop     di

        ; queue head
        mov     eax, ebp
        or      al, 2                   ; horizontal link: this QH
        mov     [di], eax
        movzx   eax, byte [usbd_addr+bx]
        movzx   edx, byte [ux_ep]
        shl     edx, 8
        or      eax, edx
        or      eax, (1 << 14) | (1 << 15) ; DTC, head of the list
        movzx   edx, byte [usbd_speed+bx] ; 0 full, 1 low, 2 high: as EHCI
        shl     edx, 12
        or      eax, edx
        cmp     byte [usbd_speed+bx], 2
        je      .hs
        cmp     byte [ux_ep], 0
        jne     .hs
        or      eax, 1 << 27            ; full / low speed control endpoint
.hs:
        mov     si, bx
        add     si, si
        movzx   edx, word [usbd_mps0+si]
        cmp     byte [ux_ep], 0
        je      .mps
        movzx   edx, word [usbd_mpsout+si]
        cmp     byte [ux_in], 0
        je      .mps
        movzx   edx, word [usbd_mpsin+si]
.mps:   shl     edx, 16
        or      eax, edx
        mov     [di+4], eax
        movzx   eax, byte [usbd_ttport+bx] ; split transactions through
        shl     eax, 23                 ; the hub's transaction translator
        movzx   edx, byte [usbd_tthub+bx]
        shl     edx, 16
        or      eax, edx
        or      eax, 1 << 30            ; one transaction per micro-frame
        mov     [di+8], eax
        mov     ax, UD_QTD
        push    di
        call    ud_addr
        pop     di
        mov     esi, eax                ; physical address of qTD0
        mov     [di+16], eax            ; overlay: next qTD
        mov     dword [di+20], 1        ; alternate next: none

        mov     ax, UD_QTD
        call    ud_addr                 ; DI = qTD0
        cmp     byte [ux_ep], 0
        jne     .bulk

        ; control: SETUP, optional DATA, STATUS
        lea     eax, [esi+32]
        mov     [di], eax
        mov     dword [di+4], 1
        mov     dword [di+8], (8 << 16) | QT_PID_SETUP | QT_CERR3 | QT_ACTIVE
        push    di
        mov     ax, UD_SETUP
        call    ud_addr
        pop     di
        mov     [di+12], eax
        add     di, 32
        mov     ecx, 32                 ; offset of the status qTD
        cmp     word [ux_len], 0
        je      .status
        lea     eax, [esi+64]
        mov     [di], eax
        mov     [di+4], eax             ; short packet: go on with STATUS
        movzx   eax, word [ux_len]
        shl     eax, 16
        or      eax, QT_CERR3 | QT_ACTIVE | QT_DT
        cmp     byte [ux_in], 0
        je      .cdo
        or      eax, QT_PID_IN
.cdo:   mov     [di+8], eax
        mov     eax, [ux_phys]
        mov     [di+12], eax
        add     di, 32
        mov     ecx, 64
.status:
        mov     dword [di], 1
        mov     dword [di+4], 1
        mov     eax, QT_PID_IN | QT_CERR3 | QT_ACTIVE | QT_DT
        cmp     word [ux_len], 0
        je      .st
        cmp     byte [ux_in], 0
        je      .st
        mov     eax, QT_PID_OUT | QT_CERR3 | QT_ACTIVE | QT_DT
.st:    mov     [di+8], eax
        mov     [ehci_last], cx
        jmp     .run

.bulk:  mov     dword [di], 1
        mov     dword [di+4], 1
        movzx   eax, word [ux_len]
        shl     eax, 16
        or      eax, QT_CERR3 | QT_ACTIVE
        mov     dl, [usbd_togout+bx]
        cmp     byte [ux_in], 0
        je      .bo
        or      eax, QT_PID_IN
        mov     dl, [usbd_togin+bx]
.bo:    test    dl, 1
        jz      .bt
        or      eax, QT_DT
.bt:    mov     [di+8], eax
        mov     eax, [ux_phys]
        mov     [di+12], eax
        mov     word [ehci_last], 0

.run:   ; the QH is the whole async schedule while the transfer runs
        mov     ecx, ebp
        mov     ax, EHCI_ASYNCLIST
        call    reg_wr
        mov     ax, EHCI_USBCMD
        call    reg_rd
        mov     ecx, eax
        or      cl, EC_ASE
        mov     ax, EHCI_USBCMD
        call    reg_wr
        push    bx
        mov     ax, EHCI_USBSTS
        mov     ecx, ES_ASS
        mov     edx, ecx
        mov     bx, 3
        call    wait_reg
        pop     bx

        mov     ax, UD_QTD
        call    ud_addr
        add     di, [ehci_last]         ; last qTD of the transfer
        mov     ecx, [tmo_ticks]
        cmp     byte [ux_ep], 0
        jne     .poll0
        mov     ecx, 3 * 18
.poll0: pushf
        sti
        call    get_ticks
        push    eax
.poll:  call    poll_pause
        test    dword [di+8], QT_ACTIVE
        jz      .done
        push    di                      ; halted on the way?
        mov     ax, UD_QH
        call    ud_addr
        test    dword [di+24], QT_HALTED
        pop     di
        jnz     .halted
        call    get_ticks
        sub     eax, [esp]
        cmp     eax, ecx
        jb      .poll
        pop     eax
        popf
        mov     byte [ux_res], US_TIMEOUT
        jmp     .stop
.halted:
        pop     eax
        popf
        mov     byte [ux_res], US_ERROR
        jmp     .stop
.done:  pop     eax
        popf
        mov     byte [ux_res], US_OK

.stop:  mov     ax, EHCI_USBCMD         ; schedule off again
        call    reg_rd
        mov     ecx, eax
        and     cl, ~EC_ASE
        mov     ax, EHCI_USBCMD
        call    reg_wr
        push    bx
        mov     ax, EHCI_USBSTS
        mov     ecx, ES_ASS
        xor     edx, edx
        mov     bx, 3
        call    wait_reg
        pop     bx

        ; results: stall / errors, bytes moved, bulk toggle
        mov     ax, UD_QTD
        call    ud_addr
        mov     cx, 3
.qtd:   mov     eax, [di+8]
        test    eax, QT_HALTED
        jz      .nohalt
        mov     byte [ux_res], US_STALL
        test    eax, QT_ERRORS
        jz      .nohalt
        mov     byte [ux_res], US_ERROR
.nohalt:
        mov     edx, eax                ; data stage: count the bytes
        and     edx, 300h
        cmp     edx, QT_PID_SETUP
        je      .next
        cmp     dword [di+12], 0
        je      .next
        test    eax, QT_ACTIVE
        jnz     .next
        shr     eax, 16
        and     ax, 7FFFh               ; bytes not moved
        mov     dx, [ux_len]
        sub     dx, ax
        mov     [ux_actual], dx
        cmp     byte [ux_ep], 0
        je      .next
        mov     eax, [di+8]             ; toggle after the transfer
        shr     eax, 31
        cmp     byte [ux_in], 0
        je      .togo
        mov     [usbd_togin+bx], al
        jmp     .next
.togo:  mov     [usbd_togout+bx], al
.next:  add     di, 32
        loop    .qtd
        popad
        mov     al, [ux_res]
        ret

ehci_last       dw      0

;-----------------------------------------------------------------------------
; UHCI transfer.  All 1024 frame list entries point to one queue head; a
; transfer hangs its TD chain into the QH element pointer.  UHCI needs one
; TD per packet, so transfers are run in batches of up to UHCI_TDS packets;
; the phases (SETUP, DATA, STATUS) and the data toggle carry over.
;-----------------------------------------------------------------------------
uhci_xfer:
        pushad
        mov     eax, [phys_fl]          ; only this controller sees our QH
        call    uhci_set_fl
        mov     word [ux_actual], 0
        mov     byte [ux_res], US_OK
        movzx   bx, byte [ux_dev]
        mov     si, bx                  ; max packet size of this endpoint
        add     si, si
        mov     ax, [usbd_mps0+si]
        cmp     byte [ux_ep], 0
        je      .mps
        mov     ax, [usbd_mpsout+si]
        cmp     byte [ux_in], 0
        je      .mps
        mov     ax, [usbd_mpsin+si]
.mps:   or      ax, ax
        jnz     .mpsok
        mov     ax, 8
.mpsok: mov     [uh_mps], ax
        mov     ax, [ux_len]
        mov     [uh_rem], ax
        mov     word [uh_off], 0
        mov     byte [uh_phase], 1      ; bulk: data
        mov     al, [usbd_togout+bx]
        cmp     byte [ux_in], 0
        je      .tog
        mov     al, [usbd_togin+bx]
.tog:   mov     [uh_tog], al
        cmp     byte [ux_ep], 0
        jne     .batch
        mov     byte [uh_phase], 0      ; control: setup first
        mov     byte [uh_tog], 1        ; data stage starts with DATA1

.batch: cmp     byte [uh_phase], 3
        jae     .finish
        ; token base: device address, endpoint, low speed bit
        movzx   edx, byte [usbd_addr+bx]
        shl     edx, 8
        movzx   eax, byte [ux_ep]
        shl     eax, 15
        or      edx, eax
        mov     [uh_token], edx
        mov     dword [uh_ctl], UT_CERR3 | UT_ACTIVE
        cmp     byte [usbd_speed+bx], 1
        jne     .fs
        or      dword [uh_ctl], UT_LS
.fs:    mov     ax, UD_UTD
        call    ud_addr
        mov     [uh_td0], eax
        xor     cx, cx                  ; TDs in this batch
.build: cmp     cx, UHCI_TDS
        jae     .run
        mov     al, [uh_phase]
        cmp     al, 3
        jae     .run
        cmp     al, 0
        je      .setup
        cmp     al, 1
        je      .data
        ; status: zero length, DATA1, opposite direction of the data stage
        mov     edx, UPID_IN
        cmp     word [ux_len], 0
        je      .stpid
        cmp     byte [ux_in], 0
        je      .stpid
        mov     edx, UPID_OUT
.stpid: or      edx, 7FFh << 21 | 1 << 19
        xor     eax, eax
        mov     byte [uh_phase], 3
        call    .td
        jmp     .build
.setup: push    di
        mov     ax, UD_SETUP
        call    ud_addr
        pop     di
        mov     edx, UPID_SETUP | (7 << 21)
        mov     byte [uh_phase], 1
        cmp     word [ux_len], 0
        jne     .sutd
        mov     byte [uh_phase], 2
.sutd:  call    .td
        jmp     .build
.data:  mov     ax, [uh_rem]
        cmp     ax, [uh_mps]
        jbe     .chunk
        mov     ax, [uh_mps]
.chunk: movzx   edx, ax                 ; packet length - 1 in bits 31..21
        dec     edx
        and     edx, 7FFh
        shl     edx, 21
        or      edx, UPID_OUT
        cmp     byte [ux_in], 0
        je      .dpid
        xor     dl, UPID_OUT ^ UPID_IN
.dpid:  test    byte [uh_tog], 1
        jz      .dtog
        or      edx, 1 << 19
.dtog:  push    ax
        movzx   eax, word [uh_off]
        add     eax, [ux_phys]
        call    .td
        pop     ax
        add     [uh_off], ax
        sub     [uh_rem], ax
        xor     byte [uh_tog], 1
        cmp     word [uh_rem], 0
        jne     .build
        mov     byte [uh_phase], 2      ; control: status next
        cmp     byte [ux_ep], 0
        je      .build
        mov     byte [uh_phase], 3      ; bulk: done
        jmp     .build

.run:   mov     [uh_count], cx
        or      cx, cx
        jnz     .go
        jmp     .finish
.go:    mov     ax, cx                  ; last TD: end of the chain
        dec     ax
        shl     ax, 4
        add     ax, UD_UTD
        call    ud_addr
        mov     dword [di], 1
        mov     ax, UD_UQH              ; hang the chain into the queue head
        call    ud_addr
        mov     ecx, [uh_td0]
        mov     [di+4], ecx
        mov     ecx, [tmo_ticks]
        cmp     byte [ux_ep], 0
        jne     .poll0
        mov     ecx, 3 * 18
.poll0: pushf
        sti
        call    get_ticks
        push    eax
.poll:  call    poll_pause
        call    .stopped
        jnc     .bdone
        call    get_ticks
        sub     eax, [esp]
        cmp     eax, ecx
        jb      .poll
        pop     eax
        popf
        mov     byte [ux_res], US_TIMEOUT
        call    .unhook
        jmp     .finish
.bdone: pop     eax
        popf
        call    .unhook

        ; results of the batch: stall / errors, bytes, short packets, toggle
        mov     ax, UD_UTD
        call    ud_addr
        mov     cx, [uh_count]
.chk:   mov     eax, [di+4]
        test    eax, UT_ACTIVE
        jnz     .short                  ; not run: the transfer stopped early
        test    eax, UT_STALLED
        jz      .nost
        mov     byte [ux_res], US_STALL
        jmp     .finish
.nost:  test    eax, UT_ERRORS
        jz      .noerr
        mov     byte [ux_res], US_ERROR
        jmp     .finish
.noerr: mov     edx, [di+8]
        cmp     dl, UPID_SETUP
        je      .next
        mov     ebp, edx
        shr     ebp, 21                 ; maximum length - 1
        inc     bp
        and     bp, 7FFh
        jz      .next                   ; zero length: status stage
        inc     ax                      ; actual length - 1 -> actual
        and     ax, 7FFh
        add     [ux_actual], ax
        shr     edx, 19                 ; toggle of the next packet
        and     dl, 1
        xor     dl, 1
        mov     [uh_tog], dl
        cmp     ax, bp
        jb      .short                  ; short packet: data stage over
.next:  add     di, 16
        loop    .chk
        jmp     .batch
.short: cmp     byte [ux_ep], 0         ; bulk: done
        jne     .finish
        cmp     byte [uh_phase], 1      ; control: status stage still to do?
        jb      .finish
        mov     dl, [di+8]
        cmp     dl, UPID_SETUP
        je      .finish
        mov     edx, [di+8]             ; short status packet: done
        shr     edx, 21
        cmp     dx, 7FFh
        je      .finish
        mov     word [uh_rem], 0
        mov     byte [uh_phase], 2
        jmp     .batch

.finish:
        cmp     byte [ux_ep], 0         ; keep the bulk toggle
        je      .out
        mov     al, [uh_tog]
        cmp     byte [ux_in], 0
        je      .togo
        mov     [usbd_togin+bx], al
        jmp     .out
.togo:  mov     [usbd_togout+bx], al
.out:   mov     eax, [phys_fl_idle]
        call    uhci_set_fl
        popad
        mov     al, [ux_res]
        ret

; CF clear when the queue has stopped: element pointer terminated, or it
; points at a TD that is no longer active (stall, error, short packet)
.stopped:
        push    eax
        push    di
        mov     ax, UD_UQH
        call    ud_addr
        mov     eax, [di+4]
        test    al, 1
        jnz     .st_yes
        and     eax, ~0Fh
        sub     eax, [uh_td0]
        cmp     eax, UHCI_TDS * 16
        jae     .st_no
        add     ax, UD_UTD
        call    ud_addr
        test    dword [di+4], UT_ACTIVE
        jz      .st_yes
.st_no: stc
        jmp     .st_ret
.st_yes:
        clc
.st_ret:
        pop     di
        pop     eax
        ret

; append TD number CX (incremented): EDX = token without address, EAX = buffer
.td:    push    di
        push    eax
        mov     ax, cx
        inc     ax
        shl     ax, 4
        add     ax, UD_UTD
        call    ud_addr                 ; next TD: link target, depth first
        or      al, 4
        sub     di, 16
        mov     [di], eax
        mov     eax, [uh_ctl]
        cmp     dl, UPID_IN
        jne     .tdo
        or      eax, UT_SPD             ; IN: stop on short packets
.tdo:   or      eax, 7FFh               ; actual length: none yet
        mov     [di+4], eax
        or      edx, [uh_token]
        mov     [di+8], edx
        pop     eax
        mov     [di+12], eax
        inc     cx
        pop     di
        ret

; take the chain out of the QH and give the controller a frame to let go
.unhook:
        push    di
        mov     ax, UD_UQH
        call    ud_addr
        mov     dword [di+4], 1
        pop     di
        jmp     uhci_frame

; wait for the next UHCI frame (1 ms)
uhci_frame:
        push    eax
        push    dx
        push    bx
        push    esi
        mov     dx, UHCI_FRNUM
        call    uhci_port_addr
        in      ax, dx
        mov     bx, ax
        pushf
        sti
        call    get_ticks
        mov     esi, eax
.w:     call    poll_pause
        in      ax, dx
        cmp     ax, bx
        jne     .done
        call    get_ticks
        sub     eax, esi
        cmp     eax, 2
        jb      .w
.done:  popf
        pop     esi
        pop     bx
        pop     dx
        pop     eax
        ret

; frame list EAX for the current UHCI controller.  All UHCI controllers
; run on the empty list; the one with a transfer gets the list with our QH.
uhci_set_fl:
        push    dx
        mov     dx, UHCI_FRBASE
        call    uhci_port_addr
        out     dx, eax
        pop     dx
        ret

; DX = UHCI register -> DX = I/O port of the current controller
uhci_port_addr:
        add     dx, [abar]
        ret

uh_mps          dw      0
uh_rem          dw      0
uh_off          dw      0
uh_phase        db      0               ; 0 setup, 1 data, 2 status, 3 done
uh_tog          db      0
uh_token        dd      0
uh_ctl          dd      0
uh_td0          dd      0
uh_count        dw      0

;-----------------------------------------------------------------------------
; xHCI.  One controller; its rings and contexts live in two DMA pages.
; Each device has three transfer rings (control, bulk out, bulk in) of 8
; TRBs; commands and events go through rings of 16.  Completion is polled
; on the event ring.
;-----------------------------------------------------------------------------
; AX = offset in the xHCI pages -> DI = pointer, EAX = physical address
xa:
        movzx   eax, ax
        mov     di, [ud_base]
        add     di, [xh_page]
        add     di, ax
        push    bx
        mov     bx, ax
        shr     bx, 12
        shl     bx, 2
        and     ax, 0FFFh
        add     eax, [xh_phys+bx]
        pop     bx
        ret

; transfer ring AX (xHCI device * 4 + ring) -> AX = offset in the pages
xh_ringoff:
        push    dx
        push    ax
        shr     ax, 2
        call    xh_devoff
        pop     dx
        and     dx, 3
        shl     dx, 7
        add     ax, dx
        add     ax, 400h
        pop     dx
        ret

; xHCI device AX -> AX = offset of its device context
xh_devoff:
        push    dx
        mov     dx, ax
        shr     ax, 1
        inc     ax
        shl     ax, 12
        test    dl, 1
        jz      .even
        add     ax, XH_DEV_SIZE
.even:  pop     dx
        ret

xop_rd: add     ax, [xh_op]
        jmp     reg_rd
xop_wr: add     ax, [xh_op]
        jmp     reg_wr
xrt_wr: add     ax, [xh_rt]
        jmp     reg_wr

xh_clr_trb:
        mov     dword [xh_trb], 0
        mov     dword [xh_trb+4], 0
        mov     dword [xh_trb+8], 0
        mov     dword [xh_trb+12], 0
        ret

; next event -> xh_ev; ECX = timeout in ticks, CF on timeout
xh_event:
        pushad
        pushf
        sti
        call    get_ticks
        mov     edx, eax
.w:     movzx   ax, byte [xh_evt_deq]
        shl     ax, 4
        add     ax, XH_EVTR
        add     ax, [xh_area]
        call    xa
        mov     al, [di+12]
        and     al, 1
        cmp     al, [xh_evt_cyc]
        je      .got
        call    poll_pause
        call    get_ticks
        sub     eax, edx
        cmp     eax, ecx
        jb      .w
        popf
        popad
        stc
        ret
.got:   mov     eax, [di]
        mov     [xh_ev], eax
        mov     eax, [di+4]
        mov     [xh_ev+4], eax
        mov     eax, [di+8]
        mov     [xh_ev+8], eax
        mov     eax, [di+12]
        mov     [xh_ev+12], eax
        inc     byte [xh_evt_deq]
        cmp     byte [xh_evt_deq], XH_EVT_TRBS
        jb      .erdp
        mov     byte [xh_evt_deq], 0
        xor     byte [xh_evt_cyc], 1
.erdp:  movzx   ax, byte [xh_evt_deq]   ; tell the controller how far we are
        shl     ax, 4
        add     ax, XH_EVTR
        add     ax, [xh_area]
        call    xa
        mov     ecx, eax
        or      cl, 8                   ; clear event handler busy
        mov     ax, XR_ERDP
        call    xrt_wr
        xor     ecx, ecx
        mov     ax, XR_ERDP + 4
        call    xrt_wr
        popf
        popad
        clc
        ret

; TRB type of the event in xh_ev -> AL
xh_ev_type:
        mov     eax, [xh_ev+12]
        shr     eax, 10
        and     al, 3Fh
        ret

; put xh_trb on a ring: BX = ring state (enqueue, cycle), SI = ring offset,
; CL = TRBs in the ring (the last one is the link back) -> EAX = physical
; address of the TRB
xh_put:
        push    edx
        push    di
        movzx   ax, byte [bx]
        shl     ax, 4
        add     ax, si
        call    xa
        push    eax
        mov     eax, [xh_trb]
        mov     [di], eax
        mov     eax, [xh_trb+4]
        mov     [di+4], eax
        mov     eax, [xh_trb+8]
        mov     [di+8], eax
        mov     eax, [xh_trb+12]
        and     al, ~1
        or      al, [bx+1]              ; cycle bit: the TRB is valid now
        mov     [di+12], eax
        inc     byte [bx]
        mov     al, [bx]
        inc     al
        cmp     al, cl
        jb      .done
        mov     ax, si                  ; link TRB back to the start
        call    xa
        mov     edx, eax
        movzx   ax, byte [bx]
        shl     ax, 4
        add     ax, si
        call    xa
        mov     [di], edx
        mov     dword [di+4], 0
        mov     dword [di+8], 0
        mov     eax, XT_LINK | 2        ; toggle cycle
        or      al, [bx+1]
        mov     [di+12], eax
        mov     byte [bx], 0
        xor     byte [bx+1], 1
.done:  pop     eax
        pop     di
        pop     edx
        ret

; run the command in xh_trb -> AL = completion code (0FFh: no answer)
xh_command:
        push    bx
        push    si
        push    ecx
        mov     bx, xh_cmd_st
        mov     si, XH_CMDR
        add     si, [xh_area]
        mov     cl, XH_CMD_TRBS
        call    xh_put
        mov     [xh_cmd_ptr], eax
        xor     ecx, ecx                ; doorbell 0: command ring
        mov     ax, [xh_db]
        call    reg_wr
.wait:  mov     ecx, 2 * 18
        call    xh_event
        jc      .none
        call    xh_ev_type
        cmp     al, XE_COMMAND
        jne     .wait
        mov     eax, [xh_ev]
        cmp     eax, [xh_cmd_ptr]
        jne     .wait
        mov     al, [xh_ev+11]
        jmp     .out
.none:  mov     al, 0FFh
.out:   pop     ecx
        pop     si
        pop     bx
        ret

xhci_xfer:
        pushad
        mov     word [ux_actual], 0
        mov     byte [xh_got], 0
        mov     byte [ux_res], US_OK
        movzx   bx, byte [ux_dev]
        movzx   bx, byte [xh_map+bx]
        mov     [xh_d], bl
        xor     cx, cx                  ; ring 0 and DCI 1: control
        mov     ah, 1
        cmp     byte [ux_ep], 0
        je      .dci
        mov     ah, [ux_ep]
        shl     ah, 1
        inc     cx                      ; ring 1: bulk out
        cmp     byte [ux_in], 0
        je      .dci
        inc     ah
        inc     cx                      ; ring 2: bulk in
        push    bx
        movzx   bx, byte [ux_dev]
        mov     al, [usbd_epint+bx]
        pop     bx
        cmp     al, [ux_ep]
        jne     .dci
        inc     cx                      ; ring 3: interrupt in
.dci:   mov     [xh_dci], ah
        mov     ax, bx
        shl     ax, 2
        add     ax, cx
        push    ax
        shl     ax, 1
        add     ax, xh_ring_st
        mov     [xh_rst], ax
        pop     ax
        call    xh_ringoff
        mov     [xh_rbase], ax

        cmp     byte [ux_ep], 0
        jne     .bulk
        mov     ax, UD_SETUP            ; setup stage: the 8 bytes inline
        call    ud_addr
        mov     eax, [di]
        mov     [xh_trb], eax
        mov     eax, [di+4]
        mov     [xh_trb+4], eax
        mov     dword [xh_trb+8], 8
        mov     eax, XT_SETUP | 40h     ; immediate data
        cmp     word [ux_len], 0
        je      .st0
        or      eax, 2 << 16            ; OUT data stage
        cmp     byte [ux_in], 0
        je      .st0
        or      eax, 3 << 16            ; IN data stage
.st0:   mov     [xh_trb+12], eax
        call    .put
        cmp     word [ux_len], 0
        je      .status
        mov     eax, [ux_phys]          ; data stage
        mov     [xh_trb], eax
        mov     dword [xh_trb+4], 0
        movzx   eax, word [ux_len]
        mov     [xh_trb+8], eax
        mov     eax, XT_DATA | 4        ; event on short packets
        cmp     byte [ux_in], 0
        je      .dt
        or      eax, 1 << 16
.dt:    mov     [xh_trb+12], eax
        call    .put
        mov     [xh_dptr], eax
.status:
        call    xh_clr_trb
        mov     eax, XT_STATUS | 20h | (1 << 16) ; IN, event when done
        cmp     word [ux_len], 0
        je      .sd
        cmp     byte [ux_in], 0
        je      .sd
        and     eax, ~(1 << 16)
.sd:    mov     [xh_trb+12], eax
        call    .put
        mov     [xh_lptr], eax
        jmp     .ring
.bulk:  mov     eax, [ux_phys]
        mov     [xh_trb], eax
        mov     dword [xh_trb+4], 0
        movzx   eax, word [ux_len]
        mov     [xh_trb+8], eax
        mov     dword [xh_trb+12], XT_NORMAL | 20h | 4
        call    .put
        mov     [xh_lptr], eax
        mov     [xh_dptr], eax

.ring:  movzx   bx, byte [xh_d]         ; doorbell of the slot: the endpoint
        movzx   ax, byte [xh_slot+bx]
        shl     ax, 2
        add     ax, [xh_db]
        movzx   ecx, byte [xh_dci]
        call    reg_wr
        mov     ecx, [tmo_ticks]
        cmp     byte [ux_ep], 0
        jne     .ev
        mov     ecx, 3 * 18
.ev:    call    xh_event
        jc      .timeout
        call    xh_ev_type
        cmp     al, XE_TRANSFER
        jne     .ev
        movzx   bx, byte [xh_d]
        mov     al, [xh_ev+15]          ; slot
        cmp     al, [xh_slot+bx]
        jne     .ev
        mov     al, [xh_ev+14]          ; endpoint
        and     al, 1Fh
        cmp     al, [xh_dci]
        jne     .ev
        mov     al, [xh_ev+11]          ; completion code
        cmp     al, XC_SUCCESS
        je      .ok
        cmp     al, XC_SHORT
        je      .ok
        mov     byte [ux_res], US_STALL
        cmp     al, XC_STALL
        je      .recover
        mov     byte [ux_res], US_ERROR
        jmp     .recover
.ok:    mov     eax, [xh_ev]
        cmp     eax, [xh_dptr]
        jne     .last
        cmp     byte [xh_got], 0
        jne     .last
        cmp     byte [xh_ev+11], XC_SHORT ; success: all of it, whatever the
        jne     .last                   ; residue says (Etron)
        mov     byte [xh_got], 1        ; data TRB: bytes not moved
        mov     eax, [xh_ev+8]
        and     eax, 0FFFFFFh
        mov     dx, [ux_len]
        sub     dx, ax
        mov     [ux_actual], dx
.last:  mov     eax, [xh_ev]
        cmp     eax, [xh_lptr]
        jne     .ev
        cmp     byte [xh_got], 0
        jne     .out
        mov     ax, [ux_len]            ; no short packet: all of it
        mov     [ux_actual], ax
        jmp     .out

.timeout:
        mov     byte [ux_res], US_TIMEOUT
        call    xh_clr_trb              ; stop the endpoint
        mov     eax, XT_STOP_EP
        call    .epcmd
        jmp     .deq
.recover:
        call    xh_clr_trb              ; halted: reset the endpoint
        mov     eax, XT_RESET_EP
        call    .epcmd
.deq:   call    xh_clr_trb              ; and go on behind everything queued
        mov     bx, [xh_rst]
        movzx   ax, byte [bx]
        shl     ax, 4
        add     ax, [xh_rbase]
        call    xa
        or      al, [bx+1]
        mov     [xh_trb], eax
        mov     eax, XT_SET_DEQ
        call    .epcmd
.out:   popad
        mov     al, [ux_res]
        ret

; EAX = command type: for our slot and endpoint
.epcmd: movzx   bx, byte [xh_d]
        movzx   edx, byte [xh_slot+bx]
        shl     edx, 24
        or      eax, edx
        movzx   edx, byte [xh_dci]
        shl     edx, 16
        or      eax, edx
        mov     [xh_trb+12], eax
        jmp     xh_command

.put:   mov     bx, [xh_rst]
        mov     si, [xh_rbase]
        mov     cl, XH_RING_TRBS
        jmp     xh_put

;-----------------------------------------------------------------------------
; Control transfer: BX = device, setup packet in AL (request type),
; AH (request), DX (value), CX (index), SI (length); data through the bounce
; buffer.  -> AL = US_*
;-----------------------------------------------------------------------------
usb_control:
        push    di
        push    eax
        mov     ax, UD_SETUP
        call    ud_addr
        pop     eax
        mov     [di], ax
        mov     [di+2], dx
        mov     [di+4], cx
        mov     [di+6], si
        mov     [ux_dev], bl
        mov     byte [ux_ep], 0
        and     al, 80h
        rol     al, 1
        mov     [ux_in], al
        mov     eax, [phys_bounce]
        mov     [ux_phys], eax
        mov     [ux_len], si
        call    usb_xfer
        pop     di
        ret

; CLEAR_FEATURE(ENDPOINT_HALT) for endpoint address AL of device BX
usb_clear_halt:
        push    cx
        push    dx
        push    si
        movzx   cx, al
        mov     ax, 0102h               ; endpoint, CLEAR_FEATURE
        xor     dx, dx
        xor     si, si
        push    cx
        call    usb_control
        pop     cx
        test    cl, 80h                 ; the toggle starts again at DATA0
        jz      .out
        mov     byte [usbd_togin+bx], 0
        jmp     .done
.out:   mov     byte [usbd_togout+bx], 0
.done:  pop     si
        pop     dx
        pop     cx
        ret

; mass storage reset recovery for device BX: bulk-only reset, or the CBI
; command block reset (SEND DIAGNOSTIC 1D 04 FF FF ...); then clear halts
usb_reset_recovery:
        push    cx
        push    dx
        push    si
        cmp     byte [usbd_proto+bx], 50h
        je      .bot
        push    di
        mov     di, [bounce_ptr]
        mov     dword [di], 0FFFF041Dh
        mov     dword [di+4], 0FFFFFFFFh
        mov     dword [di+8], 0FFFFFFFFh
        pop     di
        mov     ax, 0021h               ; ADSC
        xor     dx, dx
        movzx   cx, byte [usbd_iface+bx]
        mov     si, 12
        call    usb_control
        jmp     .halts
.bot:   mov     ax, 0FF21h              ; class, interface: BOMS reset
        xor     dx, dx
        movzx   cx, byte [usbd_iface+bx]
        xor     si, si
        call    usb_control
.halts:
        mov     al, [usbd_epin+bx]
        or      al, 80h
        call    usb_clear_halt
        mov     al, [usbd_epout+bx]
        call    usb_clear_halt
        pop     si
        pop     dx
        pop     cx
        ret

; bulk transfer on device BX: AL = 1 for IN, EDX = physical buffer, CX = len
; -> AL = US_*, ux_actual
usb_bulk:
        mov     [ux_dev], bl
        mov     [ux_in], al
        mov     [ux_phys], edx
        mov     [ux_len], cx
        mov     ah, [usbd_epout+bx]
        or      al, al
        jz      .ep
        mov     ah, [usbd_epin+bx]
.ep:    and     ah, 0Fh
        mov     [ux_ep], ah
        jmp     usb_xfer

;-----------------------------------------------------------------------------
; usb_scsi: run the SCSI command in cdb_buf on ASPI target BX with the
; bulk-only transport.  Data: [x_bufp] / [x_rem] / [x_write] like ide_packet.
; Returns AL = XR_*, x_xfer, x_over; x_tfd = 0.
;-----------------------------------------------------------------------------
usb_scsi:
        pushad
        push    es
        mov     dword [x_tfd], 0
        mov     dword [x_xfer], 0
        mov     byte [x_over], 0
        mov     byte [x_res], XR_OK
        mov     ax, [x_bufp]            ; normalized client pointer
        mov     di, ax
        mov     es, [x_bufp+2]
        call    advance_bufp
        mov     si, bx
        add     si, si
        mov     ax, [dev_port+si]       ; AL = USB device, AH = LUN
        movzx   bx, al
        mov     [usb_lun], ah
        cmp     byte [usbd_proto+bx], 50h
        je      .cbw

        ; CBI / CB: the command goes out on EP0 (ADSC)
        mov     al, [cdb_buf]
        call    usb_cdb_len
        movzx   cx, al
        push    es
        push    cs
        pop     es
        mov     di, [bounce_ptr]
        mov     si, cdb_buf
        push    cx
        rep     movsb
        pop     si                      ; length
        pop     es
        mov     ax, 0021h               ; class, interface: ADSC
        xor     dx, dx
        movzx   cx, byte [usbd_iface+bx]
        call    usb_control
        cmp     al, US_OK
        je      .data
        cmp     al, US_STALL            ; command refused
        jne     .fail
        mov     byte [x_res], XR_CHECK
        jmp     .out

.cbw:   ; command block wrapper
        mov     ax, UD_CBW
        call    ud_addr
        mov     dword [di], 43425355h   ; "USBC"
        mov     eax, [usb_tag]
        inc     dword [usb_tag]
        mov     [di+4], eax
        mov     eax, [x_rem]
        mov     [di+8], eax
        mov     [usb_xlen], eax
        xor     al, al
        cmp     dword [x_rem], 0
        je      .flags
        cmp     byte [x_write], 0
        jne     .flags
        mov     al, 80h
.flags: mov     [di+12], al
        mov     al, [usb_lun]
        mov     [di+13], al
        mov     al, [cdb_buf]           ; command length
        call    usb_cdb_len
        mov     [di+14], al
        push    di
        mov     si, cdb_buf
        add     di, 15
        mov     cx, 16
        push    es
        push    cs
        pop     es
        rep     movsb
        pop     es
        pop     di
        mov     ax, UD_CBW
        call    ud_addr
        mov     edx, eax
        xor     al, al
        mov     cx, 31
        call    usb_bulk
        cmp     al, US_OK
        je      .data
        cmp     al, US_STALL
        jne     .fail
        call    usb_reset_recovery
        jmp     .fail

.data:  ; data phase in pieces of up to one page
        cmp     dword [x_rem], 0
        je      .status
        mov     ecx, [x_rem]
        cmp     ecx, USB_BOUNCE
        jbe     .chunk
        mov     ecx, USB_BOUNCE
.chunk: cmp     byte [x_write], 0
        je      .in
        push    cx                      ; client -> bounce
        push    ds
        mov     di, [bounce_ptr]
        push    cs
        pop     es
        lds     si, [cs:x_bufp]
        rep     movsb
        pop     ds
        pop     cx
        mov     edx, [phys_bounce]
        xor     al, al
        call    usb_bulk
        jmp     .moved
.in:    mov     edx, [phys_bounce]
        mov     al, 1
        call    usb_bulk
        push    ax
        movzx   ecx, word [ux_actual]   ; bounce -> client
        push    cx
        les     di, [x_bufp]
        mov     si, [bounce_ptr]
        rep     movsb
        pop     cx
        pop     ax
.moved: movzx   ecx, word [ux_actual]
        add     [x_xfer], ecx
        sub     [x_rem], ecx
        les     di, [x_bufp]            ; advance the client pointer
        add     di, cx
        call    advance_bufp
        cmp     al, US_OK
        je      .more
        cmp     al, US_STALL
        jne     .fail
        mov     al, [usbd_epout+bx]     ; stalled data phase: clear it,
        cmp     byte [x_write], 0       ; then the CSW tells the result
        jne     .clr
        mov     al, [usbd_epin+bx]
        or      al, 80h
.clr:   call    usb_clear_halt
        cmp     byte [usbd_proto+bx], 50h
        je      .status
        mov     byte [x_res], XR_CHECK  ; CBI: the command failed
        jmp     .out
.more:  cmp     byte [x_write], 0       ; short packet ends the data phase
        jne     .data
        cmp     cx, [ux_len]
        jb      .status
        jmp     .data

.status:
        cmp     byte [usbd_proto+bx], 50h
        je      .bot
        cmp     byte [usbd_proto+bx], 00h
        jne     .out                    ; CB: no status, no stall = good
        call    cbi_status
        jmp     .out
.bot:   mov     byte [usb_csw_try], 2
.csw:   mov     ax, UD_CSW
        call    ud_addr
        mov     dword [di], 0
        mov     edx, eax
        mov     al, 1
        mov     cx, 13
        call    usb_bulk
        cmp     al, US_OK
        je      .check
        cmp     al, US_STALL
        jne     .fail
        mov     al, [usbd_epin+bx]
        or      al, 80h
        call    usb_clear_halt
        dec     byte [usb_csw_try]
        jnz     .csw
        jmp     .fail
.check: mov     ax, UD_CSW
        call    ud_addr
        cmp     dword [di], 53425355h   ; "USBS"
        jne     .phase
        mov     eax, [usb_tag]
        dec     eax
        cmp     [di+4], eax
        jne     .phase
        mov     eax, [di+8]             ; residue: what the device did not move
        cmp     eax, [usb_xlen]
        ja      .resid
        mov     edx, [usb_xlen]
        sub     edx, eax
        mov     [x_xfer], edx
.resid: mov     al, [di+12]
        or      al, al
        jz      .out
        cmp     al, 1
        jne     .phase
        mov     byte [x_res], XR_CHECK  ; sense data via REQUEST SENSE
        jmp     .out
.phase: call    usb_reset_recovery
.fail:  mov     byte [x_res], XR_HBA
        cmp     al, US_TIMEOUT
        jne     .out
        mov     byte [x_res], XR_TIMEOUT
.out:   pop     es
        popad
        mov     al, [x_res]
        ret

; CBI status from the interrupt endpoint of device BX -> x_res
cbi_status:
        mov     [ux_dev], bl
        mov     al, [usbd_epint+bx]
        mov     [ux_ep], al
        mov     byte [ux_in], 1
        mov     ax, UD_CSW
        call    ud_addr
        mov     word [di], 0
        mov     [ux_phys], eax
        mov     word [ux_len], 2
        mov     si, bx                  ; the transfer code takes toggle and
        add     si, si                  ; packet size of the bulk IN pipe
        mov     al, [usbd_togint+bx]
        xchg    al, [usbd_togin+bx]
        mov     [usbd_togint+bx], al
        mov     ax, [usbd_mpsint+si]
        xchg    ax, [usbd_mpsin+si]
        mov     [usbd_mpsint+si], ax
        call    usb_xfer
        mov     dl, [usbd_togint+bx]
        xchg    dl, [usbd_togin+bx]
        mov     [usbd_togint+bx], dl
        mov     dx, [usbd_mpsint+si]
        xchg    dx, [usbd_mpsin+si]
        mov     [usbd_mpsint+si], dx
        cmp     al, US_OK
        je      .got
        cmp     al, US_STALL
        jne     .hba
        mov     al, [usbd_epint+bx]
        or      al, 80h
        call    usb_clear_halt
        mov     byte [x_res], XR_CHECK
        ret
.hba:   mov     byte [x_res], XR_HBA
        ret
.got:   mov     ax, UD_CSW
        call    ud_addr
        mov     ax, [di]
        cmp     byte [usbd_sub+bx], 04h ; UFI: ASC / ASCQ
        jne     .cbi
        mov     dl, [cdb_buf]           ; these two leave the status alone
        cmp     dl, SCSI_INQUIRY
        je      .good
        cmp     dl, SCSI_REQUEST_SENSE
        je      .good
        or      ax, ax
        jz      .good
        mov     byte [x_res], XR_CHECK
        ret
.cbi:   or      al, al                  ; type 0: status in bits 1..0
        jnz     .hba
        and     ah, 3
        jz      .good
        cmp     ah, 1
        jne     .hba
        mov     byte [x_res], XR_CHECK
.good:  ret

; command length to send for opcode AL of the device BX -> AL.  UFI,
; SFF-8070i and ATAPI devices always take 12 bytes.
usb_cdb_len:
        push    dx
        mov     dl, [usbd_sub+bx]
        cmp     dl, 02h
        je      .twelve
        cmp     dl, 04h
        je      .twelve
        cmp     dl, 05h
        je      .twelve
        pop     dx
        jmp     cdb_length
.twelve:
        call    cdb_length
        cmp     al, 12
        jae     .out
        mov     al, 12
.out:   pop     dx
        ret

; SCSI command length from the opcode group in AL -> AL
cdb_length:
        shr     al, 5
        cmp     al, 0
        je      .six
        cmp     al, 3
        jb      .ten                    ; groups 1, 2
        cmp     al, 4
        je      .sixteen
        mov     al, 12
        ret
.six:   mov     al, 6
        ret
.ten:   mov     al, 10
        ret
.sixteen:
        mov     al, 16
        ret

; SRB reset for USB target BX
usb_reset:
        push    bx
        push    si
        mov     si, bx
        add     si, si
        movzx   bx, byte [dev_port+si]
        call    usb_reset_recovery
        pop     si
        pop     bx
        clc
        ret

usb_lun         db      0
usb_xlen        dd      0               ; data length of the current command
usb_csw_try     db      0
