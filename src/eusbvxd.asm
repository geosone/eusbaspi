;=============================================================================
; EUSBVXD.386 - Windows 3.1x enhanced mode helper for EUSBASPI.SYS
;
; The USB host controllers OHCI, EHCI and xHCI are programmed through
; memory mapped registers.  In real mode EUSBASPI reaches them in flat
; ("unreal") mode, under DOS memory managers with INT 15h AH=87h.  In a
; Windows enhanced mode DOS box neither works: the V86 code may not leave
; V86 mode and Windows does not pass block moves through to physical
; addresses.  This VxD maps the register windows of all USB controllers
; at Device_Init and lets EUSBASPI read and write them through its V86 API:
;
;   INT 2Fh, AX=1684h, BX=EUSBVXD_ID -> ES:DI = API entry (0:0 if absent)
;   call far ES:DI with
;     AX=0  get version                 -> AX = version, CF clear
;     AX=1  read  dword at physical EDX  -> EAX = value
;     AX=2  write dword ECX to physical EDX
;   CF set: not a mapped address or unknown function
;
; SYSTEM.INI, [386Enh]:  device=C:\EUSBVXD.386
;
; Built with NASM (OMF) and the OpenWatcom linker (format windows vxd), no
; DDK headers needed.  Single object, the DDB is at its start.
;=============================================================================

        bits    32

EUSBVXD_ID      equ     3BD5h           ; device ID (not registered)
VXD_VERSION     equ     0100h

; VMM
VMM_DEVICE_ID   equ     0001h
%macro VMMcall 1
        int     20h
        dd      (VMM_DEVICE_ID << 16) + %1
%endmacro
_MapPhysToLinear equ    006Ch

; control messages
Device_Init     equ     0001h

; Client_Reg_Struc (EBP in API procedures)
Client_EDX      equ     14h
Client_ECX      equ     18h
Client_EAX      equ     1Ch
Client_EFlags   equ     2Ch

MAX_WIN         equ     8               ; mapped register windows
MAX_WIN_SIZE    equ     100000h         ; at most 1 MB per window

segment _LTEXT public align=16 use32 class=CODE
group _LGROUP _LTEXT
group FLAT

;--- Device Descriptor Block (must stay first) -------------------------------
global EUSBVXD_DDB
EUSBVXD_DDB:
        dd      0                       ; next
        dw      030Ah                   ; DDK version 3.10
        dw      EUSBVXD_ID              ; device ID
        db      1, 0                    ; version 1.0
        dw      0                       ; flags
        db      'EUSBVXD '              ; name
        dd      80000000h               ; init order: undefined
        dd      control_proc wrt FLAT   ; control procedure
        dd      v86_api wrt FLAT        ; V86 API procedure
        dd      0                       ; PM API procedure
        dd      0, 0                    ; API CS:IP (set by VMM)
        dd      0                       ; reference data
        dd      0, 0                    ; service table

;--- data ---------------------------------------------------------------------
nwin            dd      0
win_phys        times MAX_WIN dd 0      ; physical base
win_size        times MAX_WIN dd 0
win_lin         times MAX_WIN dd 0      ; linear base

;--- control procedure --------------------------------------------------------
control_proc:
        cmp     eax, Device_Init
        jne     .ok
        call    map_usb_controllers
.ok:    clc                             ; never fail: Windows starts anyway
        ret

; PCI configuration register EDI of bus/device/function EBX (mechanism #1)
pci_rd:
        mov     eax, ebx
        shl     eax, 8
        or      eax, edi
        or      eax, 80000000h
        mov     dx, 0CF8h
        out     dx, eax
        mov     dx, 0CFCh
        in      eax, dx
        ret
pci_wr:                                 ; ECX = value
        push    ecx
        mov     eax, ebx
        shl     eax, 8
        or      eax, edi
        or      eax, 80000000h
        mov     dx, 0CF8h
        out     dx, eax
        pop     eax
        mov     dx, 0CFCh
        out     dx, eax
        ret

; find all USB controllers with a memory BAR0 and map their registers
map_usb_controllers:
        pushad
        xor     ebx, ebx                ; bus:device.function
.next:  xor     edi, edi
        call    pci_rd
        cmp     ax, 0FFFFh
        je      .skipdev
        mov     edi, 08h                ; class code
        call    pci_rd
        shr     eax, 8
        cmp     eax, 0C0310h            ; OHCI
        je      .usb
        cmp     eax, 0C0320h            ; EHCI
        je      .usb
        cmp     eax, 0C0330h            ; xHCI
        je      .usb
        jmp     .nextfn
.usb:   call    map_bar0
.nextfn:
        test    bl, 7                   ; multi-function device?
        jnz     .inc
        mov     edi, 0Ch
        call    pci_rd
        test    eax, 00800000h
        jnz     .inc
.skipdev:
        or      bl, 7                   ; next device
.inc:   inc     ebx
        cmp     ebx, 10000h             ; all 256 buses
        jb      .next
        popad
        ret

; BAR0 of [EBX] (memory, 32 bit or 64 bit below 4 GB) -> next window
map_bar0:
        mov     esi, [nwin]
        cmp     esi, MAX_WIN
        jae     .out
        mov     edi, 10h
        call    pci_rd
        test    al, 1                   ; I/O BAR: nothing to map
        jnz     .out
        mov     ebp, eax                ; keep the original value
        mov     ecx, 0FFFFFFFFh         ; size it
        call    pci_wr
        call    pci_rd
        push    eax
        mov     ecx, ebp
        call    pci_wr
        pop     eax
        and     eax, 0FFFFFFF0h
        neg     eax                     ; size
        jz      .out
        cmp     eax, MAX_WIN_SIZE
        jbe     .size
        mov     eax, MAX_WIN_SIZE
.size:  and     ebp, 0FFFFFFF0h
        jz      .out
        mov     [win_phys+esi*4], ebp
        mov     [win_size+esi*4], eax
        push    dword 0                 ; flags
        push    eax                     ; size
        push    ebp                     ; physical address
        VMMcall _MapPhysToLinear
        add     esp, 12
        cmp     eax, 0FFFFFFFFh
        je      .out
        mov     [win_lin+esi*4], eax
        inc     dword [nwin]
.out:   ret

;--- V86 API ------------------------------------------------------------------
; EBX = VM handle, EBP = client registers
v86_api:
        movzx   eax, word [ebp+Client_EAX]
        cmp     eax, 0
        je      .version
        cmp     eax, 2
        ja      .fail
        mov     edx, [ebp+Client_EDX]   ; physical address -> linear
        xor     esi, esi
.find:  cmp     esi, [nwin]
        jae     .fail
        mov     ecx, edx
        sub     ecx, [win_phys+esi*4]
        jb      .nwin
        lea     edi, [ecx+3]
        cmp     edi, [win_size+esi*4]
        jb      .found
.nwin:  inc     esi
        jmp     .find
.found: add     ecx, [win_lin+esi*4]
        cmp     eax, 1
        jne     .write
        mov     eax, [ecx]
        mov     [ebp+Client_EAX], eax
        jmp     .ok
.write: mov     eax, [ebp+Client_ECX]
        mov     [ecx], eax
        jmp     .ok
.version:
        mov     word [ebp+Client_EAX], VXD_VERSION
.ok:    and     byte [ebp+Client_EFlags], 0FEh
        ret
.fail:  or      byte [ebp+Client_EFlags], 1
        ret
