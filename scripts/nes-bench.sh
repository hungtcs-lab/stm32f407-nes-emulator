#!/usr/bin/env bash
# NES 性能测试：用独立的 build-bench 目录编译（自动按键；要测极限速度加 NES_NO_PACING），烧录后等跑完读结果
# 用法: scripts/nes-bench.sh "<额外 NES_DEFS，分号分隔>" ["<InfoNES 核心优化选项，默认 -O2>"]
set -uo pipefail
cd "$(dirname "$0")/.."
DEFS=${1:-}; OPT=${2:--O2}
# 环境变量: AUTOPLAY（默认 SMB 的按键脚本）、CRC（要输出 CRC 的帧号列表）、EXTRA_CFLAGS（所有 C 文件）
AUTOPLAY=${AUTOPLAY:-start:150-156,right:300-600,a:420-440}
ALL="NES_BENCH;NES_AUTOSTART_MS=500;NES_AUTOPLAY=\"$AUTOPLAY\""
[ -n "${CRC:-}" ] && ALL="$ALL;NES_CRC_FRAMES=\"$CRC\""
[ -n "$DEFS" ] && ALL="$ALL;$DEFS"
cmake -S . -B build-bench -G Ninja "-DNES_DEFS=$ALL" "-DINFONES_OPT=$OPT" "-DCMAKE_C_FLAGS=${EXTRA_CFLAGS:-}" >/dev/null
cmake --build build-bench --target nes-emulator.elf >build-bench/last.log 2>&1 || { grep -E 'error' build-bench/last.log | head; exit 1; }
grep -E 'FLASH:|RAM:|FAST:|STACK:' build-bench/last.log
st-flash --reset write build-bench/nes-emulator.bin 0x08000000 >/dev/null 2>&1 || { echo flash failed; exit 1; }
for i in $(seq 1 ${WAIT:-60}); do
    sleep 1
    [ $((i % 10)) -eq 0 ] || continue
    out=$(ELF=build-bench/nes-emulator.elf ./scripts/diag.py build-bench/nes-emulator.elf 2>&1)
    if grep -q 'bench 300-900' <<<"$out" && { [ -z "${CRC:-}" ] || [ "$(grep -c ' crc ' <<<"$out")" -ge "$(tr ',' '\n' <<<"$CRC" | wc -l)" ]; }; then
        grep -E 'g_nes|bench|run:|crc|load:|export|menu' <<<"$out"; exit 0
    fi
done
echo "timeout"; ./scripts/diag.py build-bench/nes-emulator.elf 2>&1 | tail -8
