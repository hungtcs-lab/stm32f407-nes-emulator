#!/usr/bin/env bash
# SD 稳定性测试：用给定编译参数编译烧录，复位跑 N 次，统计结果
# 用法: scripts/sd-soak.sh <次数> [额外 CFLAGS，如 "-DSD_CLOCK_DIV=2 -DSD_NO_PROBE"]
set -uo pipefail
cd "$(dirname "$0")/.."
N=${1:-5}; EXTRA=${2:-}
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="$EXTRA" >/dev/null
cmake --build build >build/last.log 2>&1 || { tail -20 build/last.log; exit 1; }
st-flash write build/selftest.bin 0x08000000 >/dev/null 2>&1 || { echo "flash failed"; exit 1; }
echo "### $EXTRA  x$N"
for i in $(seq 1 "$N"); do
    st-flash reset >/dev/null 2>&1
    sleep 14
    ./scripts/diag.py build/selftest.elf 2>&1 | grep -E 'g_test|FAIL|err=|KB/s|timeout|attempt|not ready' | sed "s/^/  [$i] /"
done
