#!/usr/bin/env python3
"""按清单把 ROM 复制到 SD 卡并改成英文名（源文件不动）。
用法: scripts/rom-copy.py <清单文件> <源目录> <目标目录>

清单格式：每行 "来源<TAB>新文件名"，# 开头的行忽略。来源可以是：
    普通文件的相对路径                  Arkanoid (U).nes
    压缩包里的文件（和 rom-scan.py 一样）  动作/魂斗罗.zip::魂斗罗(U).nes
    CRC32（从 roms/scan.tsv 查来源）       DCB94341

复制前检查 iNES 头和大小，复制后逐字节比对；目标已存在且内容相同则跳过。
"""
import os
import sys
import zipfile

MAX_SIZE = 0x40000 - 128


def crc_index(report="roms/scan.tsv"):
    """CRC32 → 来源（从 rom-scan.py 的报告里查），压缩包里文件名乱码时用 CRC 指定更可靠"""
    index = {}
    if os.path.exists(report):
        for line in open(report, encoding="utf-8"):
            cols = line.rstrip("\n").split("\t")
            if len(cols) == 8 and cols[0] in ("ok", "dup"):
                index.setdefault(cols[6].upper(), cols[7])
    return index


def load(src_root, source):
    if "::" in source:
        archive, member = source.split("::", 1)
        with zipfile.ZipFile(os.path.join(src_root, archive)) as z:
            return z.read(member)
    with open(os.path.join(src_root, source), "rb") as f:
        return f.read()


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    plan, src_root, dst_root = sys.argv[1:4]
    os.makedirs(dst_root, exist_ok=True)

    done = skipped = failed = 0
    total = 0
    names = set()
    crcs = crc_index()
    for lineno, line in enumerate(open(plan, encoding="utf-8"), 1):
        line = line.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        try:
            source, new_name = line.split("\t")
        except ValueError:
            print(f"第 {lineno} 行格式不对（要用 TAB 分隔）: {line}")
            failed += 1
            continue
        if not new_name.isascii() or "/" in new_name or not new_name.lower().endswith(".nes"):
            print(f"第 {lineno} 行新文件名必须是英文、以 .nes 结尾、不含 /: {new_name}")
            failed += 1
            continue
        if new_name.lower() in names:
            print(f"第 {lineno} 行新文件名重复: {new_name}")
            failed += 1
            continue
        names.add(new_name.lower())

        if len(source) == 8 and all(c in "0123456789abcdefABCDEF" for c in source):
            if source.upper() not in crcs:
                print(f"第 {lineno} 行 CRC {source} 在 roms/scan.tsv 里找不到")
                failed += 1
                continue
            source = crcs[source.upper()]

        try:
            data = load(src_root, source)
        except (OSError, KeyError, zipfile.BadZipFile) as e:
            print(f"读取失败: {source} ({e})")
            failed += 1
            continue
        if data[:4] != b"NES\x1a":
            print(f"不是 iNES 文件: {source}")
            failed += 1
            continue
        if len(data) > MAX_SIZE:
            print(f"太大（{len(data)} 字节）: {source}")
            failed += 1
            continue

        dst = os.path.join(dst_root, new_name)
        if os.path.exists(dst) and open(dst, "rb").read() == data:
            print(f"已存在，跳过: {new_name}")
            skipped += 1
            continue
        with open(dst, "wb") as f:
            f.write(data)
        if open(dst, "rb").read() != data:
            print(f"复制后比对不一致: {new_name}")
            failed += 1
            continue
        total += len(data)
        print(f"{len(data):>8}  {new_name}")
        done += 1

    os.sync()
    print(f"\n复制 {done} 个（{total // 1024}KB），跳过 {skipped} 个，失败 {failed} 个")


if __name__ == "__main__":
    main()
