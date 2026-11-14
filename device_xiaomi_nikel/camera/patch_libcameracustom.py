#!/usr/bin/env python3
# nikel: give the main camera (S5K3L8, sensor ID 0x30c8) an autofocus motor.
#
# SHIFT's libcameracustom.so pairs lens drivers with sensors. Its MAIN lens
# list has: 0xffff dummy, 0x0258 (IMX258) DW9714AF, 0x3109 (S5K3P9SX) DW9714AF,
# 0x0230 BU63165AF. On SHIFT the S5K3L8 is a front camera, so the main list has
# no entry for it and the HAL falls back to the dummy lens (no AF).
# nikel has no IMX258, so that entry's sensor ID is changed to 0x30c8.
# One instruction per library, in the static initializer that fills
# LensList_main:
#   lib   (thumb2) va 0x2c0fc: mov.w r0,#0x258 -> movw r0,#0x30c8
#   lib64 (arm64)  va 0x36274: mov x9,#0x258   -> mov x9,#0x30c8 (movk 0x9714<<32 follows)
# Each patch checks the original bytes first, and is skipped if already applied.
#
# Usage: patch_libcameracustom.py <vendor/xiaomi/nikel/proprietary dir>
import os, sys

PATCHES = [
    ("vendor/lib/libcameracustom.so",   0x2b0fc, "4ff41670", "43f2c800"),
    ("vendor/lib64/libcameracustom.so", 0x30274, "094b80d2", "091986d2"),
]

root = sys.argv[1]
rc = 0
for rel, off, old, new in PATCHES:
    p = os.path.join(root, rel)
    if not os.path.isfile(p):
        print("libcameracustom patch: missing " + p); rc = 1; continue
    with open(p, "rb") as f:
        d = bytearray(f.read())
    cur = d[off:off + 4].hex()
    if cur == new:
        print("libcameracustom patch: already applied: " + rel)
    elif cur == old:
        d[off:off + 4] = bytes.fromhex(new)
        with open(p, "wb") as f:
            f.write(d)
        print("libcameracustom patch: applied: " + rel)
    else:
        print("libcameracustom patch: UNEXPECTED BYTES in %s (%s) - not patched, stop and report" % (rel, cur))
        rc = 1
sys.exit(rc)
