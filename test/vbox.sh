#!/bin/bash
# Boot MS-DOS 7.1 in VirtualBox (headless) with an emulated USB stick on the
# chosen USB controller, load EUSBASPI.SYS and run ASPITEST and ASPIDISK.
# Second USB implementation besides QEMU.  VirtualBox emulates OHCI, EHCI
# (Extension Pack) and xHCI, but no UHCI.
#
#   test/vbox.sh [EUSBASPI options]         e.g. test/vbox.sh /V
#
# Environment:
#   USBHC    ohci | ehci | xhci (default ohci; ehci also enables the OHCI
#            companion like a real PC)
#   DEVICES, BATCH  as in test/run.sh
#   TIMEOUT  seconds to wait for 'END OF TEST' (default 120)
#   KEEP=1   keep the VM (default: unregister and delete it)
#
# Output (build/vbox/): serial.log, screen.png
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/build/vbox
DOS=${DOS_DIR:-$HOME/Iso/Dos}
BOOTIMG=${BOOTIMG:-$DOS/dos71de/Dos7.1_System_deutsch.IMG}
OPTS="$*"
VM=eusbaspi-test
HC=${USBHC:-ohci}

VBoxManage unregistervm "$VM" --delete >/dev/null 2>&1
rm -rf "$OUT"; mkdir -p "$OUT"
nasm -f bin -w-reloc-abs-word -I"$ROOT/src/" -o "$OUT/EUSBASPI.SYS" "$ROOT/src/eusbaspi.asm" || exit 1
nasm -f bin -w-reloc-abs-word -I"$ROOT/tools/" -I"$ROOT/src/" -o "$OUT/ASPITEST.COM" "$ROOT/tools/aspitest.asm" || exit 1
nasm -f bin -o "$OUT/CDSDUMP.COM" "$ROOT/tools/cdsdump.asm"
cp "$BOOTIMG" "$OUT/boot.img"; chmod u+w "$OUT/boot.img"

crlf() { tr '|' '\n' | sed 's/$/\r/'; }
{
    echo "${DEVICES:-A:\\EUSBASPI.SYS $OPTS|A:\\ASPIDISK.SYS}" | tr '|' '\n' | sed 's/^/DEVICE=/'
    echo 'FILES=40'
    echo 'LASTDRIVE=Z'
    echo 'SHELL=A:\COMMAND.COM A:\ /P'
} | crlf > "$OUT/CONFIG.SYS"
{
    echo '@ECHO OFF'
    echo 'PATH A:\'
    echo "${BATCH:-CDSDUMP|ASPITEST|DIR C:|TYPE C:\\USBTEST.TXT}" | tr '|' '\n' | sed 's/$/ > AUX/'
    echo 'ECHO END OF TEST > AUX'
} | crlf > "$OUT/AUTOEXEC.BAT"
mcopy -o -i "$OUT/boot.img" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" "$OUT/EUSBASPI.SYS" \
      "$OUT/ASPITEST.COM" "$OUT/CDSDUMP.COM" ::/

# USB stick: 64 MB, one FAT16 partition with USBTEST.TXT
truncate -s 64M "$OUT/usb.img"
echo 'start=63, type=6' | sfdisk -q "$OUT/usb.img"
mkfs.fat -F16 -n USBSTICK --offset 63 "$OUT/usb.img" $(( (64*2048 - 63) / 2 )) >/dev/null
printf 'USB stick read OK\r\n' > "$OUT/USBTEST.TXT"
mcopy -i "$OUT/usb.img@@32256" "$OUT/USBTEST.TXT" ::/
VBoxManage convertfromraw "$OUT/usb.img" "$OUT/usb.vdi" --format VDI >/dev/null || exit 1

USBOPT=(--usb-ohci=on)
case $HC in
    ehci) USBOPT=(--usb-ohci=on --usb-ehci=on) ;;
    xhci) USBOPT=(--usb-xhci=on) ;;
esac
VBoxManage createvm --name "$VM" --basefolder "$OUT" --ostype DOS --register >/dev/null || exit 1
VBoxManage modifyvm "$VM" --memory 32 --vram 16 --boot1 floppy --boot2 none --boot3 none --boot4 none \
    --audio-driver none --nic1 none --mouse ps2 --keyboard ps2 "${USBOPT[@]}" \
    --uart1 0x3F8 4 --uart-mode1 file "$OUT/serial.log"
VBoxManage storagectl "$VM" --name FD --add floppy
VBoxManage storageattach "$VM" --storagectl FD --port 0 --device 0 --type fdd --medium "$OUT/boot.img"
VBoxManage storagectl "$VM" --name USB --add usb
VBoxManage storageattach "$VM" --storagectl USB --port 0 --device 0 --type hdd --medium "$OUT/usb.vdi"

VBoxManage startvm "$VM" --type headless >/dev/null || exit 1
for ((i = 0; i < ${TIMEOUT:-120}; i++)); do
    sleep 1
    tr -d '\r' < "$OUT/serial.log" 2>/dev/null | grep -q 'END OF TEST' && break
done
sleep 1
VBoxManage controlvm "$VM" screenshotpng "$OUT/screen.png" >/dev/null 2>&1
VBoxManage controlvm "$VM" poweroff >/dev/null 2>&1
sleep 2
[ "${KEEP:-0}" = 1 ] || VBoxManage unregistervm "$VM" --delete >/dev/null 2>&1

echo "=== serial.log"; tr -d '\r' < "$OUT/serial.log"
tr -d '\r' < "$OUT/serial.log" | grep -q 'END OF TEST'
