; QEXIT.COM - leave QEMU through isa-debug-exit (iobase 0F4h)
        org     100h
        mov     al, 0
        out     0F4h, al
        mov     ax, 4C00h
        int     21h
