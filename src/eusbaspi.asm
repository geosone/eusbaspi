;=============================================================================
; EUSBASPI.SYS - Extended USB ASPI manager for DOS
;
; ASPI manager (SCSIMGR$) for USB mass storage devices on UHCI, OHCI, EHCI
; and xHCI host controllers.  Works alone or together with other ASPI
; managers (AHCIASPI.SYS, ASPI8DOS.SYS, ...): it chains to a manager loaded
; before it, and managers loaded after it can chain to it.
;
; The ASPI layer started as a copy of AHCIASPI.SYS; the USB transport is in
; usb.inc (resident) and usbinit.inc (initialisation).  The AHCI/IDE code of
; the copy is still assembled but not used (USB_BUILD).
;
; Build:  nasm -f bin -Isrc/ -o EUSBASPI.SYS src/eusbaspi.asm
;
; CONFIG.SYS:
;   DEVICE=EUSBASPI.SYS [/H[:n]] [/ID:name] [/M:F|B] [/T:s] [/X] [/Q] [/V]
;
;   /H[:n]  hot plugging: n slots (default 2, ASPI targets 0..n-1) that get
;           drive letters from ASPIDISK even when empty; sticks can be
;           plugged in and out while DOS runs (root ports, see hotplug.inc).
;           The driver then stays larger (enumeration code resident).
;
; Windows 3.1x enhanced mode: OHCI, EHCI and xHCI need EUSBVXD.386 in
; SYSTEM.INI [386Enh] (device=C:\EUSBVXD.386) for their memory mapped
; registers; UHCI works without it.
;=============================================================================
%define USB_BUILD

        cpu     386
        bits    16
        org     0

%include "defs.inc"
%include "aspi.inc"

%define VERSION_STR '0.3'

NPRD            equ     8               ; PRDT entries per command
BOUNCE_SIZE     equ     800h            ; bounce buffer (2 KB, 2 KB aligned)
STACK_SIZE      equ     0C00h           ; BIOS IRQ handlers run on it too
MAX_DEV         equ     7               ; targets 0..6, adapter is ID 7
HA_TARGET_ID    equ     7

; DMA area layout (offsets from a 2 KB aligned base)
DMA_USED        equ     0B00h
%ifdef USB_BUILD
DMA_RESERVE     equ     8000h           ; 7 pages (usb.inc) + 4 KB alignment
%else
DMA_RESERVE     equ     DMA_USED + 800h ; including alignment slack
%endif

; result codes of ahci_exec
XR_OK           equ     0
XR_CHECK        equ     1               ; device reported an error
XR_TIMEOUT      equ     2
XR_HBA          equ     3               ; HBA / interface error

; register access methods
AM_FLAT         equ     2
AM_BIOS         equ     3
AM_NONE         equ     0FFh

; device transports
TR_AHCI         equ     0
TR_IDE          equ     1
TR_USB          equ     2
TR_EMPTY        equ     3               ; hot plug slot without a device

; IDE task file register offsets
MAX_IDE_CH      equ     10

; device flags
DF_DMA          equ     01h
DF_CDB16        equ     04h
DF_MS10         equ     08h             ; MODE SENSE/SELECT(6) unknown: use 10

; data mapping modes

; translation modes
XL_NONE         equ     0
XL_MODESENSE    equ     1
XL_MODESELECT   equ     2

; DOS request header
RQ_CMD          equ     02h
RQ_STATUS       equ     03h
RQ_XFER         equ     0Eh
RQ_COUNT        equ     12h
RQ_END          equ     0Eh             ; init: end of resident part
RQ_CMDLINE      equ     12h             ; init: command line

; VDS
VDS_FLAG_SEG    equ     40h
VDS_FLAG_OFS    equ     7Bh
VDS_FLAG_BIT    equ     20h

;=============================================================================
; Resident part
;=============================================================================

hdr_next        dd      -1
hdr_attr        dw      0C000h          ; character device, IOCTL supported
hdr_strat       dw      strategy
hdr_intr        dw      interrupt
hdr_name        db      'SCSIMGR$'

req_ptr         dd      0

;-----------------------------------------------------------------------------
; DOS device driver entry points
;-----------------------------------------------------------------------------
strategy:
        mov     [cs:req_ptr], bx
        mov     [cs:req_ptr+2], es
        retf

interrupt:
        pushf
        push    ax
        push    bx
        push    di
        push    ds
        push    es
        lds     bx, [cs:req_ptr]
        mov     al, [bx+RQ_CMD]
        or      al, al
        jnz     .not_init
        call    word [cs:init_vec]
        jmp     .exit
.not_init:
        cmp     al, 3                   ; IOCTL input: return entry point
        je      .ioctl_in
        cmp     al, 0Dh                 ; open
        je      .done
        cmp     al, 0Eh                 ; close
        je      .done
        mov     word [bx+RQ_STATUS], 8103h ; unknown command
        jmp     .exit
.ioctl_in:
        les     di, [bx+RQ_XFER]
        mov     word [es:di], aspi_entry
        mov     [es:di+2], cs
        mov     word [bx+RQ_COUNT], 4
.done:
        mov     word [bx+RQ_STATUS], 0100h
.exit:
        pop     es
        pop     ds
        pop     di
        pop     bx
        pop     ax
        popf
        retf

init_vec        dw      init

;-----------------------------------------------------------------------------
; Resident data
;-----------------------------------------------------------------------------
%ifdef USB_BUILD
mgr_id          db      'EUSBASPI        '      ; 16 bytes, /ID: overrides
ha_ident        db      'USB Mass Storage'      ; 16 bytes
%else
mgr_id          db      'AHCIASPI        '      ; 16 bytes, /ID: overrides
ha_ident        db      'AHCI SATA       '      ; 16 bytes
%endif
ha_unique       dw      0001h           ; buffer alignment mask (word)
                db      0               ; reserved
                db      MAX_DEV + 1     ; max targets
                dd      10000h          ; max transfer length
                times 8 db 0

acc_method      db      AM_NONE
rd_vec          dw      0               ; register read routine
wr_vec          dw      0               ; register write routine
abar            dd      0               ; physical address of the HBA regs

vds_ok          db      0               ; VDS present
a20_fixed       db      0               ; flat mode: A20 enabled by us
xms_entry       dd      0

tmo_ticks       dd      15 * 182 / 10   ; command timeout in BIOS ticks
inq_scsi2       db      1               ; report CD/DVD drives as SCSI-2 (/RAWINQ)

; DMA area pointers (offsets in our segment) and physical addresses
bounce_ptr      dw      0
scratch_ptr     dw      0
phys_bounce     dd      0
phys_scratch    dd      0

; device table
dev_count       db      0
dev_port        times MAX_DEV dw 0      ; port register base (100h + n*80h)
dev_flags       times MAX_DEV db 0
dev_type        times MAX_DEV db 0
dev_pnum        times MAX_DEV db 0
dev_trans       times MAX_DEV db 0      ; TR_AHCI / TR_IDE
dev_sel         times MAX_DEV db 0      ; IDE: A0h master, B0h slave
dev_ua          times MAX_DEV db 0      ; unit attention pending (hot plug)

; hot plug (/H, see hotplug.inc)
hot_slots       db      0               ; 0: off, else number of slots
hot_active      db      0               ; set after init: no screen output
hot_last        dd      0               ; tick count of the last port check

; IDE transfer state
x_bufp          dd      0               ; PIO data pointer
x_rem           dd      0               ; PIO bytes left in the buffer

; previously loaded ASPI manager (chaining)
prev_entry      dd      0
prev_count      db      0               ; its host adapters = our HA number

; ASPI call state
in_aspi         db      0
old_ss          dw      0
old_sp          dw      0
srb_ptr         dd      0
post_ptr        dd      0

; port state saved while we own a port

; command context for ahci_exec
x_write         db      0
x_tfd           dd      0               ; PxTFD after the command
x_res           db      0
x_xfer          dd      0               ; bytes actually transferred
x_over          db      0               ; device wanted to move more data
cdb_buf         times 16 db 0

; SCSI request state
s_dev           dw      0               ; device index
s_len           dd      0               ; data length from the SRB
s_buf           dd      0               ; data buffer (far pointer)
s_dir           db      0               ; 0 none, 1 in, 2 out
s_xlate         db      0
s_blen          dw      0               ; bytes used in the bounce buffer
s_alloc6        dw      0               ; MODE SENSE(6) allocation length
s_msel_bdonly   db      0               ; MODE SELECT(6) without mode pages
s_xfer          dd      0               ; x_xfer / x_over of the request
s_over          db      0

; VDS descriptors (DDS for lock, EDDS for scatter/gather lock)
dds:
dds_size        dd      0
dds_off         dd      0
dds_seg         dw      0
dds_bufid       dw      0
dds_phys        dd      0


; GDT for INT 15h AH=87h
gdt15           times 16 db 0
gdt15_src       dw      0FFFFh, 0
                db      0, 93h, 0, 0
gdt15_dst       dw      0FFFFh, 0
                db      0, 93h, 0, 0
                times 16 db 0

; GDT for flat real mode
flat_gdtr       dw      15
                dd      0               ; linear address of flat_gdt
flat_gdt        dq      0
                dq      008F92000000FFFFh ; 4 GB data segment
old_gdtr        dw      0
                dd      0

;-----------------------------------------------------------------------------
; HBA register access
;   reg_rd: AX = register offset          -> EAX = value
;   reg_wr: AX = register offset, ECX = value (EAX destroyed)
;-----------------------------------------------------------------------------
reg_rd:
        jmp     word [cs:rd_vec]

; Windows 3.x enhanced mode: through the V86 API of EUSBVXD.386
EUSBVXD_ID      equ     3BD5h
vxd_entry       dd      0
rd_vxd: push    edx
        movzx   edx, ax
        add     edx, [cs:abar]
        mov     ax, 1                   ; read dword at physical EDX
        call    far [cs:vxd_entry]
        pop     edx
        ret
wr_vxd: push    edx
        movzx   edx, ax
        add     edx, [cs:abar]
        mov     ax, 2                   ; write ECX to physical EDX
        call    far [cs:vxd_entry]
        pop     edx
        ret

reg_wr:
        jmp     word [cs:wr_vec]

; The 4 GB limit of FS is set again for every access, with interrupts off
; until the access is done: BIOS interrupt handlers that switch to protected
; mode (SeaBIOS does) leave 64 KB limits behind, and the next access would
; fault forever.
rd_flat:
        push    esi
        push    fs
        pushf
        cli
        call    flat_enable
        movzx   esi, ax
        add     esi, [cs:abar]
        xor     ax, ax
        mov     fs, ax
        mov     eax, [fs:esi]
        popf
        pop     fs
        pop     esi
        ret

wr_flat:
        push    esi
        push    fs
        pushf
        cli
        call    flat_enable
        movzx   esi, ax
        add     esi, [cs:abar]
        xor     ax, ax
        mov     fs, ax
        mov     [fs:esi], ecx
        popf
        pop     fs
        pop     esi
        ret

; INT 15h AH=87h: copy one dword between the HBA and our scratch dword
rd_bios:
        push    di
        movzx   eax, ax
        add     eax, [cs:abar]
        mov     di, gdt15_src
        call    set_desc
        mov     eax, [cs:phys_scratch]
        mov     di, gdt15_dst
        call    set_desc
        call    bios_move
        mov     di, [cs:scratch_ptr]
        mov     eax, [cs:di]
        pop     di
        ret

wr_bios:
        push    di
        movzx   eax, ax
        add     eax, [cs:abar]
        mov     di, gdt15_dst
        call    set_desc
        mov     eax, [cs:phys_scratch]
        mov     di, gdt15_src
        call    set_desc
        mov     di, [cs:scratch_ptr]
        mov     [cs:di], ecx
        call    bios_move
        pop     di
        ret

; DI = descriptor, EAX = 32 bit base
set_desc:
        mov     [cs:di+2], ax
        shr     eax, 16
        mov     [cs:di+4], al
        mov     [cs:di+7], ah
        ret

bios_move:
        pushad
        push    es
        push    cs
        pop     es
        mov     si, gdt15
        mov     cx, 2                   ; two words
        mov     ah, 87h
        int     15h
        pop     es
        popad
        ret

;-----------------------------------------------------------------------------
; Flat real mode: (re)load FS with a 4 GB limit.  Called on every ASPI entry
; because other software (BIOS block moves, XMS) may reset segment limits.
;-----------------------------------------------------------------------------
flat_enable:
        push    eax
        push    bx
        push    fs
        pushf
        cli
        sgdt    [cs:old_gdtr]
        lgdt    [cs:flat_gdtr]
        mov     eax, cr0
        or      al, 1
        mov     cr0, eax
        jmp     short .pm
.pm:    mov     bx, 8
        mov     fs, bx
        and     al, 0FEh
        mov     cr0, eax
        jmp     short .rm
.rm:    lgdt    [cs:old_gdtr]
        popf
        pop     fs
        pop     bx
        pop     eax
        ret

; ZF set if A20 is disabled (memory wraps at 1 MB)
a20_test:
        push    ax
        push    si
        push    di
        push    ds
        push    es
        xor     ax, ax
        mov     ds, ax
        dec     ax
        mov     es, ax
        mov     si, 0500h
        mov     di, 0510h
        mov     al, [si]
        mov     ah, [es:di]
        push    ax
        mov     byte [si], 0
        mov     byte [es:di], 0FFh
        cmp     byte [si], 0FFh
        pop     ax
        mov     [es:di], ah
        mov     [si], al
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     ax
        ret

; enable A20 (XMS local enable if available, else BIOS)
a20_enable:
        pushad
        cmp     dword [cs:xms_entry], 0
        je      .bios
        mov     ah, 05h
        call    far [cs:xms_entry]
        jmp     .done
.bios:  mov     ax, 2401h
        int     15h
.done:  popad
        ret

a20_disable:
        pushad
        cmp     dword [cs:xms_entry], 0
        je      .bios
        mov     ah, 06h
        call    far [cs:xms_entry]
        jmp     .done
.bios:  mov     ax, 2400h
        int     15h
.done:  popad
        ret

;-----------------------------------------------------------------------------
; Timing helpers
;-----------------------------------------------------------------------------
get_ticks:                              ; -> EAX = BIOS tick counter
        push    ds
        push    word 40h
        pop     ds
        mov     eax, [6Ch]
        pop     ds
        ret

; wait for at least BX timer ticks (interrupts are enabled meanwhile)
delay_ticks:
        push    eax
        push    esi
        pushf
        sti
        call    get_ticks
        mov     esi, eax
.wait:  call    get_ticks
        sub     eax, esi
        js      .reset                  ; midnight wrap
        cmp     ax, bx
        jbe     .wait
        popf
        pop     esi
        pop     eax
        ret
.reset: call    get_ticks
        mov     esi, eax
        jmp     .wait

; Wait until (reg AX & ECX) == EDX.  BX = timeout in ticks.  CF on timeout.
wait_reg:
        push    eax
        push    esi
        push    di
        mov     di, ax
        pushf
        sti
        call    get_ticks
        mov     esi, eax
.poll:  call    poll_pause
        mov     ax, di
        call    reg_rd
        and     eax, ecx
        cmp     eax, edx
        je      .ok
        call    get_ticks
        sub     eax, esi
        jns     .chk
        call    get_ticks               ; midnight wrap: restart
        mov     esi, eax
        jmp     .poll
.chk:   cmp     ax, bx
        jbe     .poll
        popf
        stc
        jmp     .out
.ok:    popf
        clc
.out:   pop     di
        pop     esi
        pop     eax
        ret

;-----------------------------------------------------------------------------
; Port handling (BP = port register base)
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; Run the ATAPI packet in cdb_buf on device BX (index) with data through the
; bounce buffer.  ECX = transfer length (<= BOUNCE_SIZE), DL = 1 for write.
; Returns AL = XR_*.  Used for internal commands (INQUIRY, REQUEST SENSE).
;-----------------------------------------------------------------------------
packet_bounce:
        mov     [x_rem], ecx
        mov     ax, [bounce_ptr]
        mov     [x_bufp], ax
        mov     [x_bufp+2], cs
        mov     [x_write], dl
        jmp     usb_scsi

; x_bufp = normalized ES:DI
advance_bufp:
        push    ax
        mov     ax, di
        shr     ax, 4
        add     ax, [x_bufp+2]
        mov     [x_bufp+2], ax
        and     di, 0Fh
        mov     [x_bufp], di
        pop     ax
        ret

;=============================================================================
; ASPI entry point
;=============================================================================
aspi_entry:
        push    bp
        mov     bp, sp
        pushf
        pushad
        push    ds
        push    es
        push    fs
        push    gs
        ; Windows 3.x enhanced mode: no switch to another VM while we are
        ; in here (one driver state, one internal stack for all DOS boxes)
        xor     di, di
        smsw    ax
        test    al, 1
        jz      .nocs
        mov     ax, 1681h               ; begin critical section
        int     2Fh
        inc     di
.nocs:  push    di                      ; (kept on the caller's stack)
        mov     ax, cs
        mov     ds, ax
        les     si, [ss:bp+6]
        mov     [srb_ptr], si
        mov     [srb_ptr+2], es

        ; adapters of the previous manager: pass the request on, on the
        ; caller's stack (other managers may need more than our stack)
        mov     al, [es:si+SRB_HaId]
        cmp     al, [prev_count]
        jae     .ours
%ifdef E9LOG
        call    trace_in
%endif
        push    es
        push    si
        call    far [cs:prev_entry]
        add     sp, 4
        mov     ax, cs
        mov     ds, ax
        les     si, [ss:bp+6]
        cmp     byte [es:si+SRB_Cmd], SC_HA_INQUIRY
        jne     .fwd_done
        inc     byte [es:si+HA_Count]   ; count our adapter as well
.fwd_done:
%ifdef E9LOG
        mov     [srb_ptr], si
        mov     [srb_ptr+2], es
        call    trace_out
%endif
        jmp     .restore

.ours:  cmp     byte [in_aspi], 0
        jne     .nested
        pushf
        pop     dx
        cli
        mov     [old_ss], ss
        mov     [old_sp], sp
        mov     ax, cs                  ; (AL was overwritten with the HA id)
        mov     ss, ax
        mov     sp, stack_top
        push    dx
        popf
.nested:
        inc     byte [in_aspi]
        cld

        cmp     byte [acc_method], AM_FLAT
        je      .acc
        cmp     byte [acc_method], AM_BIOS
        jne     .go
.acc:
        ; flat mode was chosen in real mode; a V86 monitor started later
        ; needs another way: EUSBVXD.386 in Windows 3.x enhanced mode,
        ; else INT 15h
        mov     word [rd_vec], rd_bios
        mov     word [wr_vec], wr_bios
        smsw    ax
        test    al, 1
        jz      .real
        push    es
        xor     di, di
        mov     es, di
        mov     ax, 1684h               ; get V86 API entry of EUSBVXD
        mov     bx, EUSBVXD_ID
        int     2Fh
        mov     ax, es
        or      ax, di
        jz      .novxd
        mov     [vxd_entry], di
        mov     [vxd_entry+2], es
        mov     word [rd_vec], rd_vxd
        mov     word [wr_vec], wr_vxd
.novxd: pop     es
        jmp     .go
.real:  cmp     byte [acc_method], AM_FLAT
        jne     .go
        mov     word [rd_vec], rd_flat
        mov     word [wr_vec], wr_flat
        call    flat_enable
        call    a20_test
        jnz     .go
        call    a20_enable
        mov     byte [a20_fixed], 1

.go:
%ifdef E9LOG
        call    trace_in
%endif
        call    aspi_dispatch
%ifdef E9LOG
        call    trace_out
%endif

        cmp     byte [a20_fixed], 0
        je      .leave
        mov     byte [a20_fixed], 0
        call    a20_disable
.leave: dec     byte [in_aspi]
        jnz     .restore
        cli
        mov     ss, [old_ss]
        mov     sp, [old_sp]
.restore:
        pop     cx                      ; critical section to end?
        jcxz    .nocs2
        mov     ax, 1682h               ; end critical section
        int     2Fh
.nocs2: pop     gs
        pop     fs
        pop     es
        pop     ds
        popad
        popf
        pop     bp
        retf

;-----------------------------------------------------------------------------
; ASPI trace to the QEMU debug console (test builds only)
;   > c=<cmd> h=<ha> t=<target> [op=<cdb0> n=<cdblen> f=<flags> l=<buflen>]
;   < s=<status> [ha=<hastat> ts=<targstat> sk=<sense key>]
;-----------------------------------------------------------------------------
%ifdef E9LOG
trace_in:
        push    ax
        push    si
        push    es
        les     si, [srb_ptr]
        mov     al, '>'
        call    e9_putc
        mov     al, 'c'
        mov     ah, [es:si+SRB_Cmd]
        call    e9_field
        mov     al, 'h'
        mov     ah, [es:si+SRB_HaId]
        call    e9_field
        cmp     byte [es:si+SRB_Cmd], SC_HA_INQUIRY
        je      .end
        mov     al, 't'
        mov     ah, [es:si+SRB_Target]
        call    e9_field
        cmp     byte [es:si+SRB_Cmd], SC_EXEC_SCSI_CMD
        jne     .end
        push    si
        mov     si, e9_op
        call    e9_str
        pop     si
        mov     al, [es:si+SRB_CDB]
        call    e9_hex8
        mov     al, 'n'
        mov     ah, [es:si+SRB_CDBLen]
        call    e9_field
        mov     al, 'f'
        mov     ah, [es:si+SRB_Flags]
        call    e9_field
        mov     al, 'l'
        mov     ah, [es:si+SRB_BufLen+1]
        call    e9_field
        mov     al, [es:si+SRB_BufLen]
        call    e9_hex8
.end:   pop     es
        pop     si
        pop     ax
        ret

trace_out:
        push    ax
        push    bx
        push    si
        push    es
        les     si, [srb_ptr]
        mov     al, ' '
        call    e9_putc
        mov     al, '<'
        call    e9_putc
        mov     al, 's'
        mov     ah, [es:si+SRB_Status]
        call    e9_field
        cmp     byte [es:si+SRB_Cmd], SC_EXEC_SCSI_CMD
        jne     .end
        cmp     byte [es:si+SRB_Status], SS_COMP
        je      .end
        push    si
        mov     si, e9_ha
        call    e9_str
        pop     si
        mov     al, [es:si+SRB_HaStat]
        call    e9_hex8
        mov     al, 't'
        mov     ah, [es:si+SRB_TargStat]
        call    e9_field
        cmp     byte [es:si+SRB_TargStat], STATUS_CHKCOND
        jne     .end
        movzx   bx, byte [es:si+SRB_CDBLen]
        add     bx, si
        push    si
        mov     si, e9_sk
        call    e9_str
        pop     si
        mov     al, [es:bx+SRB_CDB+2]
        and     al, 0Fh
        call    e9_hex8
        mov     al, '/'
        call    e9_putc
        mov     al, [es:bx+SRB_CDB+12]
        call    e9_hex8
        mov     al, '/'
        call    e9_putc
        mov     al, [es:bx+SRB_CDB+13]
        call    e9_hex8
.end:   mov     al, 13
        call    e9_putc
        mov     al, 10
        call    e9_putc
        pop     es
        pop     si
        pop     bx
        pop     ax
        ret

; " <AL>=<AH as hex>"
e9_field:
        push    ax
        mov     al, ' '
        call    e9_putc
        pop     ax
        call    e9_putc
        mov     al, '='
        call    e9_putc
        mov     al, ah
e9_hex8:
        push    ax
        shr     al, 4
        call    .nib
        pop     ax
        push    ax
        and     al, 0Fh
        call    .nib
        pop     ax
        ret
.nib:   add     al, '0'
        cmp     al, '9'
        jbe     e9_putc
        add     al, 7
e9_putc:
        out     0E9h, al
        ret

e9_str: push    ax
.l:     mov     al, [cs:si]
        or      al, al
        jz      .e
        call    e9_putc
        inc     si
        jmp     .l
.e:     pop     ax
        ret

e9_op           db      ' op=', 0
e9_ha           db      ' ha=', 0
e9_sk           db      ' sk=', 0
%endif

aspi_dispatch:
        les     si, [srb_ptr]
        mov     al, [es:si+SRB_Cmd]
        mov     ah, [es:si+SRB_HaId]
        cmp     al, SC_HA_INQUIRY
        je      srb_ha_inquiry
        cmp     ah, [prev_count]
        jne     .bad_ha
        cmp     al, SC_GET_DEV_TYPE
        je      srb_get_dev_type
        cmp     al, SC_EXEC_SCSI_CMD
        je      srb_exec
        cmp     al, SC_ABORT_SRB
        je      srb_abort
        cmp     al, SC_RESET_DEV
        je      srb_reset
        cmp     al, SC_GET_DISK_INFO
        je      srb_disk_info
        mov     byte [es:si+SRB_Status], SS_INVALID_CMD
        ret
.bad_ha:
        mov     byte [es:si+SRB_Status], SS_INVALID_HA
        ret

;--- SC_HA_INQUIRY -----------------------------------------------------------
srb_ha_inquiry:
        mov     al, [prev_count]
        inc     al
        mov     [es:si+HA_Count], al
        dec     al
        cmp     [es:si+SRB_HaId], al
        je      .ok
        mov     byte [es:si+SRB_Status], SS_INVALID_HA
        ret
.ok:    mov     byte [es:si+HA_SCSI_ID], HA_TARGET_ID
        lea     di, [si+HA_ManagerId]
        push    si
        mov     si, mgr_id
        mov     cx, 48                  ; manager id, adapter id, unique
        rep     movsb
        pop     si
        cmp     word [es:si+SRB_Hdr_Rsvd], 0AA55h ; extended inquiry?
        jne     .done
        mov     word [es:si+SRB_Hdr_Rsvd], 55AAh
        mov     al, [es:si+SRB_Hdr_Rsvd+2]      ; bytes requested
        cmp     al, 2
        jb      .none
        mov     word [es:si+HA_ExtFlags], HA_EXT_RESIDUAL
        mov     al, 2
        jmp     .ext
.none:  xor     al, al
.ext:   mov     [es:si+SRB_Hdr_Rsvd+2], al
.done:  mov     byte [es:si+SRB_Status], SS_COMP
        ret

; ES:SI = SRB -> BX = device index.  CF (and status set) if no device.
find_dev:
        movzx   bx, byte [es:si+SRB_Target]
        cmp     bl, [dev_count]
        jae     .none
        cmp     byte [es:si+SRB_Lun], 0
        jne     .none
        clc
        ret
.none:  mov     byte [es:si+SRB_Status], SS_NO_DEVICE
        stc
        ret

;--- SC_GET_DEV_TYPE ---------------------------------------------------------
srb_get_dev_type:
        call    find_dev
        jc      .out
        mov     al, [dev_type+bx]
        mov     [es:si+SRB_DeviceType], al
        mov     byte [es:si+SRB_Status], SS_COMP
.out:   ret

;--- SC_ABORT_SRB: all requests complete synchronously -----------------------
srb_abort:
        mov     byte [es:si+SRB_Status], SS_ABORT_FAIL
        ret

;--- SC_GET_DISK_INFO --------------------------------------------------------
srb_disk_info:
        call    find_dev
        jc      .out
        xor     ax, ax
        mov     [es:si+SRB_DriveFlags], ax      ; not an INT 13h drive
        mov     [es:si+SRB_Heads], ax
        mov     byte [es:si+SRB_Status], SS_COMP
.out:   ret

;--- SC_RESET_DEV ------------------------------------------------------------
srb_reset:
        call    find_dev
        jc      .out
        call    usb_reset
.result:
        mov     word [es:si+SRB_HaStat], 0      ; HaStat, TargStat
        jnc     .ok
        mov     byte [es:si+SRB_HaStat], HASTAT_SEL_TO
        mov     byte [es:si+SRB_Status], SS_ERR
        jmp     call_post
.ok:    mov     byte [es:si+SRB_Status], SS_COMP
        jmp     call_post
.out:   ret

; call the post routine of the SRB at ES:SI if requested
call_post:
        test    byte [es:si+SRB_Flags], SRB_POSTING
        jz      .out
        mov     eax, [es:si+SRB_PostProc]
        or      eax, eax
        jz      .out
        mov     [post_ptr], eax
        sti
        push    es
        push    si
        call    far [post_ptr]
        add     sp, 4
.out:   ret

;--- SC_EXEC_SCSI_CMD --------------------------------------------------------
srb_exec:
        cmp     byte [hot_slots], 0
        je      .nohot
        call    hot_poll                ; in the hot plug part (/H only)
        les     si, [srb_ptr]
.nohot: call    find_dev
        jnc     .dev
        jmp     call_post
.dev:   mov     [s_dev], bx
        mov     word [es:si+SRB_HaStat], 0      ; HaStat, TargStat
        mov     dword [s_xfer], 0
        mov     byte [s_over], 0
        test    byte [es:si+SRB_Flags], SRB_LINKING
        jz      .nolink
        jmp     .invalid                ; ATAPI has no linked commands
.nolink:
        mov     eax, [es:si+SRB_BufLen]
        mov     [s_len], eax
        mov     eax, [es:si+SRB_BufPointer]
        mov     [s_buf], eax

        ; copy the CDB
        movzx   cx, byte [es:si+SRB_CDBLen]
        test    cx, cx
        jz      .invalid
        cmp     cx, 16
        ja      .invalid
        cmp     cx, 12
        jbe     .cdblen_ok
        test    byte [dev_flags+bx], DF_CDB16
        jz      .invalid
.cdblen_ok:
        xor     eax, eax
        mov     [cdb_buf], eax
        mov     [cdb_buf+4], eax
        mov     [cdb_buf+8], eax
        mov     [cdb_buf+12], eax
        push    si
        add     si, SRB_CDB
        mov     di, cdb_buf
.copy:  mov     al, [es:si]
        mov     [di], al
        inc     si
        inc     di
        loop    .copy
        pop     si

        ; transfer direction
        mov     byte [s_dir], 0
        cmp     dword [s_len], 0
        je      .dir_done
        mov     al, [es:si+SRB_Flags]
        and     al, SRB_DIR_MASK
        cmp     al, SRB_DIR_NONE
        je      .dir_done
        mov     byte [s_dir], 1
        cmp     al, SRB_DIR_IN
        je      .dir_done
        mov     byte [s_dir], 2
        cmp     al, SRB_DIR_OUT
        je      .dir_done
        mov     al, [cdb_buf]           ; SRB_DIR_AUTO: look at the opcode
        call    opcode_is_write
        jc      .dir_done
        mov     byte [s_dir], 1
.dir_done:
        mov     byte [s_xlate], XL_NONE
        cmp     byte [dev_trans+bx], TR_EMPTY
        je      hot_empty
        cmp     byte [dev_ua+bx], 0
        jne     hot_attention
.noua:
        call    translate_cdb
        jc      .invalid
        ; the USB transport moves the data through its own page: the
        ; client buffer, or the bounce buffer for translated MODE SENSE /
        ; MODE SELECT
        xor     eax, eax
        mov     [x_rem], eax
        cmp     byte [s_xlate], XL_NONE
        je      .client
        movzx   eax, word [s_blen]
        mov     [x_rem], eax
        mov     ax, [bounce_ptr]
        mov     [x_bufp], ax
        mov     [x_bufp+2], cs
        jmp     .go
.client:
        cmp     byte [s_dir], 0
        je      .nodata
        mov     eax, [s_len]
        mov     [x_rem], eax
.nodata:
        mov     eax, [s_buf]
        mov     [x_bufp], eax
.go:    xor     al, al
        cmp     byte [s_dir], 2
        jne     .rd
        inc     al
.rd:    mov     [x_write], al
        call    usb_scsi

.done_exec:
        les     si, [srb_ptr]
        push    eax
        mov     eax, [x_xfer]
        mov     [s_xfer], eax
        mov     ah, [x_over]
        mov     [s_over], ah
        pop     eax
        cmp     al, XR_OK
        jne     .failed
        cmp     byte [s_xlate], XL_MODESENSE
        jne     .nomsb
        call    modesense_back
.nomsb: call    inquiry_fixup
.good:  mov     byte [es:si+SRB_Status], SS_COMP
        call    exec_extras
        jmp     call_post

.failed:
        cmp     al, XR_CHECK
        jne     .hba_err
        ; ATAPI devices cannot change their block size and reject a MODE
        ; SELECT that only carries a block descriptor (ASPICD.SYS sends one
        ; to set 2048 byte blocks).  Report success, as ATAPI ASPI managers do.
        cmp     byte [s_xlate], XL_MODESELECT
        jne     .check
        cmp     byte [s_msel_bdonly], 0
        je      .check
        mov     al, [x_tfd+1]           ; ATA error register: sense key
        shr     al, 4
        cmp     al, SENSE_ILLEGAL_REQUEST
        je      .good
.check:
        mov     byte [es:si+SRB_TargStat], STATUS_CHKCOND
        call    auto_sense
        les     si, [srb_ptr]
        jmp     .err
.hba_err:
        mov     byte [es:si+SRB_HaStat], HASTAT_SEL_TO
        cmp     al, XR_HBA
        jne     .err
        mov     byte [es:si+SRB_HaStat], HASTAT_PHASE_ERR
.err:   mov     byte [es:si+SRB_Status], SS_ERR
        call    exec_extras
        jmp     call_post

.invalid:
        les     si, [srb_ptr]
        mov     byte [es:si+SRB_Status], SS_INVALID_CMD
        jmp     call_post

;--- hot plug slots: answered without a device -------------------------------
; empty slot BX: INQUIRY says removable disk, everything else NOT READY
hot_empty:
        mov     al, [cdb_buf]
        cmp     al, SCSI_INQUIRY
        je      .inq
        mov     ax, 3A02h               ; NOT READY, MEDIUM NOT PRESENT
        cmp     byte [cdb_buf], SCSI_REQUEST_SENSE
        je      hot_sense_data
        jmp     hot_check
.inq:   mov     al, bl
        add     al, '0'
        mov     [hot_inq+25], al        ; "USB slot n"
        mov     si, hot_inq
        mov     cx, hot_inq_len
        jmp     hot_data

; unit attention pending for target BX: report it once (not for INQUIRY)
hot_attention:
        cmp     byte [cdb_buf], SCSI_INQUIRY
        je      srb_exec.noua
        mov     byte [dev_ua+bx], 0
        mov     ax, 2806h               ; UNIT ATTENTION, MEDIUM MAY HAVE CHANGED
        cmp     byte [cdb_buf], SCSI_REQUEST_SENSE
        je      hot_sense_data
        ; fall through

; CHECK CONDITION with sense key AL, ASC AH (auto sense into the SRB)
hot_check:
        les     si, [srb_ptr]
        mov     byte [es:si+SRB_TargStat], STATUS_CHKCOND
        mov     dx, ax
        movzx   cx, byte [es:si+SRB_SenseLen]
        movzx   di, byte [es:si+SRB_CDBLen]
        add     di, si
        add     di, SRB_CDB
        jcxz    .st
        push    di
        push    cx
        xor     al, al
        rep     stosb
        pop     cx
        pop     di
        mov     byte [es:di], 70h
        cmp     cx, 3
        jb      .st
        mov     [es:di+2], dl
        cmp     cx, 8
        jb      .st
        mov     byte [es:di+7], 0Ah
        cmp     cx, 13
        jb      .st
        mov     [es:di+12], dh
.st:    mov     byte [es:si+SRB_Status], SS_ERR
        jmp     call_post

; REQUEST SENSE answer: sense key AL, ASC AH
hot_sense_data:
        mov     si, hot_sense
        mov     [si+2], al
        mov     [si+12], ah
        mov     cx, 18
        ; fall through

; copy CX bytes at DS:SI into the client buffer (at most its size), done
hot_data:
        movzx   ecx, cx
        cmp     ecx, [s_len]
        jbe     .len
        mov     ecx, [s_len]
.len:   les     di, [s_buf]
        rep     movsb
        les     si, [srb_ptr]
        mov     byte [es:si+SRB_Status], SS_COMP
        jmp     call_post

hot_inq         db      00h, 80h, 02h, 02h, 31, 0, 0, 0
                db      'EUSBASPI', 'USB slot n      ', '0.1 '
hot_inq_len     equ     $ - hot_inq
hot_sense       db      70h, 0, 0, 0, 0, 0, 0, 0Ah, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

; Data overrun reporting and residual byte count for the SRB at ES:SI
;  - overrun (device wanted to move more data than the buffer holds) is
;    reported as HaStat 12h when the SRB gives an explicit direction
;  - with SRB_RESIDUAL, SRB_BufLen returns the bytes not transferred
exec_extras:
        cmp     byte [s_over], 0
        je      .resid
        mov     al, [es:si+SRB_Flags]
        and     al, SRB_DIR_MASK
        cmp     al, SRB_DIR_IN
        je      .over
        cmp     al, SRB_DIR_OUT
        jne     .resid
.over:  cmp     byte [es:si+SRB_HaStat], HASTAT_OK
        jne     .resid
        mov     byte [es:si+SRB_HaStat], HASTAT_DO_DU
        mov     byte [es:si+SRB_Status], SS_ERR
.resid: test    byte [es:si+SRB_Flags], SRB_RESIDUAL
        jz      .out
        xor     eax, eax
        cmp     byte [s_xlate], XL_NONE ; translated: buffer sizes differ
        jne     .set
        cmp     byte [s_dir], 0
        je      .set
        mov     eax, [s_len]
        sub     eax, [s_xfer]
        jae     .set
        xor     eax, eax
.set:   mov     [es:si+SRB_BufLen], eax
.out:   ret

; ATAPI CD/DVD drives report ANSI version 0 (or 5) in their INQUIRY data.
; ASPICD.SYS only uses the standard SCSI-2 audio commands for drives of
; unknown vendors that claim SCSI-2 and otherwise installs them "for
; Data-Mode only" (no CD audio).  Report version 2 for CD/DVD drives.
inquiry_fixup:
        cmp     byte [cdb_buf], SCSI_INQUIRY
        jne     .out
        test    byte [cdb_buf+1], 1     ; vital product data page
        jnz     .out
        cmp     dword [s_len], 3
        jb      .out
        push    es
        push    di
        les     di, [s_buf]
        cmp     byte [hot_slots], 0     ; hot plug slots are removable, so
        je      .notslot                ; that DOS asks for media changes
        or      byte [es:di+1], 80h
.notslot:
        cmp     byte [inq_scsi2], 0
        je      .pop
        mov     al, [es:di]
        and     al, 1Fh
        cmp     al, 5                   ; CD/DVD device
        jne     .pop
        mov     al, [es:di+2]
        and     al, 7
        cmp     al, 2
        je      .pop
        mov     byte [es:di+2], 2
.pop:   pop     di
        pop     es
.out:   ret

; CF set if opcode AL transfers data from host to device
opcode_is_write:
        push    cx
        push    di
        push    es
        push    cs
        pop     es
        mov     di, write_opcodes
        mov     cx, write_opcodes_len
        repne   scasb
        pop     es
        pop     di
        pop     cx
        clc
        jne     .out
        stc
.out:   ret

write_opcodes   db      04h, 07h, 0Ah, 15h, 18h, 1Dh, 2Ah, 2Eh, 39h, 3Ah
                db      3Bh, 3Fh, 41h, 4Ch, 54h, 55h, 5Dh, 0A3h, 0AAh
                db      0AEh, 0BFh
write_opcodes_len equ   $ - write_opcodes

;-----------------------------------------------------------------------------
; Rewrite 6 byte commands that ATAPI devices commonly reject into their
; 10 byte equivalents.  CF if the request cannot be handled.
;   READ(6), WRITE(6)   -> READ(10), WRITE(10)
;   SEEK(6), REZERO     -> SEEK(10)
;   MODE SENSE(6)       -> MODE SENSE(10), answer converted back
;   MODE SELECT(6)      -> MODE SELECT(10), parameter list converted
;-----------------------------------------------------------------------------
translate_cdb:
        mov     byte [s_xlate], XL_NONE
        mov     al, [cdb_buf]
        cmp     al, SCSI_READ6
        je      .rw6
        cmp     al, SCSI_WRITE6
        je      .rw6
        cmp     al, SCSI_MODE_SENSE6
        je      .msense
        cmp     al, SCSI_MODE_SELECT6
        je      .mselect
        cmp     al, SCSI_SEEK6
        je      .seek6
        cmp     al, SCSI_REZERO_UNIT
        je      .rezero
        clc
        ret

.rezero:
        ; REZERO UNIT -> SEEK(10) to LBA 0
        mov     bl, [cdb_buf+5]
        xor     eax, eax
        mov     [cdb_buf], eax
        mov     [cdb_buf+4], eax
        mov     byte [cdb_buf], SCSI_SEEK10
        mov     [cdb_buf+9], bl
        mov     bx, [s_dev]
        clc
        ret

.seek6: ; SEEK(6) -> SEEK(10)
        mov     ah, [cdb_buf+1]
        and     ah, 1Fh                 ; LBA bits 20..16
        mov     dx, [cdb_buf+2]         ; LBA bits 15..0 (big endian)
        mov     bl, [cdb_buf+5]
        mov     byte [cdb_buf], SCSI_SEEK10
        mov     byte [cdb_buf+1], 0
        mov     byte [cdb_buf+2], 0
        mov     [cdb_buf+3], ah
        mov     [cdb_buf+4], dx
        mov     word [cdb_buf+6], 0
        mov     byte [cdb_buf+8], 0
        mov     [cdb_buf+9], bl
        mov     bx, [s_dev]
        clc
        ret

.rw6:   ; READ(6)/WRITE(6) -> READ(10)/WRITE(10)
        add     al, 20h                 ; 08h->28h, 0Ah->2Ah
        mov     ah, [cdb_buf+1]
        and     ah, 1Fh                 ; LBA bits 20..16
        mov     dx, [cdb_buf+2]         ; LBA bits 15..0 (big endian)
        movzx   cx, byte [cdb_buf+4]    ; length, 0 means 256
        or      cx, cx
        jnz     .len_ok
        mov     cx, 256
.len_ok:
        mov     bl, [cdb_buf+5]
        mov     [cdb_buf], al
        mov     byte [cdb_buf+1], 0
        mov     byte [cdb_buf+2], 0
        mov     [cdb_buf+3], ah
        mov     [cdb_buf+4], dx
        mov     byte [cdb_buf+6], 0
        xchg    cl, ch
        mov     [cdb_buf+7], cx
        mov     [cdb_buf+9], bl
        mov     bx, [s_dev]
        clc
        ret

.msense:
        ; MODE SENSE(6) -> MODE SENSE(10), answer converted afterwards
        call    .ms10
        jnc     .out
        movzx   cx, byte [cdb_buf+4]
        mov     [s_alloc6], cx
        jcxz    .ms_build               ; nothing requested, nothing to fix
        cmp     byte [s_dir], 2
        je      .fail
        mov     byte [s_dir], 1
        mov     byte [s_xlate], XL_MODESENSE
        add     cx, 4
        mov     [s_blen], cx
.ms_build:
        mov     bl, [cdb_buf+5]
        mov     al, [cdb_buf+1]
        and     al, 08h                 ; DBD
        mov     byte [cdb_buf], SCSI_MODE_SENSE10
        mov     [cdb_buf+1], al
        mov     byte [cdb_buf+4], 0
        mov     byte [cdb_buf+5], 0
        mov     byte [cdb_buf+6], 0
        xchg    cl, ch
        mov     [cdb_buf+7], cx
        mov     [cdb_buf+9], bl
        mov     bx, [s_dev]
        clc
        ret

.mselect:
        ; MODE SELECT(6) -> MODE SELECT(10), parameter list converted now
        call    .ms10
        jnc     .out
        movzx   cx, byte [cdb_buf+4]
        jcxz    .sel_build
        cmp     cx, 4
        jb      .fail
        cmp     byte [s_dir], 1
        je      .fail
        mov     byte [s_dir], 2
        mov     byte [s_xlate], XL_MODESELECT
        call    modeselect_prepare
        mov     cx, [s_blen]
.sel_build:
        mov     bl, [cdb_buf+5]
        mov     al, [cdb_buf+1]
        and     al, 11h                 ; PF, SP
        mov     byte [cdb_buf], SCSI_MODE_SELECT10
        mov     [cdb_buf+1], al
        mov     byte [cdb_buf+4], 0
        mov     byte [cdb_buf+5], 0
        mov     byte [cdb_buf+6], 0
        xchg    cl, ch
        mov     [cdb_buf+7], cx
        mov     [cdb_buf+9], bl
        mov     bx, [s_dev]
        clc
        ret
.fail:  mov     bx, [s_dev]
        stc
        ret
; CF set if the device needs the 10 byte MODE commands (USB devices
; mostly take the 6 byte ones; the ones that don't are found at init)
.ms10:  push    bx
        mov     bx, [s_dev]
        test    byte [dev_flags+bx], DF_MS10
        pop     bx
        jz      .out
        stc
.out:   ret

; build the MODE SELECT(10) parameter list in the bounce buffer
; CX = MODE SELECT(6) parameter list length (>= 4)
modeselect_prepare:
        push    ds
        push    es
        mov     di, [bounce_ptr]
        push    cs
        pop     es
        lds     si, [cs:s_buf]
        movzx   ax, byte [si+3]         ; only a block descriptor, no pages?
        add     ax, 4
        cmp     cx, ax
        setbe   byte [cs:s_msel_bdonly]
        xor     ax, ax
        stosw                           ; mode data length (reserved)
        mov     ax, [si+1]              ; medium type, device specific
        stosw
        xor     ax, ax
        stosw
        stosb
        mov     al, [si+3]              ; block descriptor length
        stosb
        add     si, 4
        sub     cx, 4
        push    cx
        rep     movsb
        pop     cx
        pop     es
        pop     ds
        add     cx, 8
        mov     [s_blen], cx
        ret

; convert a MODE SENSE(10) answer in the bounce buffer to MODE SENSE(6)
; format and copy it to the client buffer
modesense_back:
        push    es
        push    si
        mov     si, [bounce_ptr]
        mov     ax, [si]                ; mode data length (big endian)
        xchg    al, ah
        sub     ax, 3
        jae     .nonneg
        xor     ax, ax
.nonneg:
        cmp     ax, 255
        jbe     .lenok
        mov     ax, 255
.lenok: mov     [si], al
        mov     al, [si+2]              ; medium type
        mov     [si+1], al
        mov     al, [si+3]              ; device specific parameter
        mov     [si+2], al
        mov     al, [si+7]              ; block descriptor length
        mov     [si+3], al
        lea     di, [si+4]              ; move the rest down by 4
        add     si, 8
        mov     cx, [s_alloc6]
        push    es
        push    cs
        pop     es
        rep     movsb
        pop     es
        ; copy to the client
        movzx   ecx, word [s_alloc6]
        cmp     dword [s_len], ecx
        jae     .copy
        mov     cx, [s_len]
.copy:  les     di, [s_buf]
        mov     si, [bounce_ptr]
        rep     movsb
        pop     si
        pop     es
        ret

;-----------------------------------------------------------------------------
; Automatic REQUEST SENSE after CHECK CONDITION for the SRB at [srb_ptr]
;-----------------------------------------------------------------------------
auto_sense:
        les     si, [srb_ptr]
        movzx   cx, byte [es:si+SRB_SenseLen]
        test    cx, cx
        jz      .out
        mov     ax, [x_tfd]             ; keep the error register
        push    ax
        xor     eax, eax
        mov     [cdb_buf], eax
        mov     [cdb_buf+4], eax
        mov     [cdb_buf+8], eax
        mov     [cdb_buf+12], eax
        mov     byte [cdb_buf], SCSI_REQUEST_SENSE
        cmp     cx, 252
        jbe     .alloc
        mov     cx, 252
.alloc: mov     [cdb_buf+4], cl
        movzx   ecx, cx
        xor     dl, dl
        mov     bx, [s_dev]
        call    packet_bounce
        pop     dx                      ; DH = error register
        les     si, [srb_ptr]
        movzx   cx, byte [es:si+SRB_SenseLen]
        movzx   di, byte [es:si+SRB_CDBLen]
        add     di, si
        add     di, SRB_CDB
        cmp     al, XR_OK
        jne     .fake
        cmp     cx, 252
        jbe     .copy
        mov     cx, 252
.copy:  mov     si, [bounce_ptr]
        rep     movsb
        jmp     .out
.fake:  ; REQUEST SENSE failed: build sense data from the ATA error register
        push    di
        xor     al, al
        rep     stosb
        pop     di
        mov     al, [es:si+SRB_SenseLen]
        cmp     al, 3
        jb      .out
        mov     byte [es:di], 70h
        shr     dh, 4
        mov     [es:di+2], dh
        cmp     al, 8
        jb      .out
        mov     byte [es:di+7], 0Ah
.out:   ret

%ifdef USB_BUILD
%include "usb.inc"
%endif

;-----------------------------------------------------------------------------
; Stack and DMA area
;-----------------------------------------------------------------------------
                align   16
stack_bottom:   times STACK_SIZE db 0
stack_top:

dma_area:       times DMA_RESERVE db 0

resident_end:

;=============================================================================
; Initialisation (discarded after init)
;=============================================================================

opt_method      db      AM_NONE         ; /M: forced access method
opt_quiet       db      0               ; /Q
opt_verbose     db      0               ; /V
opt_xhci        db      0               ; /X: take the xHCI controller over
msg_resident    db      '  resident: ', 0
msg_bytes       db      ' bytes at segment ', 0
pci_lastbus     db      0
is_v86          db      0
pci_bdf         dw      0
pci_vendev      dd      0
id_model        times 41 db 0

; AX = offset in our segment, ECX = size -> EAX = physical address.
; Uses a permanent VDS lock when VDS is present.  CF on failure.
get_phys:
        push    es
        push    di
        push    dx
        movzx   edx, ax
        cmp     byte [vds_ok], 0
        jne     .vds
        mov     ax, cs
        movzx   eax, ax
        shl     eax, 4
        add     eax, edx
        clc
        jmp     .out
.vds:   mov     [dds_size], ecx
        mov     [dds_off], edx
        mov     [dds_seg], cs
        mov     word [dds_bufid], 0
        push    cs
        pop     es
        mov     di, dds
        mov     dx, 000Ch               ; no buffer, no remap
        mov     ax, 8103h
        int     4Bh
        jc      .out
        mov     eax, [dds_phys]
.out:   pop     dx
        pop     di
        pop     es
        ret

; DI = register -> ECX
pci_rd: push    ax
        push    bx
        mov     bx, [pci_bdf]
        mov     ax, 0B10Ah
        int     1Ah
        pop     bx
        pop     ax
        ret

; DI = register, ECX = value
pci_wr: push    ax
        push    bx
        mov     bx, [pci_bdf]
        mov     ax, 0B10Dh
        int     1Ah
        pop     bx
        pop     ax
        ret

;-----------------------------------------------------------------------------
; Pick the register access method.  CF and SI = message on failure.
;-----------------------------------------------------------------------------
choose_method:
        mov     al, [opt_method]
        cmp     al, AM_NONE
        jne     .forced
        mov     al, AM_FLAT             ; flat mode unless in V86 mode
        cmp     byte [is_v86], 0
        je      .set
        mov     al, AM_BIOS
        jmp     .set
.forced:
        cmp     al, AM_FLAT
        jne     .set
        cmp     byte [is_v86], 0
        jne     .unavail
.set:   mov     [acc_method], al
        mov     word [rd_vec], rd_flat
        mov     word [wr_vec], wr_flat
        cmp     al, AM_FLAT
        je      .flat
        mov     word [rd_vec], rd_bios
        mov     word [wr_vec], wr_bios
        clc
        ret
.flat:  call    a20_test
        jnz     .a20ok
        call    a20_enable
        call    a20_test
        jnz     .a20ok
        mov     si, msg_no_a20
        stc
        ret
.a20ok: call    flat_enable
        clc
        ret
.unavail:
        mov     si, msg_bad_method
        stc
        ret

; INQUIRY for device BX (retried for unit attention).  Updates the type.
inquiry_dev:
        mov     cx, 4
.retry: push    cx
        xor     eax, eax
        mov     [cdb_buf], eax
        mov     [cdb_buf+4], eax
        mov     [cdb_buf+8], eax
        mov     [cdb_buf+12], eax
        mov     byte [cdb_buf], SCSI_INQUIRY
        mov     byte [cdb_buf+4], 36
        mov     ecx, 36
        xor     dl, dl
        call    packet_bounce
        pop     cx
        cmp     al, XR_OK
        je      .ok
        loop    .retry
        ret
.ok:    mov     si, [bounce_ptr]
        mov     al, [si]
        and     al, 1Fh
        mov     [dev_type+bx], al
        ret

; print the line for device BX
print_dev:
        push    bx
        mov     si, msg_id
        call    print
        mov     ax, bx
        call    print_dec
        cmp     byte [dev_trans+bx], TR_IDE
        je      .ide
        mov     si, msg_port
        cmp     byte [dev_trans+bx], TR_USB
        jne     .portmsg
        mov     si, msg_usb_port
.portmsg:
        call    print
        mov     al, [dev_pnum+bx]
        xor     ah, ah
        call    print_dec
        jmp     .loc
.ide:   mov     si, msg_ide
        call    print
        mov     di, bx
        add     di, di
        mov     ax, [dev_port+di]
        call    print_hex16
        mov     si, msg_master
        cmp     byte [dev_sel+bx], 0A0h
        je      .ms
        mov     si, msg_slave
.ms:    call    print
.loc:   mov     si, msg_colon
        call    print
        mov     si, id_model            ; trimmed model string
        call    print_trim
        mov     si, msg_open
        call    print
        mov     al, [dev_type+bx]
        cmp     al, 0Fh
        jbe     .known
        mov     al, 0Fh
.known: movzx   di, al
        add     di, di
        mov     si, [type_names+di]
        call    print
        mov     si, msg_dma
        test    byte [dev_flags+bx], DF_DMA
        jnz     .mode
        mov     si, msg_pio
.mode:  call    print
        pop     bx
        ret

type_names      dw      t_disk, t_tape, t_other, t_other, t_worm, t_cdrom
                dw      t_other, t_optical, t_other, t_other, t_other
                dw      t_other, t_other, t_other, t_other, t_other

;-----------------------------------------------------------------------------
; Output helpers (init only)
;-----------------------------------------------------------------------------
putc:
%ifdef E9LOG
        out     0E9h, al
%endif
        cmp     byte [cs:hot_active], 0 ; hot plug at run time: no DOS
        jne     .out
        push    ax
        push    dx
        mov     dl, al
        mov     ah, 2
        int     21h
        pop     dx
        pop     ax
.out:   ret

print:
%ifndef E9LOG
        cmp     byte [cs:hot_active], 0
        jne     .ret
%endif
        push    ax
        push    si
.l:     mov     al, [si]
        or      al, al
        jz      .e
        call    putc
        inc     si
        jmp     .l
.e:     pop     si
        pop     ax
.ret:   ret

; print an ASCIZ string without trailing blanks and leading blanks
print_trim:
        push    ax
        push    si
        push    di
.lead:  cmp     byte [si], ' '
        jne     .end
        inc     si
        jmp     .lead
.end:   mov     di, si
.find:  cmp     byte [di], 0
        je      .back
        inc     di
        jmp     .find
.back:  cmp     di, si
        je      .out
        cmp     byte [di-1], ' '
        jne     .pr
        dec     di
        jmp     .back
.pr:    mov     al, [si]
        call    putc
        inc     si
        cmp     si, di
        jb      .pr
.out:   pop     di
        pop     si
        pop     ax
        ret

print_hex32:
        push    eax
        shr     eax, 16
        call    print_hex16
        pop     eax
print_hex16:
        push    ax
        mov     al, ah
        call    print_hex8
        pop     ax
print_hex8:
        push    ax
        shr     al, 4
        call    .nib
        pop     ax
        push    ax
        and     al, 0Fh
        call    .nib
        pop     ax
        ret
.nib:   add     al, '0'
        cmp     al, '9'
        jbe     .p
        add     al, 7
.p:     jmp     putc

print_dec:                              ; AX
        push    ax
        push    cx
        push    dx
        push    bx
        mov     bx, 10
        xor     cx, cx
.div:   xor     dx, dx
        div     bx
        push    dx
        inc     cx
        or      ax, ax
        jnz     .div
.out:   pop     ax
        add     al, '0'
        call    putc
        loop    .out
        pop     bx
        pop     dx
        pop     cx
        pop     ax
        ret

%ifdef USB_BUILD
%include "usbinit.inc"
%endif

;-----------------------------------------------------------------------------
; Messages
;-----------------------------------------------------------------------------
%ifdef USB_BUILD
msg_banner      db      'EUSBASPI ', VERSION_STR
                db      ' - ASPI manager for USB mass storage', 13, 10, 0
%else
msg_banner      db      'AHCIASPI ', VERSION_STR
                db      ' - ASPI manager for ATAPI devices on AHCI', 13, 10, 0
%endif
msg_no386       db      '386 or better required', 13, 10, 0
msg_bad_bar     db      'AHCI controller has no memory BAR', 13, 10, 0
msg_vds_fail    db      'VDS lock of DMA buffers failed', 13, 10, 0
msg_bad_method  db      'Requested access method not available', 13, 10, 0
msg_no_a20      db      'Cannot enable A20 for flat mode', 13, 10, 0
%ifdef USB_BUILD
msg_no_dev      db      'No USB mass storage devices found', 13, 10, 0
msg_not_inst    db      'EUSBASPI not installed', 13, 10, 0
%else
msg_no_dev      db      'No ATAPI devices found', 13, 10, 0
msg_not_inst    db      'AHCIASPI not installed', 13, 10, 0
%endif
msg_bad_opt     db      'Unknown option ignored', 13, 10, 0
msg_ctl3        db      ' at PCI ', 0
msg_ctl4        db      ', ABAR ', 0
msg_crlf        db      13, 10, 0
msg_port        db      '  port ', 0
msg_id          db      '  ID ', 0
msg_colon       db      ': ', 0
msg_ide         db      '  IDE ', 0
msg_usb_port    db      '  USB port ', 0
msg_chain1      db      'Chained to ASPI manager "', 0
msg_chain2      db      '", ', 0
msg_chain3      db      ' adapter(s); this is host adapter ', 0
msg_master      db      ' master', 0
msg_slave       db      ' slave', 0
msg_open        db      ' (', 0
msg_dma         db      ', DMA)', 13, 10, 0
msg_pio         db      ', PIO)', 13, 10, 0
msg_too_many    db      ': too many devices, ignored', 13, 10, 0
t_disk          db      'direct access', 0
t_tape          db      'tape', 0
t_worm          db      'WORM', 0
t_cdrom         db      'CD/DVD', 0
t_optical       db      'optical', 0
t_other         db      'other', 0

%ifdef USB_BUILD
%include "hotplug.inc"
%endif
hot_end:                                ; end of the resident part with /H

init:
        pushad
        push    ds
        push    es
        push    fs
        push    gs
        mov     ax, cs
        mov     ds, ax
        mov     es, ax
        cld

        call    parse_cmdline           ; may set /Q before the banner
        cmp     byte [opt_quiet], 0
        jne     .nobanner
        mov     si, msg_banner
        call    print
.nobanner:

        call    check_386
        jnc     .cpu_ok
        mov     si, msg_no386
        jmp     init_fail
.cpu_ok:
        smsw    ax
        and     al, 1
        mov     [is_v86], al

        push    es                      ; VDS present?
        mov     ax, VDS_FLAG_SEG
        mov     es, ax
        test    byte [es:VDS_FLAG_OFS], VDS_FLAG_BIT
        pop     es
        setnz   byte [vds_ok]

        mov     ax, 4300h               ; XMS driver (for A20 handling)
        int     2Fh
        cmp     al, 80h
        jne     .noxms
        push    es
        mov     ax, 4310h
        int     2Fh
        mov     [xms_entry], bx
        mov     [xms_entry+2], es
        pop     es
.noxms:
        call    usb_setup_dma
        jnc     .dma_ok
        mov     si, msg_vds_fail
        jmp     init_fail
.dma_ok:
        call    find_prev_manager
        call    choose_method           ; flat mode or INT 15h for MMIO
        jnc     .method_ok
        jmp     init_fail
.method_ok:
        movzx   cx, byte [hot_slots]    ; hot plug: empty slots first
        jcxz    .noslots
        mov     [dev_count], cl
        xor     bx, bx
.slot:  mov     byte [dev_trans+bx], TR_EMPTY
        mov     byte [dev_type+bx], 0
        inc     bx
        loop    .slot
.noslots:
        call    usb_scan
.scanned:
        cmp     byte [dev_count], 0
        jne     .found
        mov     si, msg_no_dev
        jmp     init_fail
.found:
        les     bx, [req_ptr]           ; resident: up to the last DMA page used
        mov     ax, [ud_base]
        add     ax, [ud_next]
        cmp     byte [hot_slots], 0     ; hot plug: all DMA pages, its code
        je      .rqend
        mov     ax, hot_end
        mov     byte [hot_active], 1
.rqend: mov     [es:bx+RQ_END], ax
        cmp     byte [opt_verbose], 0
        je      .sized
        push    ax
        mov     si, msg_resident
        call    print
        pop     ax
        call    print_dec
        mov     si, msg_bytes
        call    print
        mov     ax, cs
        call    print_hex16
        mov     si, msg_crlf
        call    print
        les     bx, [req_ptr]
.sized:
        mov     [es:bx+RQ_END+2], cs
        mov     word [es:bx+RQ_STATUS], 0100h
        jmp     init_out

; SI = error message
init_fail:
        call    print
        mov     si, msg_not_inst
        call    print
        les     bx, [req_ptr]
        mov     word [es:bx+RQ_END], 0
        mov     [es:bx+RQ_END+2], cs
        mov     word [es:bx+RQ_STATUS], 810Ch
init_out:
        pop     gs
        pop     fs
        pop     es
        pop     ds
        popad
        ret

; CF if the CPU is older than a 386
check_386:
        pushf
        pushf
        pop     ax
        and     ax, 0FFFh
        push    ax
        popf
        pushf
        pop     ax
        and     ax, 0F000h
        cmp     ax, 0F000h              ; 8086/88: bits 12-15 always set
        je      .old
        pushf
        pop     ax
        or      ax, 7000h
        push    ax
        popf
        pushf
        pop     ax
        test    ax, 7000h               ; 286 real mode: bits 12-14 clear
        jz      .old
        popf
        clc
        ret
.old:   popf
        stc
        ret

;-----------------------------------------------------------------------------
; Command line parsing
;-----------------------------------------------------------------------------
parse_cmdline:
        push    es
        les     bx, [req_ptr]
        les     si, [es:bx+RQ_CMDLINE]
.skipname:                              ; skip the driver file name
        mov     al, [es:si]
        call    is_eol
        je      .done
        cmp     al, ' '
        jbe     .args
        cmp     al, '/'
        je      .args
        inc     si
        jmp     .skipname
.args:  mov     al, [es:si]
        call    is_eol
        je      .done
        inc     si
        cmp     al, '/'
        jne     .args
        mov     al, [es:si]
        call    upcase
        inc     si
        cmp     al, 'M'
        je      .opt_m
        cmp     al, 'T'
        je      .opt_t
        cmp     al, 'Q'
        je      .opt_q
        cmp     al, 'V'
        je      .opt_v
        cmp     al, 'I'
        je      .opt_i
        cmp     al, 'R'
        je      .opt_r
        cmp     al, 'X'
        je      .opt_x
        cmp     al, 'H'
        je      .opt_h
.bad:   push    si
        mov     si, msg_bad_opt
        call    print
        pop     si
        jmp     .args

.opt_t: call    get_colon
        jc      .bad
        call    get_number
        jc      .bad
        or      ax, ax
        jz      .bad
        mov     cx, 182
        mul     cx
        mov     cx, 10
        div     cx
        mov     [tmo_ticks], ax
        jmp     .args
.opt_q: mov     byte [opt_quiet], 1
        jmp     .args
.opt_v: mov     byte [opt_verbose], 1
        jmp     .args
.opt_x: mov     byte [opt_xhci], 1
        jmp     .args
.opt_h: mov     byte [hot_slots], 2     ; /H or /H:n hot plug slots
        cmp     byte [es:si], ':'
        jne     .args
        inc     si
        call    get_number
        jc      .bad
        or      ax, ax
        jz      .bad
        cmp     ax, MAX_DEV
        ja      .bad
        mov     [hot_slots], al
        jmp     .args
.opt_m: call    get_colon
        jc      .bad
        mov     al, [es:si]
        inc     si
        call    upcase
        mov     ah, AM_FLAT
        cmp     al, 'F'
        je      .setm
        mov     ah, AM_BIOS
        cmp     al, 'B'
        jne     .bad
.setm:  mov     [opt_method], ah
        jmp     .args
.opt_r: mov     di, kw_rawinq           ; /RAWINQ
        call    match_kw
        jc      .bad
        mov     byte [inq_scsi2], 0
        jmp     .args
.opt_i: mov     al, [es:si]             ; /ID:name
        call    upcase
        cmp     al, 'D'
        jne     .bad
        inc     si
        call    get_colon
        jc      .bad
        mov     di, mgr_id
        mov     cx, 16
.idfill:
        mov     al, [es:si]
        call    is_eol
        je      .idpad
        cmp     al, ' '
        jbe     .idpad
        cmp     al, '/'
        je      .idpad
        mov     [di], al
        inc     di
        inc     si
        loop    .idfill
        jmp     .args
.idpad: mov     byte [di], ' '
        inc     di
        loop    .idpad
        jmp     .args
.done:  pop     es
        ret

; compare the rest of an option at ES:SI with the keyword at DI (upper case,
; zero terminated).  Advances SI on a match, CF otherwise.
match_kw:
        push    si
.c:     mov     ah, [di]
        or      ah, ah
        jz      .ok
        mov     al, [es:si]
        call    upcase
        cmp     al, ah
        jne     .no
        inc     si
        inc     di
        jmp     .c
.ok:    add     sp, 2
        clc
        ret
.no:    pop     si
        stc
        ret

kw_rawinq       db      'AWINQ', 0

is_eol: cmp     al, 0
        je      .y
        cmp     al, 0Dh
        je      .y
        cmp     al, 0Ah
.y:     ret

upcase: cmp     al, 'a'
        jb      .n
        cmp     al, 'z'
        ja      .n
        sub     al, 20h
.n:     ret

get_colon:
        cmp     byte [es:si], ':'
        jne     .no
        inc     si
        clc
        ret
.no:    stc
        ret

; decimal number at ES:SI -> AX.  CF if none.
get_number:
        xor     ax, ax
        xor     cx, cx
.dig:   mov     dl, [es:si]
        sub     dl, '0'
        cmp     dl, 9
        ja      .end
        mov     dh, 0
        imul    ax, ax, 10
        add     ax, dx
        inc     si
        inc     cx
        jmp     .dig
.end:   cmp     cx, 1
        ret                             ; CF if cx == 0

;-----------------------------------------------------------------------------
; Look for an ASPI manager loaded before us and chain to it
;-----------------------------------------------------------------------------
find_prev_manager:
        mov     byte [hdr_name+7], '#'  ; DOS must not find our own header
        mov     ax, 3D00h
        mov     dx, prev_name
        int     21h
        mov     byte [hdr_name+7], '$'
        jc      .none
        mov     bx, ax
        mov     ax, 4402h               ; IOCTL read: entry point
        mov     dx, prev_entry
        mov     cx, 4
        int     21h
        pushf
        mov     ah, 3Eh
        int     21h
        popf
        jc      .none
        cmp     dword [prev_entry], 0
        je      .none
        mov     di, prev_srb            ; host adapter inquiry, adapter 0
        mov     cx, 64
        xor     al, al
        push    es
        push    cs
        pop     es
        rep     stosb
        pop     es
        push    cs
        push    word prev_srb
        call    far [prev_entry]
        add     sp, 4
        mov     cx, 0FFFFh              ; managers that complete later
.wait:  cmp     byte [prev_srb+SRB_Status], SS_PENDING
        jne     .done
        loop    .wait
.done:  cmp     byte [prev_srb+SRB_Status], SS_COMP
        jne     .bad
        mov     al, [prev_srb+HA_Count]
        or      al, al
        jz      .bad
        mov     [prev_count], al
        cmp     byte [opt_quiet], 0
        jne     .out
        mov     si, msg_chain1
        call    print
        mov     cx, 16
        mov     si, prev_srb+HA_ManagerId
.name:  lodsb
        call    putc
        loop    .name
        mov     si, msg_chain2
        call    print
        movzx   ax, byte [prev_count]
        call    print_dec
        mov     si, msg_chain3
        call    print
        movzx   ax, byte [prev_count]
        call    print_dec
        mov     si, msg_crlf
        call    print
.out:   ret
.bad:   mov     dword [prev_entry], 0
.none:  mov     byte [prev_count], 0
        ret

prev_name       db      'SCSIMGR$', 0
prev_srb        times 64 db 0

