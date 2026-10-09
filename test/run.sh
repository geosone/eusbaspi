#!/bin/bash
# Boot MS-DOS 7.1 in QEMU with a USB host controller and a USB stick, load
# EUSBASPI.SYS and run ASPITEST and ASPIDISK on it.
#
#   test/run.sh [EUSBASPI options]          e.g. test/run.sh /V
#
# Environment:
#   USBHC    QEMU USB host controller (default pci-ohci; piix3-usb-uhci,
#            usb-ehci, qemu-xhci, ...)
#   DEVICES  CONFIG.SYS driver lines (| separated), default
#            'A:\EUSBASPI.SYS <options>|A:\ASPIDISK.SYS'
#   BATCH    AUTOEXEC.BAT commands (| separated, output > AUX)
#   USBBOOT=1   attach the stick at start, so the BIOS (SeaBIOS) maps it as
#               an INT 13h disk and keeps driving the controller; by default
#               it is plugged in after the BIOS (DELAY.SYS waits for it)
#   HOTPLUG_EXTRA  more monitor commands (| separated) run when the stick
#               is plugged in, e.g. 'device_add usb-serial,chardev=cn,bus=usbhc.0'
#   EMM=1    load JEMMEX (V86 mode, VDS, UMBs) first; HIGH=1 also loads
#            EUSBASPI.SYS with DEVICEHIGH
#   MM, MMOPTS  other memory managers, see test/mm.sh
#   HOTPLUG_PRE  monitor commands (| separated) run before the stick is
#               plugged in, e.g. 'device_add usb-hub,bus=usbhc.0,port=1'
#   STICKPORT   USB port of the stick, e.g. 1.2 (behind that hub)
#   NOSTICK=1   no stick at boot (for SWAP_CMDS that plug one in)
#   SWAP_ON, SWAP_CMDS  hot plug test: once SWAP_ON appears in serial.log,
#               run the monitor commands SWAP_CMDS (| separated, "sleep n"
#               waits), e.g. 'device_del stick1|sleep 2|drive_add 0
#               if=none,id=u2,format=raw,file=build/test/usb2.img|device_add
#               usb-storage,id=stick2,bus=usbhc.0,drive=u2'; the stick is
#               "stick1", build/test/usb2.img holds "second stick read OK"
#   SWAP_STEPS  several of them: 'MARK1=cmd|cmd;MARK2=cmd|...'
#   QEMU_EXTRA  more QEMU arguments
#   TIMEOUT  seconds until the screen is captured (default 60)
#
# Output (build/test/): debug.log (driver + ASPI trace), serial.log,
# screen.png
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/build/test
DOS=${DOS_DIR:-$HOME/Iso/Dos}
BOOTIMG=${BOOTIMG:-$DOS/dos71de/Dos7.1_System_deutsch.IMG}
JEMM_ZIP=${JEMM_ZIP:-$DOS/JemmB_v586.zip}
OPTS="$*"

rm -rf "$OUT"; mkdir -p "$OUT"
nasm -f bin -w-reloc-abs-word -DE9LOG -I"$ROOT/src/" -o "$OUT/EUSBASPI.SYS" "$ROOT/src/eusbaspi.asm" || exit 1
nasm -f bin -w-reloc-abs-word -I"$ROOT/tools/" -I"$ROOT/src/" -o "$OUT/ASPITEST.COM" "$ROOT/tools/aspitest.asm" || exit 1
nasm -f bin -o "$OUT/CDSDUMP.COM" "$ROOT/tools/cdsdump.asm"
nasm -f bin -o "$OUT/QEXIT.COM" "$ROOT/tools/qexit.asm"
nasm -f bin -o "$OUT/DELAY.SYS" "$ROOT/tools/delay.asm"
nasm -f bin -o "$OUT/SLEEP.COM" "$ROOT/tools/sleep.asm"
cp "$BOOTIMG" "$OUT/boot.img"; chmod u+w "$OUT/boot.img"

crlf() { tr '|' '\n' | sed 's/$/\r/'; }
. "$ROOT/test/mm.sh"
# EMM=1 (and HIGH=1 alone) mean JemmEx, as before MM= existed
if [ -z "${MM:-}" ] && { [ "${EMM:-0}" = 1 ] || [ "${HIGH:-0}" = 1 ]; }; then MM=jemmex; fi
{
    mm_setup "$OUT" "$OUT/boot.img" || exit 1
    [ "${USBBOOT:-0}" = 1 ] || printf 'DEVICE=A:\\DELAY.SYS\nDEVICE=A:\\DELAY.SYS\n'
    echo "${DEVICES:-A:\\EUSBASPI.SYS $OPTS|A:\\ASPIDISK.SYS}" | tr '|' '\n' | sed 's/^/DEVICE=/' | mm_high
    echo 'FILES=40'
    echo 'LASTDRIVE=Z'
    echo 'SHELL=A:\COMMAND.COM A:\ /P'
} | crlf > "$OUT/CONFIG.SYS"
{
    echo '@ECHO OFF'
    echo 'PATH A:\'
    echo "${BATCH:-CDSDUMP|ASPITEST|DIR C:|TYPE C:\\USBTEST.TXT}" | tr '|' '\n' | sed 's/$/ > AUX/'
    echo 'ECHO END OF TEST > AUX'
    echo 'QEXIT'
} | crlf > "$OUT/AUTOEXEC.BAT"
mcopy -o -i "$OUT/boot.img" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" "$OUT/EUSBASPI.SYS" \
      "$OUT/ASPITEST.COM" "$OUT/CDSDUMP.COM" "$OUT/QEXIT.COM" "$OUT/DELAY.SYS" "$OUT/SLEEP.COM" ::/

# USB stick: 64 MB, one FAT16 partition with USBTEST.TXT
truncate -s 64M "$OUT/usb.img"
echo 'start=63, type=6' | sfdisk -q "$OUT/usb.img"
mkfs.fat -F16 -n USBSTICK --offset 63 "$OUT/usb.img" $(( (64*2048 - 63) / 2 )) >/dev/null
printf 'USB stick read OK\r\n' > "$OUT/USBTEST.TXT"
mcopy -i "$OUT/usb.img@@32256" "$OUT/USBTEST.TXT" ::/

USBDEV=(-drive id=usbdisk,if=none,format=raw,file="$OUT/usb.img"
        -device usb-storage,bus=usbhc.0,drive=usbdisk)
HOTPLUG=
if [ "${USBBOOT:-0}" != 1 ]; then
    USBDEV=()
    HOTPLUG="drive_add 0 if=none,id=usbdisk,format=raw,file=$OUT/usb.img
device_add usb-storage,id=stick1,bus=usbhc.0,drive=usbdisk${STICKPORT:+,port=$STICKPORT}"
    [ -n "${HOTPLUG_PRE:-}" ] && HOTPLUG="$(echo "$HOTPLUG_PRE" | tr '|' '\n')
$HOTPLUG"
fi
[ "${NOSTICK:-0}" = 1 ] && HOTPLUG=   # hot plug test: no stick at boot
[ -n "${HOTPLUG_EXTRA:-}" ] && HOTPLUG="$HOTPLUG
$(echo "$HOTPLUG_EXTRA" | tr '|' '\n')"
# 386MAX touches port F4h, so QEXIT can't be used there (the timeout ends it)
EXITDEV="-device isa-debug-exit,iobase=0xf4,iosize=0x04"
[ "${MM:-}" = 386max ] && EXITDEV=
# second stick for SWAP_CMDS (usb2.img, USBTEST.TXT says "second stick")
truncate -s 32M "$OUT/usb2.img"
echo 'start=63, type=6' | sfdisk -q "$OUT/usb2.img"
mkfs.fat -F16 -n STICK2 --offset 63 "$OUT/usb2.img" $(( (32*2048 - 63) / 2 )) >/dev/null
printf 'second stick read OK\r\n' > "$OUT/USBTEST2.TXT"
mcopy -i "$OUT/usb2.img@@32256" "$OUT/USBTEST2.TXT" ::/USBTEST.TXT
{ if [ -n "$HOTPLUG" ]; then sleep 2; echo "$HOTPLUG"; fi
  steps="${SWAP_STEPS:-}"
  [ -n "${SWAP_ON:-}" ] && steps="$SWAP_ON=${SWAP_CMDS:-}"
  echo "$steps" | tr ';' '\n' | while IFS= read -r step; do
      [ -n "$step" ] || continue
      mark=${step%%=*}                  # hot plug test: when DOS reaches it
      for i in $(seq 1 $(( ${TIMEOUT:-60} * 2 ))); do
          tr -d '\r' < "$OUT/serial.log" 2>/dev/null | grep -q "$mark" && break
          sleep 0.5
      done
      echo "${step#*=}" | tr '|' '\n' | while read -r c; do
          case "$c" in sleep\ *) sleep "${c#sleep }" ;; *) echo "$c" ;; esac
      done
  done
  sleep "${TIMEOUT:-60}"; echo "screendump $OUT/screen.ppm"; sleep 1; echo quit; } |
qemu-system-i386 -machine pc -m 32 -display none -vga std -monitor stdio \
    -drive if=floppy,format=raw,file="$OUT/boot.img" -boot a \
    -device "${USBHC:-pci-ohci}",id=usbhc \
    "${USBDEV[@]}" \
    ${QEMU_EXTRA:-} \
    -debugcon file:"$OUT/debug.log" -serial file:"$OUT/serial.log" \
    $EXITDEV > "$OUT/monitor.log" 2>&1
[ -f "$OUT/screen.ppm" ] && command -v magick >/dev/null && magick "$OUT/screen.ppm" "$OUT/screen.png"

echo "=== CONFIG.SYS"; tr -d '\r' < "$OUT/CONFIG.SYS"
echo "=== debug.log"; tr -d '\r' < "$OUT/debug.log"
echo "=== serial.log"; tr -d '\r' < "$OUT/serial.log"
tr -d '\r' < "$OUT/serial.log" | grep -q 'END OF TEST'
