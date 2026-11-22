#!/bin/bash
# PCI4G.SYS in QEMU: BARHIGH.SYS puts the xHCI's 64-bit BAR above 4 GB (as
# firmware with "Above 4G Decoding" does), PCI4G.SYS moves it below 4 GB,
# EUSBASPI /X reads the stick on it.  Cases: bus 0, behind a pci-bridge,
# under JEMMEX, without BARHIGH (nothing to move) and without PCI4G (the
# hint of EUSBASPI).  Needs what test/run.sh needs.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$ROOT/build/pci4g; rm -rf "$W"; mkdir -p "$W"
nasm -f bin -DE9LOG -o "$W/PCI4G.SYS" "$ROOT/src/pci4g.asm" || exit 1
nasm -f bin -o "$W/BARHIGH.SYS" "$ROOT/tools/barhigh.asm" || exit 1
D='A:\BARHIGH.SYS|A:\PCI4G.SYS /V|A:\EUSBASPI.SYS /X /V|A:\ASPIDISK.SYS'
fail=0
t() {   # name, expected line (grep -E), env...
    local n=$1 want=$2; shift 2
    env EXTRA_FILES="$W/PCI4G.SYS $W/BARHIGH.SYS" USBHC=qemu-xhci "$@" \
        "$ROOT/test/run.sh" > "$W/$n.log" 2>&1
    if tr -d '\r' < "$W/$n.log" | grep -aqE "$want"; then echo "OK   $n"
    else echo "FAIL $n"; fail=1; fi
}
moved='PCI4G: 0.:0.\.0 BAR 10h 00000001........ \(16K\) -> F'
read_ok='USB stick read OK'
t bus0    "$moved" DEVICES="$D"
grep -aq "$read_ok" "$W/bus0.log" || { echo "FAIL bus0: stick not read"; fail=1; }
t bridge  "$moved" DEVICES="$D" USBHC='pci-bridge,chassis_nr=1' \
          QEMU_EXTRA='-device qemu-xhci,bus=usbhc,addr=1,id=xh' USBBUS=xh
grep -aq "$read_ok" "$W/bridge.log" || { echo "FAIL bridge: stick not read"; fail=1; }
t jemmex  "$moved" DEVICES="$D" EMM=1
grep -aq "$read_ok" "$W/jemmex.log" || { echo "FAIL jemmex: stick not read"; fail=1; }
t nothing 'PCI4G: no USB or disk controller BAR above 4 GB' \
          DEVICES='A:\PCI4G.SYS /V|A:\EUSBASPI.SYS /X /V|A:\ASPIDISK.SYS'
t hint    'BAR above 4 GB: load PCI4G.SYS' TIMEOUT=25 \
          DEVICES='A:\BARHIGH.SYS|A:\EUSBASPI.SYS /X /V|A:\ASPIDISK.SYS'
exit $fail
