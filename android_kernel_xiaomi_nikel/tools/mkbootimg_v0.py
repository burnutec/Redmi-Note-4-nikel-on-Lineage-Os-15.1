#!/usr/bin/env python3
"""Minimal Android boot image (header v0) packer/unpacker, no dependencies.

Packs exactly like AOSP mkbootimg for header version 0:
  header page | kernel (page aligned) | ramdisk (page aligned) | [second]
  id = SHA1(kernel, kernel_size, ramdisk, ramdisk_size, second, second_size)

Usage:
  mkbootimg_v0.py pack --kernel K --ramdisk R --out boot.img \
        [--base 0x40000000 --kernel_offset 0x80000 --ramdisk_offset 0x05000000
         --tags_offset 0x04000000 --pagesize 2048 --name NAME --cmdline STR]
  mkbootimg_v0.py pack --like ref.img --kernel K --out boot.img
        (copy every header value and the ramdisk from ref.img, replace kernel)
  mkbootimg_v0.py info boot.img
"""
import argparse
import hashlib
import struct
import sys

MAGIC = b"ANDROID!"


def parse(path):
    d = open(path, "rb").read()
    if d[:8] != MAGIC:
        sys.exit(f"{path}: not an Android boot image")
    (ks, ka, rs, ra, ss, sa, ta, ps, hv, osv) = struct.unpack_from("<10I", d, 8)
    name = d[48:64]
    cmd = d[64:576]
    idf = d[576:608]
    extra = d[608:1632]
    pg = lambda n: (n + ps - 1) // ps * ps
    off = ps
    kernel = d[off:off + ks]
    off += pg(ks)
    ramdisk = d[off:off + rs]
    off += pg(rs)
    second = d[off:off + ss]
    return dict(kernel=kernel, ramdisk=ramdisk, second=second, kernel_addr=ka,
                ramdisk_addr=ra, second_addr=sa, tags_addr=ta, pagesize=ps,
                header_version=hv, os_version=osv, name=name, cmdline=cmd,
                extra_cmdline=extra, id=idf)


def pack(h, out):
    ps = h["pagesize"]
    k, r, s = h["kernel"], h["ramdisk"], h["second"]
    sha = hashlib.sha1()
    for blob in (k, r, s):
        sha.update(blob)
        sha.update(struct.pack("<I", len(blob)))
    idf = sha.digest().ljust(32, b"\0")
    hdr = MAGIC + struct.pack("<10I", len(k), h["kernel_addr"], len(r), h["ramdisk_addr"],
                              len(s), h["second_addr"], h["tags_addr"], ps,
                              h["header_version"], h["os_version"])
    hdr += h["name"].ljust(16, b"\0")[:16]
    hdr += h["cmdline"].ljust(512, b"\0")[:512]
    hdr += idf
    hdr += h["extra_cmdline"].ljust(1024, b"\0")[:1024]
    pad = lambda b: b + b"\0" * ((ps - len(b) % ps) % ps)
    img = pad(hdr) + pad(k) + pad(r) + (pad(s) if s else b"")
    open(out, "wb").write(img)
    return img, idf


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack")
    p.add_argument("--like")
    p.add_argument("--kernel")
    p.add_argument("--ramdisk")
    p.add_argument("--out", required=True)
    p.add_argument("--base", type=lambda x: int(x, 0))
    p.add_argument("--kernel_offset", type=lambda x: int(x, 0))
    p.add_argument("--ramdisk_offset", type=lambda x: int(x, 0))
    p.add_argument("--tags_offset", type=lambda x: int(x, 0))
    p.add_argument("--pagesize", type=int)
    p.add_argument("--name")
    p.add_argument("--cmdline")
    i = sub.add_parser("info")
    i.add_argument("img")
    a = ap.parse_args()

    if a.cmd == "info":
        h = parse(a.img)
        base = h["kernel_addr"] - 0x80000
        print(f"kernel {len(h['kernel'])} B @0x{h['kernel_addr']:08x}  ramdisk {len(h['ramdisk'])} B @0x{h['ramdisk_addr']:08x}")
        print(f"tags 0x{h['tags_addr']:08x}  pagesize {h['pagesize']}  base 0x{base:08x}")
        print(f"name {h['name'].rstrip(bytes(1))!r}  cmdline {h['cmdline'].rstrip(bytes(1)).decode(errors='replace')!r}")
        print(f"id {h['id'][:20].hex()}")
        return

    if a.like:
        h = parse(a.like)
    else:
        h = dict(second=b"", second_addr=0, header_version=0, os_version=0,
                 name=b"", cmdline=b"", extra_cmdline=b"", pagesize=2048)
        base = a.base if a.base is not None else 0x40000000
        h["kernel_addr"] = base + (a.kernel_offset if a.kernel_offset is not None else 0x80000)
        h["ramdisk_addr"] = base + (a.ramdisk_offset if a.ramdisk_offset is not None else 0x05000000)
        h["tags_addr"] = base + (a.tags_offset if a.tags_offset is not None else 0x04000000)
        h["second_addr"] = base + 0x00f00000
    if a.kernel:
        h["kernel"] = open(a.kernel, "rb").read()
    if a.ramdisk:
        h["ramdisk"] = open(a.ramdisk, "rb").read()
    if "kernel" not in h or "ramdisk" not in h:
        sys.exit("need --kernel and --ramdisk (or --like to take them from a reference image)")
    if a.pagesize:
        h["pagesize"] = a.pagesize
    if a.name is not None:
        h["name"] = a.name.encode()
    if a.cmdline is not None:
        h["cmdline"] = a.cmdline.encode()
    img, idf = pack(h, a.out)
    print(f"wrote {a.out}: {len(img)} bytes, id {idf[:20].hex()}")


if __name__ == "__main__":
    main()
