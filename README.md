# EUSBASPI - ASPI manager for USB mass storage under DOS

`EUSBASPI.SYS` is an ASPI manager (device `SCSIMGR$`) for DOS. It makes USB mass storage devices - sticks, card readers, hard disks, CD/DVD and floppy drives - usable for DOS ASPI drivers such as `ASPIDISK.SYS` or `ASPICD.SYS`.

- USB controllers: UHCI, OHCI, EHCI (with companion controllers) and, with `/X`, xHCI (USB 3.x, up to 4 controllers)
- USB hubs (up to 5 levels)
- Bulk-Only and CBI devices
- hot plugging (`/H`): sticks can be changed while DOS runs
- Windows 3.x DOS boxes (with `EUSBVXD.386` for OHCI, EHCI and xHCI)
- together with other ASPI managers ([AHCIASPI](https://github.com/geosone/ahciaspi), ASPI8DOS, ...): EUSBASPI chains to a manager loaded before it

**Status:** first version. Tested in QEMU and VirtualBox (all four controller types, hubs, hot plugging, Windows for Workgroups 3.11), not yet on real hardware. Reports with the output of `DEVICE=EUSBASPI.SYS /V` and `ASPITEST` are welcome.

## Installation

Download the ZIP of the [latest release](https://github.com/geosone/eusbaspi/releases), unpack it to a disk or directory and run `INSTALL.EXE` (the [GeosOne DOS Installer](https://github.com/geosone/gdi)). It copies the files to `C:\EUSBASPI`, adds the driver to `CONFIG.SYS` and, if Windows 3.x is found, `EUSBVXD.386` to the `[386Enh]` section of its `SYSTEM.INI`. Options: upper memory, xHCI (`/X`), hot plugging (`/H`), and:

| Option | |
|---|---|
| USB drives as DOS drives | `ASPIDISK.SYS` |
| CD-ROM drive | `ASPICD.SYS` in `CONFIG.SYS`, `/D:ASPICD0` for `MSCDEX` or `SHSUCDX` in `AUTOEXEC.BAT` |

**ASPI drivers.** `ASPIDISK.SYS` and `ASPICD.SYS` are not free software and are not part of EUSBASPI. The installer finds them:
1. **on the installation disk:** copy them next to `INSTALL.EXE` (e.g. onto the EUSBASPI disk), and the installer installs these copies to `C:\EUSBASPI`;
2. otherwise **on the computer:** in `C:\DOS`, the `PATH`, the Windows directory or the paths named in `CONFIG.SYS`/`AUTOEXEC.BAT`.

The same holds for `MSCDEX.EXE` / `SHSUCDX.COM`. Options whose driver is found nowhere are left out.

`INSTALL /U` in `C:\EUSBASPI` removes the installation again.

Disable "USB legacy storage" (USB mass storage emulation) in the BIOS setup if possible: otherwise the BIOS keeps driving the controller.

By hand, `CONFIG.SYS`:

    DEVICE=C:\EUSBASPI\EUSBASPI.SYS
    DEVICE=C:\ASPI\ASPIDISK.SYS

and `SYSTEM.INI` of Windows 3.x, `[386Enh]`:

    device=C:\EUSBASPI\EUSBVXD.386

## Unplugging: USBSYNC

DOS, SMARTDRV and Windows keep written data in caches, and many sticks have a write cache of their own. Before unplugging a drive run `USBSYNC` (DOS, DOS box) or `USBSYNC.EXE` (Windows 3.1x, e.g. as a Program Manager icon). It writes the DOS buffers (and the 32-bit file access cache of Windows for Workgroups 3.11), the SMARTDRV cache and the drives' write caches (`SYNCHRONIZE CACHE` through ASPI) and then tells you that the drives can be unplugged. `/E` also stops (ejects) them, `/Q` shows messages only on errors, `/A` (DOS) uses all ASPI adapters.

## Switches

    DEVICE=EUSBASPI.SYS [/H[:n]] [/ID:name] [/M:F|B] [/T:s] [/X] [/Q] [/V]

| Switch | |
|---|---|
| `/H[:n]` | hot plugging: n drive slots (default 2) that get drive letters from ASPIDISK even when empty; the driver stays larger in memory |
| `/ID:name` | ASPI manager name (up to 16 characters, default `EUSBASPI`) |
| `/M:x` | register access: `F` flat real mode (not with EMM386 etc.), `B` BIOS INT 15h AH=87h; default: one that works |
| `/T:s` | command timeout in seconds (default 15) |
| `/X` | also take over xHCI (USB 3.x) controllers |
| `/Q`, `/V` | quiet, verbose |

## Building

Linux (or any system with the tools), GNU make:
- [NASM](https://nasm.us) for `EUSBASPI.SYS`, `EUSBVXD.386` and the tools,
- Open Watcom (`WATCOM=/opt/watcom`): the linker for `EUSBVXD.386`, the compiler for `USBSYNC.EXE`,
- the [GeosOne DOS Installer](https://github.com/geosone/gdi) (`../gdi/build/INSTALL.EXE`, or `make package GDI=path`) for the package.

| Command | |
|---|---|
| `make` | `build/EUSBASPI.SYS`, `build/EUSBVXD.386`, `build/USBSYNC.COM`, `build/USBSYNCW.EXE` (Windows, installed as `USBSYNC.EXE`), `build/ASPITEST.COM` |
| `make package` | `build/pkg/` and `build/eusbaspi.zip` |

## Tests

`test/run.sh` (QEMU, `USBHC=uhci|ohci|ehci|xhci`), `test/vbox.sh` (VirtualBox), `test/win311.sh` (Windows for Workgroups 3.11) and `test/mm.sh` (memory managers) need your own DOS boot disk image and, for some tests, `ASPIDISK.SYS`.

## License

GNU General Public License version 3, see [LICENSE](LICENSE).
