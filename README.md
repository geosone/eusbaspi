# EUSBASPI - ASPI manager for USB mass storage under DOS

`EUSBASPI.SYS` is an ASPI manager (device `SCSIMGR$`) for DOS. It makes USB mass storage devices - sticks, card readers, hard disks, CD/DVD and floppy drives - usable for DOS ASPI drivers: the [OpenASPI](https://github.com/geosone/OpenAspi) drivers that come with it, or others such as Adaptec's `ASPIDISK.SYS` or `ASPICD.SYS`.

- USB controllers: UHCI, OHCI, EHCI (with companion controllers) and, with `/X`, xHCI (USB 3.x, up to 4 controllers)
- USB hubs (up to 5 levels)
- Bulk-Only and CBI devices
- hot plugging (`/H`): sticks can be changed while DOS runs
- Windows 3.x DOS boxes (with `EUSBVXD.386` for OHCI, EHCI and xHCI)
- together with other ASPI managers ([AHCIASPI](https://github.com/geosone/ahciaspi), ASPI8DOS, ...): EUSBASPI chains to a manager loaded before it

**Status:** first version. Tested in QEMU and VirtualBox (all four controller types, hubs, hot plugging, Windows for Workgroups 3.11), not yet on real hardware. Reports with the output of `DEVICE=EUSBASPI.SYS /V` and `ASPITEST` are welcome.

## Installation

Download the ZIP of the [latest release](https://github.com/geosone/eusbaspi/releases), unpack it to a disk or directory and run `INSTALL.EXE` (the [GeosOne DOS Installer](https://github.com/geosone/gdi)). It copies the files to `C:\OPENASPI` (shared with AHCIASPI and OpenASPI; the installer itself goes to `C:\OPENASPI\EUSBASPI`), adds the driver to `CONFIG.SYS` and, if Windows 3.x is found, `EUSBVXD.386` to the `[386Enh]` section of its `SYSTEM.INI`. Options: upper memory, xHCI (`/X`), hot plugging (`/H`), the PATH, and the OpenASPI drivers that come with it, loaded after `EUSBASPI.SYS`:

| Option | |
|---|---|
| Sticks, card readers, disks (on) | `ASPIDSK.SYS` |
| CD/DVD drives (on) | `ASPICDA.SYS /D:ASPICDA0`; [SHSUCDX](https://github.com/geosone/shsucd) in the place of `MSCDEX` (MS-DOS) or `NWCDEX` (DR-DOS) in `AUTOEXEC.BAT`, with digital CD audio through [VSBHDA](https://github.com/geosone/VSBHDA) (`CDAUDIO.COM`) |
| ZIP, LS-120, LS-240, Clik!, Jaz | `ASPIFDD.SYS` |
| Tape drives | `ASPITAP.SYS`, `TAPECTL.COM` |

With EUSBASPI already loaded, `ASPISCAN.COM` finds ZIP/LS-120 and tape drives and preselects their options. OpenASPI and SHSUCD have installers of their own; all of them share `C:\OPENASPI`, so each can be installed alone or together.

**CD-ROM.** The installer takes the CD-ROM extension in this order: `SHSUCDX.COM` of the installation disk, else `SHSUCDX`, `MSCDEX.EXE`, `NWCDEX.EXE` on the computer. An existing `MSCDEX`/`NWCDEX` line keeps its drivers and drive letters, starts SHSUCDX instead (SHSUCDX ignores the MSCDEX options it doesn't have) and gets `/D:ASPICDA0`; `INSTALL /U` puts MSCDEX or NWCDEX back.

**From 0.3:** remove 0.3 first (`INSTALL /U` in `C:\OPENASPI\EUSBASPI`): it loaded Adaptec's `ASPIDISK.SYS` and `ASPICD.SYS`, which use the same drives as the OpenASPI drivers.

`INSTALL /U` in `C:\OPENASPI\EUSBASPI` removes the installation again; `C:\OPENASPI` stays while AHCIASPI or OpenASPI are still there.

Disable "USB legacy storage" (USB mass storage emulation) in the BIOS setup if possible: otherwise the BIOS keeps driving the controller.

By hand, `CONFIG.SYS`:

    DEVICE=C:\OPENASPI\EUSBASPI.SYS
    DEVICE=C:\OPENASPI\ASPIDSK.SYS

and `SYSTEM.INI` of Windows 3.x, `[386Enh]`:

    device=C:\OPENASPI\EUSBVXD.386

## Unplugging: USBSYNC

DOS, SMARTDRV and Windows keep written data in caches, and many sticks have a write cache of their own. Before unplugging a drive run `USBSYNC` (DOS, DOS box) or `USBSYNC.EXE` (Windows 3.1x, e.g. as a Program Manager icon). It writes the DOS buffers (and the 32-bit file access cache of Windows for Workgroups 3.11), the SMARTDRV cache and the drives' write caches (`SYNCHRONIZE CACHE` through ASPI) and then tells you that the drives can be unplugged. `/E` also stops (ejects) them, `/Q` shows messages only on errors, `/A` (DOS) uses all ASPI adapters.

## Switches

    DEVICE=EUSBASPI.SYS [/H[:n]] [/ID:name] [/M:F|B] [/T:s] [/X] [/Q] [/V]

| Switch | |
|---|---|
| `/H[:n]` | hot plugging: n drive slots (default 2) that get drive letters from ASPIDSK or ASPIDISK even when empty; the driver stays larger in memory |
| `/ID:name` | ASPI manager name (up to 16 characters, default `EUSBASPI`) |
| `/M:x` | register access: `F` flat real mode (not with EMM386 etc.), `B` BIOS INT 15h AH=87h; default: one that works |
| `/T:s` | command timeout in seconds (default 15) |
| `/X` | also take over xHCI (USB 3.x) controllers |
| `/Q`, `/V` | quiet, verbose |

## Building

Linux (or any system with the tools), GNU make:
- [NASM](https://nasm.us) for `EUSBASPI.SYS`, `EUSBVXD.386` and the tools,
- Open Watcom (`WATCOM=/opt/watcom`): the linker for `EUSBVXD.386`, the compiler for `USBSYNC.EXE`,
- for the package: the [GeosOne DOS Installer](https://github.com/geosone/gdi) 1.14 (`../gdi/build/INSTALL.EXE`, or `make package GDI=path`), the OpenASPI drivers (`make` in [OpenAspi](https://github.com/geosone/OpenAspi): `../OpenAspi/build/dos`, or `OPENASPI=dir`) and SHSUCDX/CDAUDIO (`install/mkpkg.sh` in [shsucd](https://github.com/geosone/shsucd): `../shsucd/build/pkg`, or `SHSUCD=dir`).

| Command | |
|---|---|
| `make` | `build/EUSBASPI.SYS`, `build/EUSBVXD.386`, `build/USBSYNC.COM`, `build/USBSYNCW.EXE` (Windows, installed as `USBSYNC.EXE`), `build/ASPITEST.COM` |
| `make package` | `build/pkg/`, `build/eusbaspi.zip` and the disk image `build/EUSBASPI.IMG` (mtools) |

## Tests

`test/install.sh` installs the package (`make package`) in QEMU with MS-DOS 7.1 (`BOOTIMG`, a boot floppy with XMGR, UDVD2 and MSCDEX): an IDE CD with MSCDEX before; after setup a USB CD and a USB stick on EHCI, both CDs with letters from SHSUCDX; `INSTALL /U` restores `CONFIG.SYS` and the MSCDEX line.

`test/run.sh` (QEMU, `USBHC=uhci|ohci|ehci|xhci`), `test/vbox.sh` (VirtualBox), `test/win311.sh` (Windows for Workgroups 3.11) and `test/mm.sh` (memory managers) need your own DOS boot disk image and, for some tests, `ASPIDISK.SYS`.

## License

GNU General Public License version 3, see [LICENSE](LICENSE).
