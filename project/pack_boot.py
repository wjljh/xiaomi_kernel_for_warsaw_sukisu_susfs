#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Android boot image v4 内核替换打包器（含 AVB 尾部重算）。

用法:
  pack_boot.py inspect <boot.img>
  pack_boot.py pack <template.img> <Image> <out.img>
"""
import struct, sys, os

PAGE = 4096
FOOTER = 64

def hdr(d):
    ks, rs, osv, hs = struct.unpack_from("<IIII", d, 8)
    hv = struct.unpack_from("<I", d, 40)[0]
    return ks, rs, osv, hs, hv

def footer_fields(d):
    if d[-64:-60] != b"AVBf":
        return None
    v = struct.unpack_from(">I", d, len(d) - 60)[0]
    orig = struct.unpack_from(">Q", d, len(d) - 52)[0]
    vbo = struct.unpack_from(">Q", d, len(d) - 44)[0]
    vbs = struct.unpack_from(">Q", d, len(d) - 36)[0]
    return v, orig, vbo, vbs

def round_up(x, a):
    return (x + a - 1) // a * a

def inspect(path):
    d = open(path, "rb").read()
    ks, rs, osv, hs, hv = hdr(d)
    print("文件            : %s" % path)
    print("大小            : %d 字节" % len(d))
    print("header_version  : %d   header_size: %d" % (hv, hs))
    print("kernel_size     : %d   ramdisk_size: %d" % (ks, rs))
    print("内核起始偏移    : %d，内核结束: %d" % (PAGE, PAGE + ks))
    print("ARM64 魔数      : %s" % d[PAGE+56:PAGE+60].hex())
    blob = d[PAGE:PAGE+ks]
    i = blob.find(b"6.6.")
    if i >= 0:
        j = blob.find(b"\x00", i)
        print("内核版本        : %s" % blob[i:j].decode("utf-8", "replace")[:80])
    f = footer_fields(d)
    if f:
        v, orig, vbo, vbs = f
        print("AVB 尾部        : ver=%d original_size=%d vbmeta_offset=%d vbmeta_size=%d" % f)
        print("  vbmeta 区间   : [%d, %d)" % (vbo, vbo + vbs))
        print("  与内核冲突    : %s" % ("是!" if PAGE + ks > vbo else "否"))
    else:
        print("AVB 尾部        : 无")

def pack(template, image, out):
    d = bytearray(open(template, "rb").read())
    k = open(image, "rb").read()
    ks, rs, osv, hs, hv = hdr(d)
    f = footer_fields(d)
    if f is None:
        print("模板没有 AVB 尾部，拒绝打包")
        return 1
    _, orig0, vbo0, vbs0 = f
    vbmeta = bytes(d[vbo0:vbo0 + vbs0])          # 原样保留 vbmeta 块
    if k[56:60] != b"ARM\x64":
        print("警告: 新内核偏移 56 处不是 ARMd 魔数: %s" % k[56:60].hex())

    new_kend = PAGE + len(k)
    new_vbo = round_up(new_kend, PAGE)            # 新 vbmeta 位置（页对齐）
    new_vend = new_vbo + vbs0
    if new_vend + FOOTER > len(d):
        print("错误: 新布局超出分区大小")
        return 1

    # 全清数据区（保留 4096 字节头），再写内核与 vbmeta
    d[PAGE:] = b"\x00" * (len(d) - PAGE)
    d[PAGE:new_kend] = k
    d[new_vbo:new_vend] = vbmeta
    struct.pack_into("<I", d, 8, len(k))          # kernel_size

    # 重写 AVB 尾部（64 字节，大端）
    struct.pack_into(">I", d, len(d) - 64, 0x41564266)   # "AVBf"
    struct.pack_into(">I", d, len(d) - 60, 1)            # version major
    struct.pack_into(">I", d, len(d) - 56, 0)            # version minor
    struct.pack_into(">Q", d, len(d) - 52, new_vbo)      # original_image_size
    struct.pack_into(">Q", d, len(d) - 44, new_vbo)      # vbmeta_offset
    struct.pack_into(">Q", d, len(d) - 36, vbs0)         # vbmeta_size

    open(out, "wb").write(bytes(d))
    print("打包完成 -> %s" % out)
    print("  模板内核 %d 字节 -> 新内核 %d 字节" % (ks, len(k)))
    print("  新 vbmeta 偏移 %d（旧 %d），kernel_size 字段 = %d" % (new_vbo, vbo0, len(k)))
    print("  文件大小 %d（模板 %d，一致: %s）" % (len(d), os.path.getsize(template), len(d) == os.path.getsize(template)))
    print("  --- 回读校验 ---")
    inspect(out)
    return 0

if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "inspect":
        inspect(sys.argv[2])
    elif len(sys.argv) >= 5 and sys.argv[1] == "pack":
        sys.exit(pack(sys.argv[2], sys.argv[3], sys.argv[4]))
    else:
        print(__doc__)
