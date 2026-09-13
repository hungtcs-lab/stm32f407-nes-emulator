#!/usr/bin/env bash
# SD 稳定性测试：用给定编译参数编译烧录，复位跑 N 次，统计结果
# 用法: scripts/sd-soak.sh <次数> [额外 CFLAGS，如 "-DSD_CLOCK_DIV=8"]
# 用独立的 build-soak 目录，不影响 build/ 里正式固件的编译参数；测完记得 ./flash.sh 烧回正式固件
set -uo pipefail
cd "$(dirname "$0")/.."
N=${1:-5}; EXTRA=${2:-}
cmake -S . -B build-soak -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="$EXTRA" >/dev/null
cmake --build build-soak --target selftest.elf >build-soak/last.log 2>&1 || { tail -20 build-soak/last.log; exit 1; }
st-flash write build-soak/selftest.bin 0x08000000 >/dev/null 2>&1 || { echo "flash failed"; exit 1; }
echo "### $EXTRA  x$N"
for i in $(seq 1 "$N"); do
    st-flash reset >/dev/null 2>&1
    sleep 14
    ./scripts/diag.py build-soak/selftest.elf 2>&1 | grep -E 'g_test|FAIL|err=|KB/s|timeout|attempt|not ready' | sed "s/^/  [$i] /"
done
