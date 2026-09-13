#!/usr/bin/env python3
"""扫描目录里的 .nes 文件（包括 .zip 压缩包里的），判断能不能在这块板子上运行。
用法: scripts/rom-scan.py <目录> [报告文件，默认 roms/scan.tsv]

判断条件：
  - iNES 文件头正确
  - 文件大小 ≤ 262,016 字节（Flash ROM 存储区 256KB 减去 128 字节头）
  - Mapper 在移植版 InfoNES 支持列表里（去掉了 5/6/19/85/188/235）

报告是 TSV，列: 结果 大小 mapper PRG(K) CHR(K) 电池 CRC32 来源
来源格式: 普通文件是相对路径；压缩包里的是 "压缩包相对路径::包内文件名"（rom-copy.py 认这个格式）
同一个 CRC32 只保留第一次出现的，重复的标为 dup。
"""
import os
import sys
import zipfile
import zlib

MAX_SIZE = 0x40000 - 128

# lib/infones/mapper/ 里有的 Mapper，减去 INFONES_SMALL_RAM 下去掉的
SUPPORTED = {
    0, 1, 2, 3, 4, 7, 8, 9, 10, 11, 13, 15, 16, 17, 18, 21, 22, 23, 24, 25, 26,
    32, 33, 34, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 57, 58, 60, 61, 62,
    64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 82, 83,
    86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 99, 100, 101, 105, 107, 108,
    109, 110, 112, 113, 114, 115, 116, 117, 118, 119, 122, 133, 134, 135, 140,
    151, 160, 180, 181, 182, 183, 185, 187, 189, 191, 193, 194, 200, 201, 202,
    222, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 236, 240, 241, 242,
    243, 244, 245, 246, 248, 249, 251, 252, 255,
}


def iter_roms(root):
    """产出 (来源, 数据)"""
    for dirpath, _, names in os.walk(root, followlinks=True):
        for name in sorted(names):
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, root)
            low = name.lower()
            if low.endswith(".nes"):
                with open(path, "rb") as f:
                    yield rel, f.read()
            elif low.endswith(".zip"):
                try:
                    with zipfile.ZipFile(path) as z:
                        for info in z.infolist():
                            if info.filename.lower().endswith(".nes") and not info.is_dir():
                                yield f"{rel}::{info.filename}", z.read(info)
                except (zipfile.BadZipFile, OSError) as e:
                    yield f"{rel}", None


def classify(data):
    if data is None:
        return "bad", "-", 0, 0, ""
    if len(data) < 16 or data[:4] != b"NES\x1a":
        return "not-ines", "-", 0, 0, ""
    mapper = (data[6] >> 4) | (data[7] & 0xF0)
    prg, chr_ = data[4] * 16, data[5] * 8
    battery = "yes" if data[6] & 2 else ""
    if len(data) > MAX_SIZE:
        return "too-big", mapper, prg, chr_, battery
    if mapper not in SUPPORTED:
        return "mapper", mapper, prg, chr_, battery
    return "ok", mapper, prg, chr_, battery


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    root = sys.argv[1]
    report = sys.argv[2] if len(sys.argv) == 3 else "roms/scan.tsv"
    os.makedirs(os.path.dirname(report) or ".", exist_ok=True)

    seen = set()
    counts = {}
    rows = []
    for src, data in iter_roms(root):
        status, mapper, prg, chr_, battery = classify(data)
        crc = f"{zlib.crc32(data):08X}" if data else "-"
        if status == "ok":
            if crc in seen:
                status = "dup"
            seen.add(crc)
        counts[status] = counts.get(status, 0) + 1
        size = len(data) if data else 0
        rows.append((status, size, mapper, prg, chr_, battery, crc, src))

    order = {"ok": 0, "dup": 1, "too-big": 2, "mapper": 3, "not-ines": 4, "bad": 5}
    rows.sort(key=lambda r: (order.get(r[0], 9), r[7]))
    with open(report, "w", encoding="utf-8") as f:
        f.write("status\tsize\tmapper\tprg_k\tchr_k\tbattery\tcrc32\tsource\n")
        for r in rows:
            f.write("\t".join(str(x) for x in r) + "\n")

    labels = {"ok": "能运行", "dup": "重复", "too-big": "太大", "mapper": "Mapper 不支持",
              "not-ines": "不是 iNES", "bad": "压缩包损坏"}
    print("  ".join(f"{labels.get(k, k)} {v}" for k, v in sorted(counts.items(), key=lambda kv: order.get(kv[0], 9))))
    print(f"报告: {report}")


if __name__ == "__main__":
    main()
