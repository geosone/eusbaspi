EUSBASPI 0.4 - ASPI manager for USB mass storage
================================================

EUSBASPI.SYS is an ASPI manager (device SCSIMGR$) for DOS.  It makes USB
mass storage devices - sticks, card readers, hard disks, CD/DVD and
floppy drives - usable for DOS ASPI drivers: the OpenASPI drivers that
come with it, or others such as Adaptec's ASPIDISK.SYS and ASPICD.SYS.

Supported:
  - USB controllers: UHCI, OHCI, EHCI (with companion controllers) and,
    with /X, xHCI (USB 3.x, up to 4 controllers),
  - USB hubs (up to 5 levels),
  - Bulk-Only and CBI devices (sticks, disks, card readers, floppy, CD),
  - hot plugging (/H): sticks can be changed while DOS runs,
  - Windows 3.x DOS boxes (with EUSBVXD.386 for OHCI, EHCI and xHCI),
  - other ASPI managers (AHCIASPI.SYS, ASPI8DOS.SYS, ...): EUSBASPI chains
    to a manager loaded before it.

License: GNU General Public License version 3 (see LICENSE.TXT).
Source:  https://github.com/geosone/eusbaspi


Installation
------------

Run INSTALL.EXE from the distribution disk or directory.  It copies the
files to C:\OPENASPI (shared with AHCIASPI and OpenASPI), adds
EUSBASPI.SYS to CONFIG.SYS and, if Windows 3.x is found, EUSBVXD.386 to
the [386Enh] section of its SYSTEM.INI.  INSTALL /U in
C:\OPENASPI\EUSBASPI removes it again.

The OpenASPI drivers (https://github.com/geosone/OpenAspi) come with it,
each one an option; INSTALL loads the ones chosen after EUSBASPI.SYS:

  ASPIDSK.SYS  sticks, card readers, disks (on by default)
  ASPICDA.SYS  CD/DVD drives (on by default), with SHSUCDX for the drive
               letter and digital CD audio through VSBHDA (CDAUDIO.COM
               switches it)
  ASPIFDD.SYS  ZIP, LS-120, LS-240, Clik!, Jaz
  ASPITAP.SYS  tape drives (TAPECTL.COM)

With EUSBASPI already loaded, ASPISCAN.COM finds ZIP/LS-120 and tape
drives, and their options are preselected.  OpenASPI and SHSUCD have
installers of their own as well; they all share C:\OPENASPI.

CD-ROM: INSTALL puts SHSUCDX in the place of MSCDEX (MS-DOS) or NWCDEX
(DR-DOS) in AUTOEXEC.BAT, with their drivers and drive letters, and adds
" /D:ASPICDA0" to that line (or a new line
"C:\OPENASPI\SHSUCDX.COM /D:ASPICDA0 /A:AUTO").  INSTALL /U puts MSCDEX
or NWCDEX back.

From 0.3: remove it first (INSTALL /U in C:\OPENASPI\EUSBASPI); 0.3 had
loaded Adaptec's ASPIDISK and ASPICD, which use the same drives.

By hand, CONFIG.SYS:

    DEVICE=C:\OPENASPI\EUSBASPI.SYS
    DEVICE=C:\OPENASPI\ASPIDSK.SYS

SYSTEM.INI of Windows 3.x, section [386Enh]:

    device=C:\OPENASPI\EUSBVXD.386

Disable "USB legacy storage" (USB mass storage emulation) in the BIOS
setup if possible: otherwise the BIOS keeps driving the controller.


Unplugging USB drives: USBSYNC
------------------------------

DOS, SMARTDRV and Windows keep written data in caches for a while, and
many USB sticks have a write cache of their own.  Before you unplug a
drive, run

    USBSYNC [/E] [/A] [/Q]          in DOS or a DOS box
    USBSYNC.EXE [/E] [/Q]           in Windows 3.1x (add it to the Program
                                    Manager: File / New / Program Item)

It writes the DOS buffers (and the 32-bit file access cache of Windows
for Workgroups 3.11), the SMARTDRV cache and the write caches of the USB
drives (SYNCHRONIZE CACHE), then says that the drives can be unplugged.

  /E  also stop (eject) the drives; unplug them afterwards
  /A  (DOS) all ASPI host adapters, not only those of EUSBASPI
  /Q  quiet: messages only on errors


Switches
--------

    DEVICE=EUSBASPI.SYS [/H[:n]] [/ID:name] [/M:F|B] [/T:s] [/X] [/Q] [/V]

  /H[:n]   Hot plugging: n drive slots (default 2, ASPI targets 0..n-1)
           that get drive letters from ASPIDISK even when they are empty;
           sticks can be plugged in and out while DOS runs.  The driver
           stays larger in memory.
  /ID:name ASPI manager name reported to the clients (up to 16
           characters, default EUSBASPI).
  /M:x     Access to the controller registers: F = flat real mode (not
           with EMM386 and other memory managers), B = BIOS INT 15h
           AH=87h.  Without /M EUSBASPI chooses one that works.
  /T:s     Command timeout in seconds (default 15).
  /X       Also take over xHCI (USB 3.x) controllers.
  /Q       Quiet: no messages at boot time.
  /V       Verbose: show the controllers, ports and devices.


Files
-----

  EUSBASPI.SYS   the driver
  EUSBVXD.386    Windows 3.x enhanced mode helper (memory mapped registers)
  USBSYNC.COM    write all cached data before unplugging (DOS)
  USBSYNC.EXE    the same for Windows 3.1x
  ASPITEST.COM   test program: lists the ASPI host adapters and devices
  ASPISCAN.COM   which devices the OpenASPI drivers would take (OpenASPI)
  README.TXT     this file
  LICENSE.TXT    GNU General Public License version 3
  INSTALL.EXE    GeosOne DOS Installer (https://github.com/geosone/gdi)
  INSTALL.INI    its configuration

Optional (installed when selected):

  ASPIDSK.SYS    OpenASPI: sticks, card readers, disks (GPL 3)
  ASPICDA.SYS    OpenASPI: CD/DVD
  SHSUCDX.COM    CD-ROM redirector with digital CD audio (SHSUCD of Jason
  CDAUDIO.COM    Hood, changed; https://github.com/geosone/shsucd)
  SHSUCD.TXT     its readme, SHSUCD.LIC its license
  ASPIFDD.SYS    OpenASPI: ZIP, LS-120 and other removable drives
  ASPITAP.SYS    OpenASPI: tape drives
  TAPECTL.COM    OpenASPI: tape control (rewind, eject, ...)


Notes
-----

  - EUSBASPI polls the controllers and uses no interrupts.
  - This is a first version, tested in QEMU and VirtualBox.  Please report
    problems with the output of "DEVICE=EUSBASPI.SYS /V" and "ASPITEST" at
    the GitHub project.
