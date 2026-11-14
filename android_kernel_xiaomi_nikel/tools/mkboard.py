"""Generate a nikel board DTS (overrides on top of SHIFT's mt6797 SoC tree).

Inputs : SHIFT5me base DTB (SoC, with __symbols__), nikel stock merged DTB.
Output : DTS text containing only what nikel's board needs on top of SHIFT's
         SoC description, with phandles rewritten as labels / path refs.
"""
import re
import struct
import sys

import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fdtlib as F  # noqa: E402

SKIP_PROPS = {"phandle", "linux,phandle"}
SKIP_PATHS = re.compile(r"^/(chosen|memory(@[0-9a-f]+)?|__symbols__|__fixups__|__local_fixups__)(/|$)")

# props whose every cell is a phandle
PH_LIST = re.compile(r"^(interrupt-parent|pinctrl-\d+|.*-supply|cpu-idle-states|cpu|mediatek,pctl-regmap)$")
# props that are <phandle specifier...> lists; specifier length from provider
SPEC = {
    "clocks": "#clock-cells",
    "power-domains": "#power-domain-cells",
}
# SoC / kernel-version properties: SHIFT's kernel reads these, so SHIFT wins.
KEEP_SHIFT = {
    ("/soc/gpufreq", "clocks"),                          # clock IDs of SHIFT's clk driver
    ("/soc/mt_soc_dl1_pcm@11220000", "compatible"),      # driver name in SHIFT's kernel
}
# Nodes where a property missing on nikel means "nikel must not have it".
DELETE_OK = {"/soc/bat_metter"}  # Pump Express fast-charge: nikel's charger has no PE+

GPIO_PROP = re.compile(r"^(.*-gpios?|gpios|lcm_[a-z_]*_gpio)$")


class Ctx:
    def __init__(self, root):
        self.root = root
        self.ph = F.phandles(root)


def provider_cells(ctx, ph, cellprop):
    n = ctx.ph.get(ph)
    if n is None or cellprop not in n.props:
        return None
    return F.cells(n.props[cellprop])[0]


def normalize(ctx, node, name, val):
    """Return list of tokens: ints, ('ref', path), or the raw bytes/str."""
    if F.is_string(val):
        return ("str", val)
    c = F.cells(val)
    if c is None or not c:
        return ("bytes", val)
    if PH_LIST.match(name):
        if all(x in ctx.ph for x in c):
            return ("cells", [("ref", ctx.ph[x].path) for x in c])
        return ("cells", c)
    cellprop = SPEC.get(name)
    if cellprop is None and GPIO_PROP.match(name):
        cellprop = "#gpio-cells"
    if cellprop:
        out, i = [], 0
        while i < len(c):
            ph = c[i]
            n = provider_cells(ctx, ph, cellprop)
            if n is None:  # not a provider: give up, keep raw
                return ("cells", c)
            out.append(("ref", ctx.ph[ph].path))
            out.extend(c[i + 1:i + 1 + n])
            i += 1 + n
        return ("cells", out)
    return ("cells", c)


def main(shift_dtb, nikel_dtb, out_path):
    S = F.load(shift_dtb)
    N = F.load(nikel_dtb)
    cs, cn = Ctx(S), Ctx(N)
    sym = F.symbols(S)
    path2lab = {}
    for lab, p in sorted(sym.items()):
        path2lab.setdefault(p, lab)

    # ---- collect diff ---------------------------------------------------
    emit = {}       # path -> {"props": [(name, norm)], "del": [names], "new": bool}
    refs_needed = set()

    def want(path):
        return emit.setdefault(path, {"props": [], "del": [], "new": False})

    for n in N.walk():
        if n.parent is None or SKIP_PATHS.match(n.path):
            continue
        s = S.get(n.path)
        if s is None:
            if n.parent.path == "/" or S.get(n.parent.path) is not None:
                e = want(n.path)
                e["new"] = True
            for k, v in n.props.items():
                if k in SKIP_PROPS:
                    continue
                want(n.path)["props"].append((k, normalize(cn, n, k, v)))
            continue
        for k, v in n.props.items():
            if k in SKIP_PROPS or (n.path, k) in KEEP_SHIFT:
                continue
            nv = normalize(cn, n, k, v)
            if k not in s.props or normalize(cs, s, k, s.props[k]) != nv:
                want(n.path)["props"].append((k, nv))
        # Only delete SHIFT props where nikel's absence is a board fact.
        if n.path in DELETE_OK:
            for k in s.props:
                if k not in n.props and k not in SKIP_PROPS:
                    want(n.path)["del"].append(k)

    for e in emit.values():
        for _, nv in e["props"]:
            if nv[0] == "cells":
                for t in nv[1]:
                    if isinstance(t, tuple):
                        refs_needed.add(t[1])

    # labels for referenced nodes that SHIFT has no label for
    gen_labels = {}
    used = set(sym)
    for p in sorted(refs_needed):
        if p in path2lab:
            continue
        base = re.sub(r"[^A-Za-z0-9_]", "_", p.rstrip("/").split("/")[-1]).strip("_") or "node"
        if base[0].isdigit():
            base = "n_" + base
        lab, i = "nk_" + base, 2
        while lab in used:
            lab = f"nk_{base}_{i}"
            i += 1
        used.add(lab)
        gen_labels[p] = lab

    def ref(path):
        if path in path2lab:
            return "&" + path2lab[path]
        return "&" + gen_labels[path]

    def fmt(name, nv):
        kind, v = nv
        if kind == "str":
            parts = v[:-1].split(b"\0")
            return f'{name} = ' + ", ".join('"%s"' % p.decode().replace('"', '\\"') for p in parts) + ";"
        if kind == "bytes":
            return f"{name} = [" + " ".join("%02x" % b for b in v) + "];" if v else f"{name};"
        toks = [ref(t[1]) if isinstance(t, tuple) else "0x%x" % t for t in v]
        # wrap long cell arrays
        lines, cur = [], []
        for t in toks:
            cur.append(t)
            if len(cur) == 12:
                lines.append(" ".join(cur))
                cur = []
        if cur:
            lines.append(" ".join(cur))
        if len(lines) <= 1:
            return f"{name} = <{lines[0] if lines else ''}>;"
        return f"{name} = <" + ("\n" + "\t" * 0).join([lines[0]] + ["    " + l for l in lines[1:]]) + ">;"

    # ---- build nested output -------------------------------------------
    tree = {}
    for path in emit:
        parts = [p for p in path.split("/") if p]
        t = tree
        for p in parts:
            t = t.setdefault(p, {})

    out = []

    def w(depth, s):
        for i, line in enumerate(s.split("\n")):
            out.append("\t" * depth + ("\t" if i else "") + line)

    def render(path, sub, depth):
        name = path.rstrip("/").split("/")[-1] if path != "/" else "/"
        e = emit.get(path)
        lab = gen_labels.get(path)
        head = (f"{lab}: " if lab else "") + name + " {"
        w(depth, head)
        if e:
            for k in e["del"]:
                w(depth + 1, f"/delete-property/ {k};")
            for k, nv in e["props"]:
                w(depth + 1, fmt(k, nv))
        for child, csub in sub.items():
            render((path if path != "/" else "") + "/" + child, csub, depth + 1)
        w(depth, "};")

    render("/", tree, 0)
    stats = dict(nodes=len(emit), new=sum(1 for e in emit.values() if e["new"]),
                 props=sum(len(e["props"]) for e in emit.values()),
                 deleted=sum(len(e["del"]) for e in emit.values()),
                 gen_labels=len(gen_labels), shift_labels_used=len(refs_needed) - len(gen_labels))
    open(out_path, "w").write("\n".join(out) + "\n")
    return stats, emit


if __name__ == "__main__":
    st, _ = main(*sys.argv[1:4])
    print(st)
