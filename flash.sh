#!/usr/bin/env bash
# 编译并烧录。用法: ./flash.sh [nes-emulator|selftest]
# 先试 probe-rs，失败自动回退 st-flash（能列出探头 != 能用探头）
set -euo pipefail
cd "$(dirname "$0")"
TARGET="${1:-nes-emulator}"
CHIP=STM32F407VETx

[ -d build ] || cmake -S . -B build -G Ninja
cmake --build build

if probe-rs download --chip "$CHIP" "build/$TARGET.elf" 2>/dev/null; then
    probe-rs reset --chip "$CHIP"
    echo ">>> probe-rs 烧录完成"
elif st-flash --reset write "build/$TARGET.bin" 0x08000000; then
    echo ">>> st-flash 烧录完成"
else
    echo "!!! 烧录失败：没有可用的调试器" >&2
    exit 1
fi
echo "串口: 115200 8N1, J6 的 2=GND 3=PA10/RX 4=PA9/TX；不接串口可用 scripts/diag.py 读日志"
