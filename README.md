# EUSBASPI - ASPI manager for USB mass storage under DOS

`EUSBASPI.SYS` is an ASPI manager (device `SCSIMGR$`) for DOS. It makes USB mass storage devices - sticks, card readers, hard disks, CD/DVD and floppy drives - usable for DOS ASPI drivers: the [OpenASPI](https://github.com/geosone/OpenAspi) drivers that come with it, or others such as Adaptec's `ASPIDISK.SYS` or `ASPICD.SYS`.

- USB controllers: UHCI, OHCI, EHCI (with companion controllers) and, with `/X`, xHCI (USB 3.x, up to 4 controllers)
- USB hubs (up to 5 levels)
- up to 16 USB devices (hubs included; a USB 3 hub counts twice) as ASPI targets 0-6 and 8-15 (7 is the adapter); xHCI keeps the device contexts in XMS, without an XMS driver it takes up to 8 devices. Adaptec's `ASPIDISK.SYS` only looks at targets 0-7, the OpenASPI drivers at all of them
- Bulk-Only and CBI devices
- hot plugging (`/H`): sticks can be changed while DOS runs
- USB disks the BIOS offers as INT 13h hard disks are taken over: their drive letters keep working
- Windows 3.x DOS boxes (with `EUSBVXD.386` for OHCI, EHCI and xHCI)
- USB mice and keyboards (`USBHID.SYS`): as the PS/2 mouse for the DOS mouse drivers and as the keyboard, on all four controller types
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
| USB mice and keyboards | `USBHID.SYS` |
| Controllers above 4 GB (off) | `PCI4G.SYS` before `EUSBASPI.SYS` |

With EUSBASPI already loaded, `ASPISCAN.COM` finds ZIP/LS-120 and tape drives and `HIDINFO.COM` USB mice and keyboards, and they preselect their options. OpenASPI and SHSUCD have installers of their own; all of them share `C:\OPENASPI`, so each can be installed alone or together.

**CD-ROM.** The installer takes the CD-ROM extension in this order: `SHSUCDX.COM` of the installation disk, else `SHSUCDX`, `MSCDEX.EXE`, `NWCDEX.EXE` on the computer. An existing `MSCDEX`/`NWCDEX` line keeps its drivers and drive letters, starts SHSUCDX instead (SHSUCDX ignores the MSCDEX options it doesn't have) and gets `/D:ASPICDA0`; `INSTALL /U` puts MSCDEX or NWCDEX back.

**From 0.3:** remove 0.3 first (`INSTALL /U` in `C:\OPENASPI\EUSBASPI`): it loaded Adaptec's `ASPIDISK.SYS` and `ASPICD.SYS`, which use the same drives as the OpenASPI drivers.

`INSTALL /U` in `C:\OPENASPI\EUSBASPI` removes the installation again; `C:\OPENASPI` stays while AHCIASPI or OpenASPI are still there.

Disable "USB legacy storage" (USB mass storage emulation) in the BIOS setup if possible: otherwise the BIOS keeps driving the controller. A USB disk the BIOS already offers as a hard disk (a stick plugged in at boot, DOS has it as `C:` or later) is taken over: EUSBASPI finds it by its size and sector 0 and answers INT 13h for it from then on, so its drive letter keeps working and ASPI disk drivers give it no second one. For that EUSBASPI must be loaded low (`DEVICE=`, not `DEVICEHIGH=`): the BIOS can't read into upper memory.

By hand, `CONFIG.SYS`:

    DEVICE=C:\OPENASPI\EUSBASPI.SYS
    DEVICE=C:\OPENASPI\ASPIDSK.SYS

and `SYSTEM.INI` of Windows 3.x, `[386Enh]`:

    device=C:\OPENASPI\EUSBVXD.386

## Unplugging: USBSYNC

DOS, SMARTDRV and Windows keep written data in caches, and many sticks have a write cache of their own. Before unplugging a drive run `USBSYNC` (DOS, DOS box) or `USBSYNC.EXE` (Windows 3.1x, e.g. as a Program Manager icon). It writes the DOS buffers (and the 32-bit file access cache of Windows for Workgroups 3.11), the SMARTDRV cache and the drives' write caches (`SYNCHRONIZE CACHE` through ASPI) and then tells you that the drives can be unplugged. `/E` also stops (ejects) them, `/Q` shows messages only on errors, `/A` (DOS) uses all ASPI adapters.

## USB mice and keyboards: USBHID

EUSBASPI takes the USB controllers over, and with them the USB mouse and keyboard a BIOS may have driven (SeaBIOS, "USB legacy" support). It keeps the HID devices (boot interface: mice, keyboards) configured for client drivers; `USBHID.SYS`, loaded after it, gives them to DOS as the PS/2 mouse of the BIOS (INT 15h AX=C200h-C207h):

    DEVICE=C:\OPENASPI\EUSBASPI.SYS
    DEVICE=C:\OPENASPI\USBHID.SYS [/V]

and in `AUTOEXEC.BAT` the mouse driver for a PS/2 mouse as usual (Microsoft `MOUSE`, CuteMouse, Logitech `MOUSE`). All USB mice work at once (their movements and buttons together), on UHCI, OHCI, EHCI and xHCI (`/X`). USBHID reads them 18 times a second (timer interrupt) and puts the packets into the keyboard controller (8042 command D3h), so they reach the mouse driver with IRQ 12 like those of a PS/2 mouse; drivers that read port 60h themselves (Logitech) take them too. Without a keyboard controller USBHID calls the IRQ 12 handler itself. A USB mouse plugged in later is not seen yet (no hot plugging for HID devices). `/V` lists the mice.

USB keyboards type through the keyboard controller as well (8042 command D2h): each key becomes the scan codes (set 1) a PS/2 keyboard sends, with IRQ 1, so the BIOS, `KEYB` (load it after `USBHID.SYS`, e.g. in `AUTOEXEC.BAT`) and programs with an INT 09h handler of their own get them as from a PS/2 keyboard. USBHID repeats a held key itself (after half a second, 12 times a second; USB keyboards don't repeat) and sets the keyboard's Num, Caps and Scroll Lock LEDs from the BIOS's shift state. Keyboards need a keyboard controller (8042); without one USBHID leaves them alone. Like the mice they are read 18 times a second.

`HIDINFO` lists the HID devices EUSBASPI keeps (`/R`: the reports of the first mouse for 8 seconds; `/Q /MOUSE`: errorlevel 0 if there is a USB mouse, `/Q /KBD` a USB keyboard, both together either).

## Controllers above 4 GB: PCI4G

With "Above 4G Decoding" on in the BIOS setup, the firmware may place a USB controller's registers (a 64-bit BAR) above 4 GB, out of reach of DOS; EUSBASPI then reports `BAR above 4 GB: load PCI4G.SYS`. `PCI4G.SYS`, loaded before `EUSBASPI.SYS` (and `AHCIASPI.SYS`), moves the BARs of USB (class 0C03h) and mass storage controllers (class 01h) into free space below 4 GB and stays 0 bytes resident:

    DEVICE=C:\OPENASPI\PCI4G.SYS [/V]

The free space is the gap between the end of RAM below 4 GB (INT 15h E820h) and FEC00000h, less the reserved E820h ranges and every BAR, expansion ROM and bridge window; BARs are sized with memory decoding off. A device on bus 0 gets the highest free address that fits, one behind a bridge an address in the bridge's 32-bit memory window (the bridges are not reprogrammed, so a full window means it stays). Graphics cards and other devices are left alone. `/V` shows the free range and the BARs left above 4 GB. Tested in QEMU (`test/pci4g.sh`: `BARHIGH.SYS` moves the xHCI's BAR above 4 GB first, on bus 0 and behind a `pci-bridge`, also under JEMMEX), not yet on real hardware.

Chipset fixes applied before the controllers are started (from Linux's `pci-quirks.c` and `ehci-pci.c`): the AMD-8111 EHCI is not used (erratum; its OHCI companions keep the ports), "async park" off on the ATI/AMD SB600 and SB700 (A12) EHCI, the VIA VT6212's EHCI sleep time; with `/X`, the switchable ports of Intel 7/8/9 series and Bay Trail xHCI controllers are routed from EHCI to xHCI (USB 3 speed), and that xHCI is started before the EHCI controllers. Not tried on these chipsets yet (QEMU has none of them).

## Switches

    DEVICE=EUSBASPI.SYS [/F] [/H[:n] [/RMB]] [/ID:name] [/M:F|B] [/T:s] [/X] [/Q] [/V]

| Switch | |
|---|---|
| `/H[:n]` | hot plugging: n drive slots (default 2) that get drive letters from ASPIFDD (OpenASPI) or Adaptec's ASPIDISK even when empty (listed at start as "empty USB slot"; with one device and `/H:1` there is no empty one); the driver stays larger in memory. The devices keep the removable bit (RMB) their firmware reports; EUSBASPI marks the adapter as hot plug (bit 7 of byte 2 of the HA inquiry's unique parameters), and the OpenASPI drivers then also ask fixed disks at each media check whether another device took their slot (UNIT ATTENTION, medium may have changed) |
| `/RMB` | with `/H`: every device is reported removable (as up to 0.13), for ASPI drivers that check only removable drives for media changes (Adaptec's ASPIDISK) |
| `/F` | flat numbering: every LUN is an ASPI target of its own, as in 0.5. By default each USB device is one ASPI target and its LUNs (card reader slots, CD+disk sticks) are ASPI LUNs, so old drivers that only scan targets 0-7 (e.g. Adaptec ASPIDISK) still see all disks; `/L` selects this default and is still accepted. Use `/F` for a driver that only looks at LUN 0 |
| `/ID:name` | ASPI manager name (up to 16 characters, default `EUSBASPI`) |
| `/M:x` | register access: `F` flat real mode (not with EMM386 etc.), `B` BIOS INT 15h AH=87h; default: one that works |
| `/T:s` | command timeout in seconds (default 15). Optional CD commands (READ SUB-CHANNEL, READ HEADER, GET CONFIGURATION, GET EVENT STATUS, PAUSE/RESUME, STOP PLAY, READ DISC INFORMATION, MECHANISM STATUS) get at most 3 s: some USB CD firmware (SMI 3267/3268, IS902E) never answers them. After such a timeout the device is reset and EUSBASPI answers that command itself with ILLEGAL REQUEST until the device is unplugged. A CD drive that went silent is then woken with up to three TEST UNIT READY (3 s each, a reset after each timeout); with `/H` a drive that still does not answer gets a USB port reset and is enumerated again, as if replugged. A drive that does not even take the bulk-only reset gets the port reset at once, without clearing its endpoint halts first (as Linux does; under VMware each of those took 5 s). READ SUB-CHANNEL does not reach a CD drive until it got a PLAY AUDIO command: before that EUSBASPI answers it itself ("no current audio status"), since CD drivers poll it (SYMCD) and the SK6221 hangs on it for good. A MODE SELECT that only sets 2048 byte blocks, with or without the read error recovery page (ASPICD, ARTCD), is answered by EUSBASPI as well; SMI 3255/3257 never answer it. CD/DVD drives always get MODE SENSE/SELECT(10), never the 6 byte forms, and after every failed command EUSBASPI fetches the sense data from the device, as Linux does. READ TOC in LBA: a drive that answers in MSF anyway (IS902E: track 1 at 00:02:00, so INICD looked for the volume at sector 528) is recognised by track 1 and its answers are converted to LBA |
| `/X` | also take over xHCI (USB 3.x) controllers |
| `/Q`, `/V` | quiet, verbose |

## Building

Linux (or any system with the tools), GNU make:
- [NASM](https://nasm.us) for `EUSBASPI.SYS`, `EUSBVXD.386` and the tools,
- Open Watcom (`WATCOM=/opt/watcom`): the linker for `EUSBVXD.386`, the compiler for `USBSYNC.EXE`,
- for the package: the [GeosOne DOS Installer](https://github.com/geosone/gdi) 1.16.5 (`../gdi/build/INSTALL.EXE`, or `make package GDI=path`), the OpenASPI drivers (`make` in [OpenAspi](https://github.com/geosone/OpenAspi): `../OpenAspi/build/dos`, or `OPENASPI=dir`) and SHSUCDX/CDAUDIO (`install/mkpkg.sh` in [shsucd](https://github.com/geosone/shsucd): `../shsucd/build/pkg`, or `SHSUCD=dir`).

| Command | |
|---|---|
| `make` | `build/EUSBASPI.SYS`, `build/PCI4G.SYS`, `build/USBHID.SYS`, `build/HIDINFO.COM`, `build/MTEST.COM` (INT 33h test), `build/EUSBVXD.386`, `build/USBSYNC.COM`, `build/USBSYNCW.EXE` (Windows, installed as `USBSYNC.EXE`), `build/ASPITEST.COM` |
| `make package` | `build/pkg/`, `build/eusbaspi.zip` and the disk image `build/EUSBASPI.IMG` (mtools) |

## Native C mass storage

`native/msc.c` is USB mass storage in C for [OpenASPI](https://github.com/geosone/OpenASPI)'s native layer, for 32- and 64-bit systems that drive the controllers themselves, such as the GDOS kernel of [GonWare](https://gitea.disconnected-by-peer.at/GonSoft/GonWare). It runs on [eusbcore](https://github.com/geosone/eusbcore), the USB stack (xHCI, EHCI, OHCI, UHCI, hubs) that came out of this C port of `EUSBASPI.SYS` and is included as the submodule `third_party/eusbcore`.

Every mass storage device becomes part of one ASPI adapter `USB`, as one target with its LUNs (with `/F` every LUN one target): Bulk-Only (CBW/CSW, reset recovery, stalled data phases and CSWs) and CBI/CB (ADSC, interrupt status, UFI). Requests run through OpenASPI's shared packet device handling, like ATAPI on AHCI and IDE: REQUEST SENSE as autosense and, for the ATAPI, UFI and SFF-8070i command sets, 12-byte packets and the 6-to-10-byte rules.

The embedding system provides the host services of `openaspi/native/host.h`, adds its controllers with `usb_add()` (EHCI before its companions), calls `usb_enumerate()` and then `eusbaspi_register_storage()`. CMake builds it as `EUSBASPI::native` on top of `EUSBCORE::core` and `OpenASPI::native`; a parent project that already has `EUSBCORE::core` (because other class drivers use it too) keeps its own:

```sh
git submodule update --init --depth 1
cmake -S . -B build/native -G Ninja && ninja -C build/native
```

Tested in QEMU on x86-64, i686 and AArch64: on xHCI, EHCI, OHCI, UHCI and an EHCI's UHCI companion, reading and writing.

## Tests

`test/install.sh` installs the package (`make package`) in QEMU with MS-DOS 7.1 (`BOOTIMG`, a boot floppy with XMGR, UDVD2 and MSCDEX): an IDE CD with MSCDEX before; after setup a USB CD and a USB stick on EHCI, both CDs with letters from SHSUCDX; `INSTALL /U` restores `CONFIG.SYS` and the MSCDEX line.

`test/run.sh` (QEMU, `USBHC=uhci|ohci|ehci|xhci`), `test/pci4g.sh` (PCI4G), `test/vbox.sh` (VirtualBox), `test/win311.sh` (Windows for Workgroups 3.11) and `test/mm.sh` (memory managers) need your own DOS boot disk image and, for some tests, `ASPIDISK.SYS`.

## License

GNU General Public License version 2 or (at your option) any later
version, see [LICENSE](LICENSE). The installer of the package
([GDI](https://github.com/geosone/gdi)) is under version 3, its text
`GPL3.TXT`.
