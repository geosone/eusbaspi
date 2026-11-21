; PCI4G.SYS - move the memory BARs of USB and disk controllers that the
; firmware placed above 4 GB ("Above 4G Decoding" in the BIOS setup) into
; free PCI address space below 4 GB, where the DOS drivers (EUSBASPI,
; AHCIASPI) reach them.  Load it in CONFIG.SYS before them; it does its work
; while it is initialised and stays 0 bytes resident.
;
;   DEVICE=PCI4G.SYS [/V]        /V: also list the BARs and the free space
;
; The free space is the part of the gap between the end of RAM below 4 GB
; (INT 15h E820h) and FEC00000h (I/O APIC and chipset) that no E820h range,
; BAR, expansion ROM or bridge window uses.  A device on bus 0 gets an
; address in it, a device behind a PCI bridge one in the bridge's (32-bit)
; memory window.  Only devices of class 0C03h (USB) and 01h (mass storage)
; are moved; graphics cards and the rest stay where the firmware put them.
;
; Copyright (C) 2026 GeosOne.  GNU General Public License version 2 or later.

        cpu     386
        bits    16
        org     0

MAXUSED equ     256                     ; used memory ranges (lo, hi)
MAXCAND equ     16                      ; BARs to move
MAXBRG  equ     32                      ; bridges (secondary bus, window)
HOLE_TOP equ    0FEBFFFFFh

        dd      -1
        dw      8000h, strategy, interrupt
        db      'PCI4G$$$'

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
        cld
        les     bx, [req]
        cmp     byte [es:bx+2], 0       ; INIT only
        jne     .done
        lds     si, [es:bx+12h]         ; command line
.arg:   lodsb
        cmp     al, 13
        je      .args
        cmp     al, 10
        je      .args
        or      al, al
        jz      .args
        cmp     al, '/'
        jne     .arg
        lodsb
        and     al, 0DFh
        cmp     al, 'V'
        jne     .arg
        mov     byte [cs:verbose], 1
        jmp     .arg
.args:  push    cs
        pop     ds
        call    main
        les     bx, [req]
.done:  mov     word [es:bx+0Eh], 0     ; nothing stays resident
        mov     [es:bx+10h], cs
        mov     word [es:bx+3], 0100h
        pop     es
        pop     ds
        popad
        retf

verbose db      0
lastbus db      0
nused   dw      0
ncand   dw      0
nbrg    dw      0
hole_lo dd      0
bdf     dw      0                       ; current function

;-----------------------------------------------------------------------------
main:
        mov     ax, 0B101h
        xor     edi, edi
        int     1Ah
        jc      .nopci
        cmp     edx, 'PCI '
        jne     .nopci
        mov     [lastbus], cl
        call    e820
        jc      .nomap
        call    scan
        cmp     byte [verbose], 0
        je      .move
        mov     si, msg_hole
        call    print
        mov     eax, [hole_lo]
        call    hex32
        mov     al, '-'
        call    putc
        mov     eax, HOLE_TOP
        call    hex32
        mov     si, msg_crlf
        call    print
.move:  xor     bp, bp
.cand:  cmp     bp, [ncand]
        jae     .end
        call    place
        add     bp, 16
        jmp     .cand
.end:   cmp     word [ncand], 0
        jne     .ret
        cmp     byte [verbose], 0
        je      .ret
        mov     si, msg_none
        jmp     print
.nopci: mov     si, msg_nopci
        jmp     print
.nomap: mov     si, msg_nomap
        jmp     print
.ret:   ret

;-----------------------------------------------------------------------------
; e820: hole_lo = end of the RAM below 4 GB (1 MB aligned); the other ranges
; above it go to the used list.  CF = no E820h map.
;-----------------------------------------------------------------------------
e820:
        xor     ebx, ebx
        xor     bp, bp                  ; entries
.next:  push    cs
        pop     es
        mov     di, e820buf
        mov     dword [di+20], 1        ; ACPI 3.0: valid
        mov     eax, 0E820h
        mov     ecx, 24
        mov     edx, 'PAMS'
        int     15h
        jc      .last
        cmp     eax, 'PAMS'
        jne     .last
        inc     bp
        push    ebx
        call    .entry
        pop     ebx
        or      ebx, ebx
        jnz     .next
.last:  or      bp, bp
        jz      .none
        mov     eax, [hole_lo]          ; 1 MB aligned
        add     eax, 0FFFFFh
        jc      .none
        and     eax, 0FFF00000h
        mov     [hole_lo], eax
        clc
        ret
.none:  stc
        ret

.entry: cmp     dword [e820buf+4], 0    ; at or above 4 GB
        jne     .skip
        mov     eax, [e820buf]          ; EAX = lo, EDX = hi
        mov     edx, [e820buf+8]
        cmp     dword [e820buf+12], 0
        jne     .top
        or      edx, edx
        jz      .skip
        dec     edx
        add     edx, eax
        jnc     .clip
.top:   mov     edx, 0FFFFFFFFh
.clip:  cmp     dword [e820buf+16], 1   ; RAM, ACPI tables, ACPI NVS
        je      .ram
        cmp     dword [e820buf+16], 3
        je      .ram
        cmp     dword [e820buf+16], 4
        je      .ram
        jmp     add_used                ; reserved: not for BARs
.ram:   inc     edx                     ; the end of RAM
        jz      .full
        cmp     edx, [hole_lo]
        jbe     .skip
        mov     [hole_lo], edx
.skip:  ret
.full:  mov     dword [hole_lo], 0FFF00000h
        ret

; EAX = lo, EDX = hi -> used list
add_used:
        push    di
        mov     di, [nused]
        cmp     di, MAXUSED * 8
        jae     .full
        mov     [used+di], eax
        mov     [used+di+4], edx
        add     word [nused], 8
.full:  pop     di
        ret

;-----------------------------------------------------------------------------
; scan: every function's BARs, ROM and bridge windows
;-----------------------------------------------------------------------------
scan:
        xor     bx, bx
.fn:    mov     [bdf], bx
        xor     di, di
        call    rd32
        cmp     cx, 0FFFFh
        je      .absent
        mov     di, 08h
        call    rd32
        shr     ecx, 8
        mov     [class], ecx
        mov     di, 0Ch
        call    rd32
        shr     ecx, 16
        mov     [htype], cl
        cmp     dword [class], 060000h  ; host bridge: chipset registers
        jb      .bars
        cmp     dword [class], 0600FFh
        jbe     .multi
.bars:  mov     al, [htype]
        and     al, 7Fh
        mov     dx, 24h + 4             ; type 0: BARs 10h-24h, ROM 30h
        mov     si, 30h
        cmp     al, 0
        je      .go
        cmp     al, 1
        jne     .multi
        mov     dx, 14h + 4             ; bridge: BARs 10h-14h, ROM 38h
        mov     si, 38h
        call    bridge
.go:    call    bars
        call    rom
.multi: test    bl, 7                   ; function 0 alone?
        jnz     .nextfn
        test    byte [htype], 80h
        jnz     .nextfn
        or      bl, 7
.nextfn:
        inc     bl
        jnz     .fn
        cmp     bh, [lastbus]
        jae     .ret
        inc     bh
        jmp     .fn
.absent:
        test    bl, 7
        jnz     .nextfn
        or      bl, 7
        jmp     .nextfn
.ret:   ret

class   dd      0
htype   db      0
bar_lo  dd      0
bar_hi  dd      0
bar_end dw      0

; the BARs 10h..DX-4 of [bdf]
bars:
        push    bx
        push    si
        mov     [bar_end], dx
        mov     di, 10h
.bar:   cmp     di, [bar_end]
        jae     .ret
        call    rd32
        test    cl, 1                   ; I/O
        jnz     .io
        mov     [bar_lo], ecx
        mov     dword [bar_hi], 0
        mov     al, cl
        and     al, 6
        cmp     al, 4                   ; 64 bit
        jne     .b32
        add     di, 4
        call    rd32
        sub     di, 4
        mov     [bar_hi], ecx
.b32:   mov     eax, [bar_lo]
        and     eax, 0FFFFFFF0h
        or      eax, [bar_hi]
        jz      .nextb                  ; not assigned
        mov     esi, 0FFFFFFF0h
        call    bar_size                ; -> EAX
        or      eax, eax
        jz      .nextb
        cmp     dword [bar_hi], 0
        jne     .high
        mov     edx, [bar_lo]
        and     edx, 0FFFFFFF0h
        xchg    eax, edx
        dec     edx
        add     edx, eax
        jnc     .add
        mov     edx, 0FFFFFFFFh
.add:   call    add_used
        jmp     .nextb
.high:  call    candidate
.nextb: mov     al, [bar_lo]
        and     al, 6
        cmp     al, 4
        jne     .io
        add     di, 4
.io:    add     di, 4
        jmp     .bar
.ret:   pop     si
        pop     bx
        ret

; a BAR above 4 GB of [bdf] at DI, size EAX: one to move?
candidate:
        push    dx
        mov     edx, [class]
        shr     edx, 8
        cmp     dx, 0C03h               ; USB
        je      .yes
        cmp     dh, 01h                 ; mass storage (IDE, SATA, RAID, NVMe)
        je      .yes
        pop     dx
        cmp     byte [verbose], 0
        je      .ret
        push    eax
        call    print_bdf
        mov     ax, di
        mov     [pb_reg], al
        mov     eax, [bar_lo]
        mov     [pb_lo], eax
        mov     eax, [bar_hi]
        mov     [pb_hi], eax
        pop     eax
        mov     si, msg_left
        call    print_bar
        mov     si, msg_crlf
        call    print
.ret:   ret
.yes:   pop     dx
        push    di
        mov     si, [ncand]
        cmp     si, MAXCAND * 16
        jae     .full
        mov     bx, [bdf]
        mov     [cand+si], bx
        mov     [cand+si+2], di
        mov     [cand+si+4], eax
        mov     ecx, [bar_lo]
        mov     [cand+si+8], ecx
        mov     ecx, [bar_hi]
        mov     [cand+si+12], ecx
        add     word [ncand], 16
.full:  pop     di
        ret

; the expansion ROM at SI of [bdf], if it decodes
rom:
        mov     di, si
        call    rd32
        test    cl, 1
        jz      .ret
        mov     [bar_lo], ecx
        mov     dword [bar_hi], 0
        mov     eax, ecx
        and     eax, 0FFFFF800h
        jz      .ret
        mov     esi, 0FFFFF800h
        call    bar_size
        or      eax, eax
        jz      .ret
        mov     edx, [bar_lo]
        and     edx, 0FFFFF800h
        xchg    eax, edx
        dec     edx
        add     edx, eax
        jnc     .add
        mov     edx, 0FFFFFFFFh
.add:   jmp     add_used
.ret:   ret

; size of the memory BAR at DI of [bdf] (bar_lo, bar_hi as read, ESI = its
; address mask) -> EAX, 0 = none or 4 GB and more.  Memory decoding is off
; meanwhile.
bar_size:
        pushf
        cli
        call    mem_off
        mov     ecx, 0FFFFFFFFh
        call    wr32
        call    rd32
        mov     [szlo], ecx
        mov     ecx, [bar_lo]
        call    wr32
        mov     dword [szhi], 0FFFFFFFFh
        mov     al, [bar_lo]
        and     al, 6
        cmp     al, 4                   ; 64 bit: the upper half too
        jne     .on
        add     di, 4
        mov     ecx, 0FFFFFFFFh
        call    wr32
        call    rd32
        mov     [szhi], ecx
        mov     ecx, [bar_hi]
        call    wr32
        sub     di, 4
.on:    call    mem_on
        popf
        xor     eax, eax
        cmp     dword [szhi], 0FFFFFFFFh
        jne     .ret
        mov     eax, [szlo]
        and     eax, esi
        neg     eax
.ret:   ret

; memory decoding of [bdf] off / back on (command register)
mem_off:
        push    di
        mov     di, 04h
        call    rd32
        mov     [cmdreg], cx
        and     cl, ~2
        call    wr16
        pop     di
        ret
mem_on: push    di
        mov     di, 04h
        mov     cx, [cmdreg]
        call    wr16
        pop     di
        ret

szlo    dd      0
szhi    dd      0
cmdreg  dw      0

; a PCI-PCI bridge: its windows below 4 GB are used; its secondary bus
; and 32-bit memory window go to the bridge list
bridge:
        push    dx
        push    si
        mov     di, 18h
        call    rd32
        mov     al, ch                  ; secondary bus
        mov     di, 20h                 ; memory window
        call    rd32
        mov     edx, ecx
        and     edx, 0FFF00000h
        or      edx, 0FFFFFh
        shl     ecx, 16
        and     ecx, 0FFF00000h
        mov     si, [nbrg]
        cmp     si, MAXBRG * 12
        jae     .nolist
        mov     [brg+si], al
        mov     [brg+si+4], ecx
        mov     [brg+si+8], edx
        add     word [nbrg], 12
.nolist:
        cmp     ecx, edx
        ja      .pref
        mov     eax, ecx
        call    add_used
.pref:  mov     di, 24h                 ; prefetchable window
        call    rd32
        mov     edx, ecx
        and     edx, 0FFF00000h
        or      edx, 0FFFFFh
        shl     ecx, 16
        and     ecx, 0FFF00000h
        mov     eax, ecx
        mov     di, 24h
        push    eax
        call    rd32
        pop     eax
        and     cl, 0Fh
        cmp     cl, 1                   ; 64 bit window
        jne     .p32
        mov     di, 28h
        call    rd32
        or      ecx, ecx                ; base above 4 GB
        jnz     .ret
        mov     di, 2Ch
        call    rd32
        or      ecx, ecx
        jz      .p32
        mov     edx, 0FFFFFFFFh         ; reaches above 4 GB
.p32:   cmp     eax, edx
        ja      .ret
        call    add_used
.ret:   pop     si
        pop     dx
        ret

;-----------------------------------------------------------------------------
; place: move candidate BP (cand+BP) below 4 GB
;-----------------------------------------------------------------------------
place:
        mov     si, bp                  ; (BP would address the stack)
        mov     ax, [cand+si]
        mov     [bdf], ax
        mov     ax, [cand+si+2]
        mov     [c_reg], ax
        mov     [pb_reg], al
        mov     eax, [cand+si+8]
        mov     [pb_lo], eax
        mov     eax, [cand+si+12]
        mov     [pb_hi], eax
        mov     eax, [cand+si+4]
        mov     [size], eax
        call    print_bdf
        mov     eax, [size]
        mov     ecx, eax                ; alignment: the size, 4 KB at least
        cmp     ecx, 1000h
        jae     .al
        mov     ecx, 1000h
.al:    dec     ecx
        not     ecx
        mov     [amask], ecx
        mov     ebx, [hole_lo]          ; window: bus 0 the hole,
        mov     edx, HOLE_TOP           ; else the bridge's
        mov     al, [bdf+1]
        or      al, al
        jz      .win
        xor     si, si
.brg:   cmp     si, [nbrg]
        jae     .nowin
        cmp     [brg+si], al
        je      .found
        add     si, 12
        jmp     .brg
.found: mov     ebx, [brg+si+4]
        mov     edx, [brg+si+8]
        cmp     ebx, edx
        ja      .nowin
.win:   ; top down: a = (hi - size + 1) & mask
        mov     eax, edx
        sub     eax, [size]
        jc      .nofit
        inc     eax
.try:   and     eax, [amask]
        cmp     eax, ebx
        jb      .nofit
        mov     ecx, eax                ; ECX = last byte
        add     ecx, [size]
        dec     ecx
        cmp     ecx, edx
        ja      .nofit
        xor     si, si
.ov:    cmp     si, [nused]
        jae     .free
        cmp     [used+si], ebx          ; the window itself, or that of a
        ja      .ovc                    ; bridge above: not in the way
        cmp     [used+si+4], edx
        jae     .ovn
.ovc:   cmp     eax, [used+si+4]        ; a <= used hi
        ja      .ovn
        cmp     ecx, [used+si]          ; and last >= used lo
        jb      .ovn
        mov     eax, [used+si]          ; below that range
        sub     eax, [size]
        jc      .nofit
        jmp     .try
.ovn:   add     si, 8
        jmp     .ov
.free:  push    eax
        mov     edx, ecx
        call    add_used
        pop     eax
        call    program
        mov     eax, [size]
        mov     si, msg_bar
        call    print_bar
        mov     si, msg_to
        call    print
        mov     eax, [newbase]
        call    hex32
        jmp     .size
.nowin: mov     si, msg_nowin
        jmp     .fail
.nofit: mov     si, msg_nofit
.fail:  push    si
        mov     eax, [size]
        mov     si, msg_bar
        call    print_bar
        pop     si
        call    print
.size:  mov     si, msg_crlf
        jmp     print

size    dd      0
c_reg   dw      0
amask   dd      0
newbase dd      0

; write BAR cand+BP = EAX (upper half 0), memory decoding off meanwhile
program:
        mov     [newbase], eax
        mov     di, [c_reg]
        pushf
        cli
        call    mem_off
        mov     ecx, [pb_lo]
        and     ecx, 0Fh                ; type bits
        or      ecx, [newbase]
        call    wr32
        add     di, 4
        xor     ecx, ecx
        call    wr32
        call    mem_on
        popf
        ret

;-----------------------------------------------------------------------------
; PCI configuration access of [bdf] (PCI BIOS), DI = register
rd32:   push    ax
        push    bx
        mov     bx, [bdf]
        mov     ax, 0B10Ah
        int     1Ah
        pop     bx
        pop     ax
        ret
wr32:   push    ax
        push    bx
        mov     bx, [bdf]
        mov     ax, 0B10Dh
        int     1Ah
        pop     bx
        pop     ax
        ret
wr16:   push    ax
        push    bx
        mov     bx, [bdf]
        mov     ax, 0B10Ch
        int     1Ah
        pop     bx
        pop     ax
        ret

;-----------------------------------------------------------------------------
; output (DOS, and port E9h with -DE9LOG)
putc:   push    ax
        push    dx
%ifdef E9LOG
        out     0E9h, al
%endif
        mov     dl, al
        mov     ah, 02h
        int     21h
        pop     dx
        pop     ax
        ret
print:  push    ax
.l:     lodsb
        or      al, al
        jz      .e
        call    putc
        jmp     .l
.e:     pop     ax
        ret
hex32:  push    eax
        shr     eax, 16
        call    hex16
        pop     eax
hex16:  xchg    al, ah
        call    hex8
        xchg    al, ah
hex8:   push    ax
        shr     al, 4
        call    .dig
        pop     ax
.dig:   push    ax
        and     al, 0Fh
        add     al, '0'
        cmp     al, '9'
        jbe     .p
        add     al, 7
.p:     call    putc
        pop     ax
        ret

; "PCI4G: bb:dd.f"
print_bdf:
        push    ax
        mov     si, msg_name
        call    print
        mov     al, [bdf+1]
        call    hex8
        mov     al, ':'
        call    putc
        mov     al, [bdf]
        shr     al, 3
        call    hex8
        mov     al, '.'
        call    putc
        mov     al, [bdf]
        and     al, 7
        add     al, '0'
        call    putc
        pop     ax
        ret

; SI = text, EAX = size: "<text>10h hhhhhhhhllllllll (64K)" of pb_*
print_bar:
        push    eax
        call    print
        mov     al, [pb_reg]
        call    hex8
        mov     si, msg_h
        call    print
        mov     eax, [pb_hi]
        call    hex32
        mov     eax, [pb_lo]
        and     al, 0F0h
        call    hex32
        pop     eax
        mov     si, msg_lpar            ; size in KB
        call    print
        shr     eax, 10
        call    dec32
        mov     si, msg_kb
        jmp     print

pb_reg  db      0
pb_lo   dd      0
pb_hi   dd      0

dec32:  push    eax
        push    ecx
        push    edx
        mov     ecx, 10
        xor     dx, dx
        push    dx
.div:   xor     edx, edx
        div     ecx
        add     dl, '0'
        push    dx
        or      eax, eax
        jnz     .div
.out:   pop     ax
        or      al, al
        jz      .end
        call    putc
        jmp     .out
.end:   pop     edx
        pop     ecx
        pop     eax
        ret

msg_name  db    'PCI4G: ', 0
msg_bar   db    ' BAR ', 0
msg_left  db    ' left above 4 GB: BAR ', 0
msg_h     db    'h ', 0
msg_to    db    ' -> ', 0
msg_lpar  db    ' (', 0
msg_kb    db    'K)', 0
msg_nowin db    ': the bridge has no memory window', 0
msg_nofit db    ': no free space below 4 GB', 0
msg_hole  db    'PCI4G: free for BARs between ', 0
msg_none  db    'PCI4G: no USB or disk controller BAR above 4 GB', 13, 10, 0
msg_nopci db    'PCI4G: no PCI BIOS', 13, 10, 0
msg_nomap db    'PCI4G: no INT 15h E820h memory map, nothing moved', 13, 10, 0
msg_crlf  db    13, 10, 0

e820buf times 24 db 0
used    times MAXUSED * 8 db 0
cand    times MAXCAND * 16 db 0         ; bdf, register, size, lo, hi
brg     times MAXBRG * 12 db 0          ; secondary bus, window lo, hi
