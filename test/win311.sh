#!/bin/bash
# EUSBASPI under Windows for Workgroups 3.11 in enhanced mode: boots the
# image made by test/mkwin311.sh with EUSBASPI.SYS + ASPIDISK.SYS (the USB
# stick is plugged in after the BIOS, while DELAY.SYS waits), reads the
# stick in real mode, then starts WIN /3 with a DOS box (C:\T.BAT) that runs
# ASPITEST and reads the stick again.  There VDS is Windows' own (VDMAD).
#
#   test/win311.sh [EUSBASPI options]
#
# Environment:
#   USBHC       QEMU USB host controller (default pci-ohci)
#   MM=none     HIMEM.SYS only (default)
#   MM=emm386   HIMEM.SYS + EMM386.EXE NOEMS
#   MM=386max   386MAX.SYS (and 386MAX.VXD, MAXVXD=path) instead of HIMEM
#   HIGH=1      load the drivers with DEVICEHIGH (EMMs only)
#   WINBAT      commands for the DOS box (| separated, output > AUX)
#   VXD=0       without EUSBVXD.386 (OHCI/EHCI/xHCI do not work then)
#   TIMEOUT     seconds until the screen is captured (default 180)
#
# Output (build/win311test/): serial.log (DOS and DOS box output),
#   debug.log (driver + ASPI trace), screen.png
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/build/win311test
BASE=$ROOT/build/win311/win311.img
DOS=${DOS_DIR:-$HOME/Iso/Dos}
BOOTIMG=${BOOTIMG:-$DOS/dos71de/Dos7.1_System_deutsch.IMG}
MSCAB=${MSCAB:-"$HOME/Iso/Windows 98/Windows98/WIN95/WIN95_02.CAB"}
MAX386=${MAX386:-$HOME/mars/386MAX/build/bin/386MAX.SYS}
MM=${MM:-none}
OPTS="$*"
[ -f "$BASE" ] || { echo "run test/mkwin311.sh first"; exit 1; }

rm -rf "$OUT"; mkdir -p "$OUT/c"
nasm -f bin -w-reloc-abs-word -DE9LOG -I"$ROOT/src/" -o "$OUT/c/EUSBASPI.SYS" "$ROOT/src/eusbaspi.asm" || exit 1
nasm -f bin -w-reloc-abs-word -I"$ROOT/tools/" -I"$ROOT/src/" -o "$OUT/c/ASPITEST.COM" "$ROOT/tools/aspitest.asm" || exit 1
nasm -f bin -o "$OUT/c/QEXIT.COM" "$ROOT/tools/qexit.asm"
nasm -f bin -o "$OUT/c/WINMODE.COM" "$ROOT/tools/winmode.asm"
nasm -f bin -o "$OUT/c/DELAY.SYS" "$ROOT/tools/delay.asm"
mcopy -i "$BOOTIMG" ::ASPIDISK.SYS "$OUT/c/"
make -s -C "$ROOT" build/EUSBVXD.386 || exit 1
[ "${VXD:-1}" = 1 ] && cp "$ROOT/build/EUSBVXD.386" "$OUT/c/"
cp "$BASE" "$OUT/test.img"

crlf() { tr '|' '\n' | sed 's/$/\r/'; }
DH=DEVICE
if [ "${HIGH:-0}" = 1 ] && [ "$MM" != none ]; then DH=DEVICEHIGH; fi
EXITDEV="-device isa-debug-exit,iobase=0xf4,iosize=0x04"
{
    case "$MM" in
    none)   echo 'DEVICE=C:\HIMEM.SYS' ;;
    emm386) (cd "$OUT" && cabextract -q -F emm386.exe "$MSCAB" 2>/dev/null)
            cp "$OUT/emm386.exe" "$OUT/c/EMM386.EXE"
            echo 'DEVICE=C:\HIMEM.SYS'
            echo 'DEVICE=C:\EMM386.EXE NOEMS'
            echo 'DOS=HIGH,UMB' ;;
    386max) cp "$MAX386" "$OUT/c/"
            cp "${MAXVXD:-$(dirname "$MAX386")/386MAX.VXD}" "$OUT/c/386MAX.VXD"
            echo 'DEVICE=C:\386MAX.SYS'
            echo 'DOS=HIGH,UMB'
            EXITDEV= ;;                 # 386MAX touches port F4h
    *) echo "unknown MM=$MM" >&2; exit 1 ;;
    esac
    echo 'DEVICE=C:\DELAY.SYS|DEVICE=C:\DELAY.SYS'
    echo "$DH=C:\\EUSBASPI.SYS $OPTS"
    echo "$DH=C:\\ASPIDISK.SYS"
    echo 'FILES=40|BUFFERS=20|LASTDRIVE=Z|SHELL=C:\COMMAND.COM C:\ /P'
} | crlf > "$OUT/c/CONFIG.SYS"
{
    echo '@ECHO OFF'
    echo 'PATH C:\;C:\WINDOWS'
    echo 'ECHO --- real mode > AUX'
    echo 'TYPE D:\USBTEST.TXT > AUX'
    echo 'ECHO --- starting Windows > AUX'
    echo 'C:\WINDOWS\WIN /3 C:\T.BAT'
    echo 'ECHO --- back from Windows > AUX'
    echo 'QEXIT'
} | crlf > "$OUT/c/AUTOEXEC.BAT"
{
    echo '@ECHO OFF'
    echo 'ECHO --- DOS box > AUX'
    echo 'WINMODE > AUX'
    echo "${WINBAT:-ASPITEST /E9|DIR D:\\|TYPE D:\\USBTEST.TXT}" | tr '|' '\n' | sed 's/$/ > AUX/'
    echo 'ECHO --- DOS box done > AUX'
    echo 'QEXIT'
} | crlf > "$OUT/c/T.BAT"
P="$OUT/test.img@@32256"
mcopy -o -i "$P" "$OUT"/c/* ::/
if [ "${VXD:-1}" = 1 ]; then              # SYSTEM.INI: load EUSBVXD.386
    mtype -i "$P" ::/WINDOWS/SYSTEM.INI | tr -d '\r' |
        sed 's/^\[386Enh\]$/[386Enh]\ndevice=C:\\EUSBVXD.386/' | sed 's/$/\r/' > "$OUT/SYSTEM.INI"
    mcopy -o -i "$P" "$OUT/SYSTEM.INI" ::/WINDOWS/SYSTEM.INI
fi

# USB stick: 64 MB, one FAT16 partition with USBTEST.TXT
truncate -s 64M "$OUT/usb.img"
echo 'start=63, type=6' | sfdisk -q "$OUT/usb.img"
mkfs.fat -F16 -n USBSTICK --offset 63 "$OUT/usb.img" $(( (64*2048 - 63) / 2 )) >/dev/null
printf 'EUSBASPI Win3.11 test\r\n' > "$OUT/USBTEST.TXT"
mcopy -i "$OUT/usb.img@@32256" "$OUT/USBTEST.TXT" ::/

{ sleep 2
  echo "drive_add 0 if=none,id=usbdisk,format=raw,file=$OUT/usb.img"
  echo "device_add usb-storage,id=stick1,bus=usbhc.0,drive=usbdisk"
  sleep "${TIMEOUT:-180}"; echo "screendump $OUT/screen.ppm"; sleep 1; echo quit; } |
qemu-system-i386 -machine pc ${ACCEL:--accel kvm} -m 32 -display none -vga std -monitor stdio \
    -drive if=ide,index=0,format=raw,file="$OUT/test.img" -boot c -nic none \
    -device "${USBHC:-pci-ohci}",id=usbhc \
    -debugcon file:"$OUT/debug.log" -serial file:"$OUT/serial.log" \
    $EXITDEV ${QEMU_EXTRA:-} > "$OUT/monitor.log" 2>&1
[ -f "$OUT/screen.ppm" ] && command -v magick >/dev/null && magick "$OUT/screen.ppm" "$OUT/screen.png"

echo "=== CONFIG.SYS"; tr -d '\r' < "$OUT/c/CONFIG.SYS"
echo "=== debug.log (driver + ASPI trace)"; tr -d '\r' < "$OUT/debug.log" | head -${DEBUGLINES:-60}
echo "=== serial.log"; tr -d '\r' < "$OUT/serial.log"
tr -d '\r' < "$OUT/serial.log" | sed -n '/--- DOS box/,$p' | grep -q 'EUSBASPI Win3.11 test'
