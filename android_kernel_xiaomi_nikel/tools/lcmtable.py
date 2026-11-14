"""Extract MTK LCM_setting_table arrays from a .c file and find them in a kernel Image.

struct LCM_setting_table { unsigned int cmd; unsigned char count; unsigned char para_list[64]; }
-> 4 + 1 + 64 = 69 bytes, padded to 72 (4-byte alignment) on arm64/arm.
"""
import re
import struct
import sys

REC = 72


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def tables(src):
    """name -> list of (cmd, count, params) honoring #if 0 / #if 1 blocks crudely."""
    out = {}
    # resolve simple #if 0/1 ... #else ... #endif (non-nested)
    def resolve(s):
        pat = re.compile(r"#if\s+([01])\b(.*?)(?:#else(.*?))?#endif", re.S)
        while True:
            m = pat.search(s)
            if not m:
                return s
            keep = m.group(2) if m.group(1) == "1" else (m.group(3) or "")
            s = s[:m.start()] + keep + s[m.end():]
    src = resolve(strip_comments(src))
    for m in re.finditer(r"struct\s+LCM_setting_table\s+(\w+)\s*\[\]\s*=\s*\{(.*?)\n\};", src, re.S):
        rows = []
        for r in re.finditer(r"\{\s*(0x[0-9A-Fa-f]+|\w+)\s*,\s*(\w+)\s*,\s*\{([^}]*)\}\s*\}", m.group(2)):
            cmd, cnt, params = r.groups()
            try:
                cmd = int(cmd, 0)
                cnt = int(cnt, 0)
            except ValueError:
                cmd = {"REGFLAG_DELAY": 0xFC, "REGFLAG_END_OF_TABLE": 0xFD}.get(cmd, cmd)
                cnt = int(cnt, 0) if str(cnt).isdigit() or str(cnt).startswith("0x") else 0
            ps = [int(x, 0) for x in params.replace("\n", " ").split(",") if x.strip()]
            rows.append((cmd, cnt, ps))
        out[m.group(1)] = rows
    return out


def encode(rows):
    b = b""
    for cmd, cnt, ps in rows:
        if not isinstance(cmd, int):
            return None
        b += struct.pack("<IB", cmd, cnt) + bytes(ps[:64]).ljust(64, b"\0") + b"\0\0\0"
    return b


def find(image, rows, probe=6):
    """Find where the first `probe` records occur; return offset list."""
    blob = image
    hits = []
    head = encode(rows[:probe])
    i = blob.find(head)
    while i != -1:
        hits.append(i)
        i = blob.find(head, i + 1)
    return hits


def compare(image, off, rows):
    """Compare rows against image starting at off; return (#equal, first_diff_index)."""
    eq = 0
    for k, (cmd, cnt, ps) in enumerate(rows):
        rec = image[off + k * REC: off + (k + 1) * REC]
        c, n = struct.unpack_from("<IB", rec)
        if c != cmd or n != cnt or rec[5:5 + len(ps)] != bytes(ps):
            return eq, k
        eq += 1
    return eq, None


if __name__ == "__main__":
    img = open(sys.argv[1], "rb").read()
    for path in sys.argv[2:]:
        t = tables(open(path, encoding="latin-1").read())
        for name, rows in t.items():
            hits = find(img, rows)
            if not hits:
                print(f"{path} :: {name} ({len(rows)} rows): not found")
                continue
            for h in hits:
                eq, d = compare(img, h, rows)
                print(f"{path} :: {name} ({len(rows)} rows) @0x{h:x}: {eq}/{len(rows)} rows identical"
                      + ("" if d is None else f", first difference at row {d}: {rows[d][0]:#x}"))
