#!/usr/bin/env python3
"""不接串口，直接用 ST-Link 读出板子 RAM 里的日志和状态。
用法: scripts/diag.py [build/xxx.elf]   默认 nes-emulator.elf"""
import os, struct, subprocess, sys, tempfile

elf = sys.argv[1] if len(sys.argv) > 1 else "build/nes-emulator.elf"
syms = {}
for line in subprocess.run(["arm-none-eabi-nm", elf], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3: syms[p[2]] = int(p[0], 16)

def read(addr, size):
    with tempfile.NamedTemporaryFile(delete=False) as f: path = f.name
    r = subprocess.run(["st-flash", "read", path, hex(addr), str(size)], capture_output=True, text=True)
    data = open(path, "rb").read(); os.unlink(path)
    if len(data) != size: sys.exit("读取失败:\n" + r.stderr)
    return data

if "g_test" in syms:
    magic, stage, npass, nfail, done = struct.unpack("<5I", read(syms["g_test"], 20))
    print(f"g_test : magic={magic:08X} stage={stage} pass={npass} fail={nfail} done={done}")
if "g_nes" in syms:
    d = read(syms["g_nes"], 80)
    magic, frames, fps10, emu, lcd, skip, inram, size = struct.unpack_from("<8I", d)
    name = d[32:80].split(b'\0')[0].decode(errors='replace')
    print(f"g_nes  : frames={frames} fps={fps10/10:.1f} emu={emu}us/frame lcd={lcd}us quality={skip} "
          f"rom={name!r} {size}B in {'RAM' if inram else 'flash'}")

LOG_SIZE = 4096
lg = read(syms["g_log"], 8 + LOG_SIZE)
lmagic, llen = struct.unpack_from("<2I", lg)
buf = lg[8:]
if lmagic != 0xD1A6C0DE:
    sys.exit(f"日志 magic 不对 ({lmagic:08X})，程序可能没跑到 board_init")
if llen > LOG_SIZE:
    start = llen % LOG_SIZE
    buf = buf[start:] + buf[:start]
else:
    buf = buf[:llen]
print("-" * 60)
print(buf.decode("utf-8", "replace"))
