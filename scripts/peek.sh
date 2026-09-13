#!/usr/bin/env bash
# 用 ST-Link 读内存并按 32 位小端显示: peek.sh <地址|符号> [字节数]
set -euo pipefail
addr=$1; size=${2:-4}
if [[ ! $addr =~ ^0x ]]; then
    addr=0x$(arm-none-eabi-nm ${ELF:-build/nes-emulator.elf} | awk -v s="$addr" '$3==s{print $1}')
fi
tmp=$(mktemp)
st-flash read "$tmp" "$addr" "$size" >/dev/null 2>&1
xxd -e -g4 "$tmp" | sed "s/^0*\([0-9a-f]*\):/  +\1:/"
rm -f "$tmp"
