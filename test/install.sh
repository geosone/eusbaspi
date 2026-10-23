#!/bin/bash
# Test the setup package (make package -> build/pkg) in QEMU: DOS 7.1 from a
# floppy (BOOTIMG, with XMGR.SYS, UDVD2.SYS and MSCDEX.EXE), the package on
# the IDE hard disk C:, one CD on the IDE channel (UDVD2, MSCDEX); on EHCI a
# USB CD (EUSBASPI, ASPICDA) and a USB stick (ASPIDSK):
#   boot 1: INSTALL /S from C:\PKG to C:\OPENASPI: EUSBASPI, ASPIDSK and
#           ASPICDA in CONFIG.SYS, SHSUCDX in the place of MSCDEX with
#           /D:ASPICDA0
#   boot 2: SHSUCDX gives both CDs a letter (R: IDE, S: USB), the stick
#           has a DOS drive (E:, the letter ASPIDSK reports)
#   boot 3: INSTALL /U: CONFIG.SYS and the MSCDEX line as before
#
#   test/install.sh
#
# Output (build/test-install/): serial1..3.log, screen1..3.png
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/build/test-install
BOOTIMG=${BOOTIMG:-$HOME/Iso/Dos/dos71de/Dos7.1_System_deutsch.IMG}
[ -f "$ROOT/build/pkg/INSTALL.EXE" ] || { echo "no build/pkg (make package)"; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/ide" "$OUT/usb"
crlf() { tr '|' '\n' | sed 's/$/\r/'; }

cp "$BOOTIMG" "$OUT/a.img"; chmod u+w "$OUT/a.img"
nasm -f bin -o "$OUT/QEXIT.COM" "$ROOT/tools/qexit.asm" || exit 1
echo 'DEVICE=A:\XMGR.SYS|DOS=HIGH|DEVICE=A:\UDVD2.SYS /D:MSCD001|FILES=30|LASTDRIVE=Z' | crlf > "$OUT/CONFIG.SYS"
# setup adds its lines before the last line: A:\RUN.BAT does the steps
echo '@ECHO OFF|LH A:\MSCDEX.EXE /D:MSCD001 /L:R /M:10|A:\RUN.BAT' | crlf > "$OUT/AUTOEXEC.BAT"
echo '@ECHO OFF|ECHO --- setup > AUX|C:\PKG\INSTALL.EXE /S:A:\ANS.INI > AUX|ECHO --- CONFIG.SYS > AUX|TYPE A:\CONFIG.SYS > AUX|ECHO --- AUTOEXEC.BAT > AUX|TYPE A:\AUTOEXEC.BAT > AUX|A:\QEXIT' | crlf > "$OUT/RUN1.BAT"
echo '@ECHO OFF|ECHO --- R: > AUX|DIR R:\ > AUX|ECHO --- S: > AUX|DIR S:\ > AUX|ECHO --- E: > AUX|DIR E:\ > AUX|ECHO --- end > AUX|A:\QEXIT' | crlf > "$OUT/RUN2.BAT"
echo '@ECHO OFF|ECHO --- INSTALL /U > AUX|C:\OPENASPI\EUSBASPI\INSTALL.EXE /U > AUX|ECHO --- CONFIG.SYS > AUX|TYPE A:\CONFIG.SYS > AUX|ECHO --- AUTOEXEC.BAT > AUX|TYPE A:\AUTOEXEC.BAT > AUX|A:\QEXIT' | crlf > "$OUT/RUN3.BAT"
echo '[Answers]|Dir=C:\OPENASPI' | crlf > "$OUT/ANS.INI"
cp "$OUT/RUN1.BAT" "$OUT/RUN.BAT"
mcopy -o -i "$OUT/a.img" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" "$OUT/RUN.BAT" "$OUT/ANS.INI" "$OUT/QEXIT.COM" :: || exit 1

# C: 32 MB FAT16 with the package
truncate -s 32M "$OUT/c.img"
echo '63,,6,*' | sfdisk -q "$OUT/c.img" >/dev/null || exit 1
C="$OUT/c.img@@32256"
mformat -i "$C" -T 65473 -h 16 -s 63 -H 63 :: || exit 1
mmd -i "$C" ::/PKG && mcopy -i "$C" "$ROOT"/build/pkg/* ::/PKG/ || exit 1

printf 'IDE CD\r\n' > "$OUT/ide/IDECD.TXT"
printf 'USB CD\r\n' > "$OUT/usb/USBCD.TXT"
xorriso -as mkisofs -quiet -V IDECD -o "$OUT/ide.iso" "$OUT/ide" 2>/dev/null || exit 1
xorriso -as mkisofs -quiet -V USBCD -o "$OUT/usb.iso" "$OUT/usb" 2>/dev/null || exit 1
# the stick: 8 MB FAT16, partitioned
truncate -s 8M "$OUT/stick.img"
echo '63,,6,*' | sfdisk -q "$OUT/stick.img" >/dev/null || exit 1
mformat -i "$OUT/stick.img@@32256" -T 16321 -h 16 -s 63 -H 63 -v STICK :: || exit 1
printf 'USB stick\r\n' > "$OUT/STICK.TXT"; mcopy -i "$OUT/stick.img@@32256" "$OUT/STICK.TXT" :: || exit 1

boot() {   # n [keys after 10 s]
    {
        if [ -n "${2:-}" ]; then sleep 10; for k in $2; do echo "sendkey $k"; sleep 2; done; fi
        sleep ${WAIT:-75}; echo "screendump $OUT/screen$1.ppm"; sleep 1; echo quit
    } | qemu-system-i386 -machine pc ${ACCEL:--accel kvm} -m 32 -display none -vga std -monitor stdio \
        -drive if=floppy,format=raw,file="$OUT/a.img" -drive if=ide,index=0,format=raw,file="$OUT/c.img" \
        -drive if=ide,index=2,media=cdrom,file="$OUT/ide.iso" \
        -device usb-ehci,id=ehci -device usb-bot,id=bot,bus=ehci.0 \
        -drive if=none,id=ucd,media=cdrom,file="$OUT/usb.iso" -device scsi-cd,drive=ucd,bus=bot.0 \
        -drive if=none,id=stick,format=raw,file="$OUT/stick.img" -device usb-storage,drive=stick,bus=ehci.0 \
        -boot a -nic none \
        -serial file:"$OUT/serial$1.log" -device isa-debug-exit,iobase=0x501,iosize=0x02 > "$OUT/monitor$1.log" 2>&1
}
boot 1
mcopy -o -i "$OUT/a.img" "$OUT/RUN2.BAT" ::/RUN.BAT
boot 2
mcopy -o -i "$OUT/a.img" "$OUT/RUN3.BAT" ::/RUN.BAT
boot 3 "r ret ret"
for f in "$OUT"/*.ppm; do [ -f "$f" ] && magick "$f" "${f%.ppm}.png" && rm -f "$f"; done
for n in 1 2 3; do tr -d '\r' < "$OUT/serial$n.log"; done

python3 - "$OUT" <<'PYEOF'
import sys
o = sys.argv[1]
log = [open(f"{o}/serial{n}.log", "rb").read().decode("latin-1").replace("\r", "") for n in (1, 2, 3)]
def part(t, a, b):
    return [l.strip() for l in t.split(f"--- {a}")[-1].split(f"--- {b}")[0].split("\n") if l.strip()]
cfg, aut = part(log[0], "CONFIG.SYS", "AUTOEXEC.BAT"), part(log[0], "AUTOEXEC.BAT", "xxx")
e = cfg.index("DEVICE=C:\\OPENASPI\\EUSBASPI.SYS")
assert e < cfg.index("DEVICE=C:\\OPENASPI\\ASPIDSK.SYS"), cfg
assert e < cfg.index("DEVICE=C:\\OPENASPI\\ASPICDA.SYS /D:ASPICDA0"), cfg
assert "C:\\OPENASPI\\SHSUCDX.COM /D:MSCD001 /L:R /M:10 /D:ASPICDA0" in aut, aut
assert "IDECD" in "".join(part(log[1], "R:", "S:")), "R: (IDE CD)"
assert "USBCD" in "".join(part(log[1], "S:", "E:")), "S: (USB CD)"
assert "STICK" in "".join(part(log[1], "E:", "end")), "E: (USB stick)"
cfg3, aut3 = part(log[2], "CONFIG.SYS", "AUTOEXEC.BAT"), part(log[2], "AUTOEXEC.BAT", "xxx")
assert not any("OPENASPI" in l for l in cfg3), cfg3
assert "LH A:\\MSCDEX.EXE /D:MSCD001 /L:R /M:10" in aut3, aut3
print("EUSBASPI setup test passed")
PYEOF
