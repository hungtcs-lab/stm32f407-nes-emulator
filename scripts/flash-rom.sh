#!/usr/bin/env bash
# 把 .nes 文件烧进板子的 Flash ROM 存储区（0x08040000），不需要 SD 卡
# 用法: scripts/flash-rom.sh roms/xxx.nes
# 固件启动时如果 SD 卡上没有这个 ROM，会自动导出一份到 SD:/NES/
set -euo pipefail
cd "$(dirname "$0")/.."
[ $# -eq 1 ] || { echo "用法: $0 <rom.nes>"; exit 1; }
out=build/romstore.bin
python3 - "$1" "$out" <<'PY'
import os, struct, sys, zlib
src, out = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
assert data[:4] == b'NES\x1a', 'not an iNES file'
MAX = 0x40000 - 128
assert len(data) <= MAX, f'ROM too big: {len(data)} > {MAX}'
name = os.path.basename(src).encode()[:63]
hdr = struct.pack('<4I', 0x5253454E, 1, len(data), zlib.crc32(data)) + name.ljust(64, b'\0') + b'\xff' * 48
assert len(hdr) == 128
open(out, 'wb').write(hdr + data)
print(f'{os.path.basename(src)}: {len(data)} bytes, crc32 {zlib.crc32(data):08X}')
PY
st-flash --reset write "$out" 0x08040000 2>&1 | grep -E 'jolly|ERROR|error' || true
