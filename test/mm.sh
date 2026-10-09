# Memory manager setup for the QEMU tests (sourced by test/run.sh).
#
#   MM=none     no memory manager (real mode, no VDS)
#   MM=himem    MS HIMEM.SYS only (XMS, real mode)
#   MM=emm386   MS HIMEM.SYS + EMM386.EXE (V86 mode, VDS, UMBs)
#   MM=jemm386  HimemX + Jemm386 (V86 mode, VDS, UMBs)
#   MM=jemmex   JemmEx (V86 mode, VDS, UMBs)
#   MM=386max   386MAX.SYS (V86 mode, VDS, UMBs)
#   MMOPTS      options for the EMM (default NOEMS; 386MAX: none)
#   HIGH=1      load the ASPI drivers with DEVICEHIGH and MSCDEX with LH
#
# Sources (override if needed):
#   MSCAB   Windows 9x cabinet with HIMEM.SYS/EMM386.EXE
#   JEMM_ZIP, HIMEMX_ZIP, MAX386 (386MAX.SYS, see ~/mars/386MAX: make)

MSCAB=${MSCAB:-"$HOME/Iso/Windows 98/Windows98/WIN95/WIN95_02.CAB"}
JEMM_ZIP=${JEMM_ZIP:-$DOS/JemmB_v586.zip}
HIMEMX_ZIP=${HIMEMX_ZIP:-$DOS/HimemX340.zip}
MAX386=${MAX386:-$HOME/mars/386MAX/build/bin/386MAX.SYS}

# mm_setup OUTDIR IMAGE: copy the files of $MM onto IMAGE and print the
# CONFIG.SYS lines that load it (without CR)
mm_setup() {
    local out=$1 img=$2 mm=${MM:-none} f
    mkdir -p "$out/mm"
    case "$mm" in
    none) return 0 ;;
    himem|emm386)
        (cd "$out/mm" && cabextract -q -F himem.sys -F emm386.exe "$MSCAB" 2>/dev/null)
        mcopy -o -i "$img" "$out/mm/himem.sys" ::/HIMEM.SYS
        echo 'DEVICE=A:\HIMEM.SYS'
        if [ "$mm" = emm386 ]; then
            mcopy -o -i "$img" "$out/mm/emm386.exe" ::/EMM386.EXE
            echo "DEVICE=A:\\EMM386.EXE ${MMOPTS-NOEMS}"
            echo 'DOS=HIGH,UMB'
        fi ;;
    jemm386)
        unzip -p "$HIMEMX_ZIP" HimemX.exe > "$out/mm/HIMEMX.EXE"
        unzip -p "$JEMM_ZIP" JEMM386.EXE > "$out/mm/JEMM386.EXE"
        mcopy -o -i "$img" "$out/mm/HIMEMX.EXE" "$out/mm/JEMM386.EXE" ::/
        echo 'DEVICE=A:\HIMEMX.EXE'
        echo "DEVICE=A:\\JEMM386.EXE ${MMOPTS-NOEMS}"
        echo 'DOS=HIGH,UMB' ;;
    jemmex)
        unzip -p "$JEMM_ZIP" JEMMEX.EXE > "$out/mm/JEMMEX.EXE"
        mcopy -o -i "$img" "$out/mm/JEMMEX.EXE" ::/
        echo "DEVICE=A:\\JEMMEX.EXE ${MMOPTS-NOEMS}"
        echo 'DOS=HIGH,UMB' ;;
    386max)
        mcopy -o -i "$img" "$MAX386" ::/386MAX.SYS
        # without its VxD next to it, 386MAX waits for a key at boot
        mcopy -o -i "$img" "$(dirname "$MAX386")/386MAX.VXD" ::/386MAX.VXD
        echo "DEVICE=A:\\386MAX.SYS ${MMOPTS-}"
        echo 'DOS=HIGH,UMB' ;;
    *) echo "unknown MM=$mm" >&2; return 1 ;;
    esac
}

# mm_high: DEVICE= -> DEVICEHIGH= for the given driver names if HIGH=1
mm_high() {
    if [ "${HIGH:-0}" = 1 ]; then
        sed -E 's/^DEVICE=(.*(AHCIASPI|EUSBASPI|ASPICD|ASPIDISK)[^ ]*\.SYS)/DEVICEHIGH=\1/I'
    else
        cat
    fi
}
