"""Minimal FDT parser -> ordered tree, for comparing device trees without dtc."""
import struct


class Node:
    def __init__(self, name, parent=None):
        self.name = name
        self.parent = parent
        self.props = {}      # name -> bytes, insertion ordered
        self.children = {}   # name -> Node, insertion ordered

    @property
    def path(self):
        if self.parent is None:
            return "/"
        pp = self.parent.path
        return (pp if pp != "/" else "") + "/" + self.name

    def walk(self):
        yield self
        for c in self.children.values():
            yield from c.walk()

    def get(self, path):
        n = self
        for part in [p for p in path.split("/") if p]:
            n = n.children.get(part)
            if n is None:
                return None
        return n


def _strz(b, o):
    return b[o:b.index(b"\0", o)].decode(errors="replace")


def parse(blob, off=0):
    magic, total, ost, ostr = struct.unpack_from(">4I", blob, off)
    assert magic == 0xD00DFEED, hex(magic)
    b = blob[off:off + total]
    p = ost
    root = None
    cur = None
    while True:
        tok = struct.unpack_from(">I", b, p)[0]
        p += 4
        if tok == 1:
            name = _strz(b, p)
            p += (len(name.encode()) + 4) // 4 * 4
            n = Node(name, cur)
            if cur is None:
                root = n
            else:
                cur.children[name] = n
            cur = n
        elif tok == 2:
            cur = cur.parent
        elif tok == 3:
            ln, no = struct.unpack_from(">II", b, p)
            p += 8
            cur.props[_strz(b, ostr + no)] = b[p:p + ln]
            p += (ln + 3) // 4 * 4
        elif tok == 4:
            continue
        elif tok == 9:
            break
        else:
            raise ValueError(f"bad token {tok} at {p}")
    return root


def load(path, off=0):
    return parse(open(path, "rb").read(), off)


def phandles(root):
    out = {}
    for n in root.walk():
        for k in ("phandle", "linux,phandle"):
            if k in n.props and len(n.props[k]) == 4:
                out[struct.unpack(">I", n.props[k])[0]] = n
    return out


def symbols(root):
    """label -> path from __symbols__."""
    s = root.children.get("__symbols__")
    if not s:
        return {}
    return {k: v.rstrip(b"\0").decode() for k, v in s.props.items()}


def is_string(v):
    if not v or v[-1] != 0 or v[0] == 0 or b"\0\0" in v:
        return False
    return all(32 <= c < 127 or c == 0 for c in v[:-1])


def cells(v):
    return list(struct.unpack(">%dI" % (len(v) // 4), v)) if len(v) % 4 == 0 else None
