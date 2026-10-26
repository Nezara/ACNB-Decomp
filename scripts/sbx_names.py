"""Sandbox name + type tables: what the --script engine (port/platform/sandbox) uses to
turn names in a script into addresses in the build it runs in.

    python scripts/sbx_names.py hybrid OUTDIR              (port\\build.ps1: PS2 addresses only)
    python scripts/sbx_names.py sourceport OUTDIR MAPFILE  (sourceport\\build.ps1 post-link: + RVAs from acnb.map)
    python scripts/sbx_names.py lint SCRIPT..              (name lint of scripts without a game run, hybrid rules)

OUTDIR/sbx_names.tsv, one row per (name, PS2 address, file):
    name  file  ps2  ovl  kind  size  type  sp
  file = original source path below ac/ (src/menu/msys.c) or '-'; ps2 = hex; ovl = main / OLMENU / OLSORTIE / -;
  kind f (function) / d (data); size hex; type = encoded DWARF type of a global (below) or '-'; sp = the source port's
  RVA (hex, address = module base + RVA) or '-' (not in that build: not native yet, or no match in the map).
  Row order = the hybrid's old lookup order (symbols/symbol_addrs.txt in file order, then port/gen/symtab.tsv's
  sources: functions.json + SDK functions, by address), so a bare name means what it meant to runtime/script.c.
  Line 1: "#sbx-names 1 BUILD rows=N sentinels=NAME:ADDR,.." - the engine compares each sentinel with the real
  address in the running build (hybrid: function table; source port: symbols the exe references itself) and refuses
  the script on a mismatch (a stale table after a rebuild).
OUTDIR/sbx_types.tsv (symbols/types.json): "S name size" (struct / union), "E name size" (enum), then per member
  "M name offset type bits" (bits = '-' or BITSIZE:BITOFFSET).
Type encoding: s8 u8 s16 u16 s32 u32 s64 u64 f32 f64 u128 v (void) | p (pointer, 4 bytes) | f (function) |
  s:NAME (struct / union) | e:NAME (enum) | aN:T (array of N T; [4][2] = a4:a2:T) | xN (other fundamental, N bytes).
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SYM = os.path.join(ROOT, "symbols")
OVL_BASE = 0x1df5f00
FUND = {"char": "s8", "signed char": "s8", "unsigned char": "u8", "short": "s16", "unsigned short": "u16",
        "int": "s32", "unsigned int": "u32", "long": "s32", "unsigned long": "u32", "long long": "s64",
        "unsigned long long": "u64", "float": "f32", "double": "f64", "u_long128": "u128", "void": "v"}
SENT_SP = ("g_gamedata", "MenuObjTopp", "sp_exit")   # host_sbx.cpp takes these addresses itself


def enc(t):
    k = t[0]
    if k == "fund":
        return FUND.get(t[1], "x4")
    if k == "ptr":
        return "p"
    if k == "func":
        return "f"
    if k in ("struct", "union"):
        return "s:" + t[1]
    if k == "enum":
        return "e:" + t[1]
    if k == "array":
        inner = enc(t[2])
        for n in reversed(t[1]):
            inner = "a%d:%s" % (n, inner)
        return inner
    return "x4"


def rel_src(s):
    s = (s or "").replace("\\", "/")
    for cut in ("/ac/", "/ext/"):
        i = s.find(cut)
        if i >= 0:
            return s[i + len(cut):]
    return s.rsplit("/", 1)[-1] if s else "-"


def rows_table():
    """[(name, file, ps2, ovl, kind, size, type)] in the hybrid's lookup order"""
    funcs = json.load(open(os.path.join(SYM, "functions.json")))
    globs = json.load(open(os.path.join(SYM, "globals.json")))
    byna = {}
    for f in funcs:
        byna.setdefault((f["name"], f["addr"]), []).append(("f", rel_src(f.get("src")), f.get("overlay") or "main",
                                                             f.get("size") or 0, "-"))
    for g in globs:
        if g.get("scope") or not g.get("addr"):
            continue   # function-scope statics: no script name
        byna.setdefault((g["name"], g["addr"]), []).append(("d", rel_src(g.get("src")), g.get("overlay") or "main",
                                                             0, enc(g["type"])))
    out, seen = [], set()

    def add(name, addr, kind, size):
        cands = byna.get((name, addr)) or [(kind, "-", "main" if addr < OVL_BASE else "-", size, "-")]
        for k, src, ovl, sz, ty in cands:
            key = (name, addr, src)
            if key in seen:
                continue
            seen.add(key)
            out.append((name, src, addr, ovl, k, sz or size, ty))

    pat = re.compile(r"(\S+) = 0x([0-9A-Fa-f]+);(?: // type:(\w+))?(?: size:0x([0-9A-Fa-f]+))?")
    for line in open(os.path.join(SYM, "symbol_addrs.txt")):
        m = pat.match(line)
        if m:
            add(m.group(1), int(m.group(2), 16), "f" if m.group(3) == "func" else "d", int(m.group(4) or "0", 16))
    # port/gen/symtab.tsv's sources (gen_symtab.py), in its order (address, overlay)
    st = {}
    ovn = {"main": 0, "OLSORTIE": 1, "OLMENU": 2}
    for f in funcs:
        st[(f["addr"], ovn[f["overlay"]])] = (f["size"], f["name"])
    sp = re.compile(r"(\S+) = 0x([0-9A-Fa-f]+); // type:func size:0x([0-9A-Fa-f]+)")
    for line in open(os.path.join(SYM, "sdk_symbol_addrs.txt")):
        m = sp.match(line)
        if m:
            st.setdefault((int(m.group(2), 16), 0), (int(m.group(3), 16), m.group(1)))
    for (addr, _ov), (size, name) in sorted(st.items()):
        add(name, addr, "f", size)
    out += nodwarf_rows({r[0] for r in out})
    return out


NW_DECL = re.compile(r"extern\s+(?:const\s+)?(?:struct\s+)?(\w+(?:\s+\w+)*?)\s*(\*?)\s*(\w+)((?:\[\d+\])*)\s*;")


def nodwarf_rows(have):
    """s103: the no-DWARF globals of platform/names/nodwarf_abs.s (Stage 0; dev: runtime/nodwarf_abs.s) (PS2 address) as data rows, typed from
    include/acnb/nodwarf.h where the declaration is simple; names already in the table are skipped (no new clashes).
    Appended after every other row, so lookup order and the hybrid sentinels stay as they were."""
    port = os.path.join(ROOT, "port")
    types = {}
    for line in open(os.path.join(port, "include", "acnb", "nodwarf.h"), encoding="utf-8", errors="replace"):
        m = NW_DECL.match(line.strip())
        if not m:
            continue
        base, star, name, dims = m.groups()
        t = "p" if star else FUND.get(base) or ("s:" + base if not base.startswith(("unsigned", "signed")) else None)
        if not t:
            continue
        for n in reversed([int(x) for x in re.findall(r"\[(\d+)\]", dims)]):
            t = "a%d:%s" % (n, t)
        types[name] = t
    out = []
    for line in open(os.path.join(port, "platform", "names", "nodwarf_abs.s"), encoding="utf-8", errors="replace"):
        m = re.match(r"\s*\.set\s+_(\w+)\s*,\s*0x([0-9A-Fa-f]+)", line)
        if not m or m.group(1) in have:
            continue
        name, addr = m.group(1), int(m.group(2), 16)
        have.add(name)
        out.append((name, "nodwarf", addr, "main" if addr < OVL_BASE else "-", "d", 0, types.get(name, "-")))
    return out


MAPSYM = re.compile(r'^([0-9a-f]{8}) ([0-9a-f]{8}) +(\d+) (\s*)(.*)$')


def load_map(path):
    """text: {(bare name, obj stem): rva}, ctext: {C name: rva}, data: {name: rva}, objs: set of stems"""
    text, ctext, data, objs, cur = {}, {}, {}, set(), None
    for line in open(path, encoding="utf-8", errors="replace"):
        m = MAPSYM.match(line.rstrip("\n"))
        if not m:
            continue
        addr, ind, rest = int(m.group(1), 16), len(m.group(4)), m.group(5)
        if ind < 4:
            cur = None
            continue
        mi = re.match(r'(.*):\((.+)\)$', rest)
        if mi:
            stem = os.path.basename(mi.group(1))
            stem = stem[:-2] if stem.endswith(".o") else stem
            cur = (stem, mi.group(2))
            objs.add(stem)
            continue
        if not cur:
            continue
        stem, sec = cur
        if sec.startswith(".text"):
            if "(" in rest:
                bare = rest.split("(", 1)[0]
                if "::" not in bare and " " not in bare:
                    text.setdefault((bare, stem), addr)
            elif rest.startswith("_"):
                ctext.setdefault(rest[1:].split("@")[0], addr)
        elif sec.startswith((".data", ".rdata", ".bss")) and rest.startswith("_") and "(" not in rest:
            data.setdefault(rest[1:].split("@")[0], addr)
    return text, ctext, data, objs


def stem_for(src, objs, cache):
    """obj stem of an original source path: the map object whose name ends with the file's base name, most matching
    directory components first (src/menu/mop.c -> src_menu_mop; lib/fs/lib/x.c -> lib_fs_x)"""
    if src in cache:
        return cache[src]
    parts = src[:src.rfind(".")].split("/") if "." in src else src.split("/")
    base, best, bs = parts[-1], None, -1
    for o in objs:
        if o != base and not o.endswith("_" + base):
            continue
        pre = o[:len(o) - len(base)].rstrip("_").split("_") if o != base else []
        score = sum(1 for p in parts[:-1] if p in pre)
        if score > bs:
            best, bs = o, score
    cache[src] = best
    return best


def sp_rva(row, mp, cache):
    name, src, addr, ovl, kind, size, ty = row
    text, ctext, data, objs = mp
    stem = stem_for(src, objs, cache) if src != "-" else None
    if kind == "f":
        if stem and (name, stem) in text:
            return text[(name, stem)]
        if src == "-":   # no source file known (SDK, no DWARF): a C name, else one C++ function of that name
            hits = {a for (n, o), a in text.items() if n == name}
            if len(hits) == 1:
                return hits.pop()
        r = ctext.get(name)
        if r is None and src != "-":   # not decompiled yet: the link stub NAME__missing__<src path> stands in (s103 ST4:
            ms = name + "__missing__" + re.sub(r"[^0-9A-Za-z]", "_", src[:src.rfind(".")])   # a stored fn pointer
            r = next((a for (n, o), a in text.items() if n == ms), None)   # names the function in both builds)
        return r
    if src != "-" and not stem:   # file not in the source port's objects: statics are named after the source path
        stem = re.sub(r"[^0-9A-Za-z]", "_", src[:src.rfind(".")])
    if stem and name + "__S__" + stem in data:
        return data[name + "__S__" + stem]
    if name in data:
        return data[name]
    return mp_fstatic(mp).get(name)   # function-local static FUNC__VAR = the source port's VAR__F__FUNC__STEM (s103)


FSTATIC = {}


def mp_fstatic(mp):
    """{FUNC__VAR: rva} from the map's VAR__F__FUNC__src_.. / lib_.. data names (cached on the map tuple's dict)"""
    data = mp[2]
    if "fstatic" not in FSTATIC:
        fs = {}
        for n, a in list(data.items()):
            i = n.find("__F__")
            if i <= 0:
                continue
            var, rest = n[:i], n[i + 5:]
            j = max(rest.rfind("__src_"), rest.rfind("__lib_"))
            if j > 0:
                fs.setdefault(rest[:j] + "__" + var, a)
        FSTATIC["fstatic"] = fs
    return FSTATIC["fstatic"]


def write_types(outdir):
    t = json.load(open(os.path.join(SYM, "types.json")))
    with open(os.path.join(outdir, "sbx_types.tsv"), "w", newline="\n") as fh:
        for name, v in t.items():
            if v["kind"] == "enum":
                fh.write("E\t%s\t%d\n" % (name, v.get("size") or 4))
                continue
            fh.write("S\t%s\t%d\n" % (name, v.get("size") or 0))
            for m in v.get("members", []):
                bits = "%d:%d" % (m["bit_size"], m["bit_offset"]) if "bit_size" in m else "-"
                fh.write("M\t%s\t%d\t%s\t%s\n" % (m["name"], m["offset"], enc(m["type"]), bits))


def write_names(build, outdir, mapfile):
    rows = rows_table()
    mp = load_map(mapfile) if mapfile else None
    cache = {}
    lines, sp = [], {}
    for r in rows:
        rva = sp_rva(r, mp, cache) if mp else None
        sp[r[0]] = sp.get(r[0]) or rva
        lines.append("%s\t%s\t%x\t%s\t%s\t%x\t%s\t%s\n" % (r[0], r[1], r[2], r[3], r[4], r[5], r[6],
                                                          "%x" % rva if rva is not None else "-"))
    if build == "sourceport":
        text, ctext, data, objs = mp
        sent = []
        for n in SENT_SP:
            a = data.get(n, ctext.get(n))
            if a is None:
                sys.exit("sbx_names: sentinel %s not in %s" % (n, mapfile))
            sent.append("%s:%x" % (n, a))
    else:   # hybrid: three function rows spread over the table, checked against the runtime's function table
        fr = [r for r in rows if r[4] == "f" and sum(1 for q in rows if q[0] == r[0]) == 1]
        sent = ["%s:%x" % (r[0], r[2]) for r in (fr[0], fr[len(fr) // 2], fr[-1])]
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "sbx_names.tsv"), "w", newline="\n") as fh:
        fh.write("#sbx-names 1 %s rows=%d sentinels=%s\n" % (build, len(lines), ",".join(sent)))
        fh.writelines(lines)
    write_types(outdir)
    n_sp = sum(1 for l in lines if not l.endswith("\t-\n"))
    print("sbx_names: %d rows%s -> %s" % (len(lines), " (%d in the source port)" % n_sp if mp else "",
                                           os.path.join(outdir, "sbx_names.tsv")))


# ---------------------------------------------------------------------------------------------------- lint
KW_FN = {"u8", "u16", "u32", "s8", "s16", "s32", "f32", "obj", "scene", "vb", "ovl"}
EXPR_CMDS = {"until", "repeat", "assert", "def", "log", "watch", "dump"}


def expand(path, depth=0):
    """the script's lines with `include NAME` inlined (engine rules: NAME.script / routes/NAME.script next to the file,
    then port/autoreplay/routes); a missing include stays as a "#!include NAME" line (a lint problem)"""
    out, d = [], os.path.dirname(path)
    for line in open(path).read().splitlines():
        w = line.split("#", 1)[0].split()
        if len(w) == 2 and w[0] == "include" and depth < 4:
            for c in (os.path.join(d, w[1] + ".script"), os.path.join(d, "routes", w[1] + ".script"),
                      os.path.join(ROOT, "port", "autoreplay", "routes", w[1] + ".script")):
                if os.path.exists(c):
                    out += expand(c, depth + 1)
                    break
            else:
                out.append("#!include " + w[1])
            continue
        out.append(line)
    return out


def lint(paths):
    """bare names the scripts use that the table resolves to more than one object (= a load error in the engine)"""
    rows = rows_table()
    ids, addrs = {}, set()
    for r in rows:
        ids.setdefault(r[0], set()).add((r[2], r[1]))
        addrs.add(r[2])
    bad = 0
    tok = re.compile(r"@?0x[0-9a-fA-F]+|\d+|[A-Za-z_][\w/]*\.c(?:pp)?:\w+|[A-Za-z_]\w*")
    for p in paths:
        macros = set()
        lines = expand(p)
        for ln, line in enumerate(lines, 1):
            s = line.split("#", 1)[0].strip()
            w = s.split(None, 1)
            if w and w[0] == "def" and len(w) > 1:
                macros.add(w[1].split()[0])
        for ln, line in enumerate(lines, 1):
            if line.startswith("#!include "):
                print("%s:%d: include %s: no such route" % (p, ln, line[10:])); bad += 1
                continue
            s = line.split("#", 1)[0].strip()
            w = s.split(None, 1)
            if not w or w[0] not in EXPR_CMDS or len(w) < 2:
                continue
            e = w[1]
            if w[0] == "def":
                e = e.split(None, 1)[1] if len(e.split(None, 1)) > 1 else ""
            if w[0] == "log":
                e = e.split("|", 1)[1] if "|" in e else ""
            if w[0] == "repeat":
                e = e.split(" until ", 1)[1] if " until " in e else ""
            e = re.sub(r"\s(timeout|max)\s+\d+\s*$", "", e)
            for m in tok.finditer(e):
                t = m.group(0)
                if m.start() > 0 and e[m.start() - 1] == ".":
                    continue   # field-path member (SYM[i].a.b): resolved by the engine's load lint, not here
                if t[0].isdigit():
                    if int(t, 0) >= 0x100000:
                        print("%s:%d: raw address literal %s" % (p, ln, t)); bad += 1
                    continue
                if t[0] == "@":
                    if int(t[1:], 0) not in addrs:
                        print("%s:%d: %s is no symbol's address" % (p, ln, t)); bad += 1
                    continue
                if ".c" in t:
                    fn, nm = t.rsplit(":", 1)
                    hit = [f for a, f in ids.get(nm, ()) if f == fn or f.endswith("/" + fn)]
                    if len(hit) != 1:
                        print("%s:%d: %s: %d matches" % (p, ln, t, len(hit))); bad += 1
                    continue
                if t in KW_FN or t in macros:
                    continue
                c = ids.get(t)
                if not c:
                    print("%s:%d: unknown name %s" % (p, ln, t)); bad += 1
                elif len({a for a, f in c}) > 1 or len(c) > 1:
                    print("%s:%d: name clash %s: %s" % (p, ln, t, ", ".join("%s@%x" % (f, a) for a, f in sorted(c))))
                    bad += 1
    print("sbx_names lint: %d problem(s) in %d file(s)" % (bad, len(paths)))
    return 1 if bad else 0


def main():
    a = sys.argv[1:]
    if a and a[0] == "lint":
        sys.exit(lint(a[1:]))
    if len(a) < 2 or a[0] not in ("hybrid", "sourceport") or (a[0] == "sourceport" and len(a) < 3):
        sys.exit(__doc__)
    write_names(a[0], a[1], a[2] if a[0] == "sourceport" else None)


if __name__ == "__main__":
    main()
