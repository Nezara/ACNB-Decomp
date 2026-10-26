"""Generate the source-port preludes of the decompiled code (Stage 0: the source-port half of the development version's
gen_bridge.py; the hybrid bridge it also wrote is not in Stage 0).

    python gen_sp.py      (run by sourceport/build.ps1 before compiling)

Scans port/<rel>.cpp (port/src/**, port/lib/**; not port/src/iop) for definitions of DWARF functions of that source
file (<rel> as in gen_headers.src_rel) and writes
  port/gen/sp/<rel>.h    the prelude force-included before port/<rel>.cpp (ACNB_SOURCEPORT): porting layer and local
                         header; static functions not decompiled yet are renamed <name>__missing__<rel> so the link
                         lists them; overlay files get their linker group (port/platform/ovlreg)
  port/gen/sp_natives.txt  "native ADDR OVERLAY rel:name" per natively defined DWARF function (sp_missing.py's
                         Versus progress report)
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import protofix  # noqa: E402
from gen_headers import src_rel, mangle, ROOT, SYM  # noqa: E402

PORT = os.path.join(ROOT, "port")


_DECL = re.compile(r'^extern (.+?);\s*/\* (0x[0-9a-f]+)')
_main_decl_cache = {}


def main_data_decls(local_h):
    """[(header, C name, declaration text without extern)] of the writable data declared at main-ELF addresses
    (< the overlay area 0x1df5f00) in globals.h, nodwarf.h and the file's local header (source-port overlay groups)"""
    out = []
    for h in (os.path.join(PORT, "include", "acnb", "globals.h"), os.path.join(PORT, "include", "acnb", "nodwarf.h"),
              local_h):
        if h not in _main_decl_cache:
            L = []
            if os.path.exists(h):
                for line in open(h, encoding="utf-8"):
                    m = _DECL.match(line.strip())
                    if not m or int(m.group(2), 16) >= 0x1df5f00 or re.search(r"\bconst\b", m.group(1)):
                        continue
                    t = m.group(1)
                    al = t.endswith(" PS2_GALIGN")   # source-port 16-byte alignment (gen_headers)
                    if al:
                        t = t[:-len(" PS2_GALIGN")]
                    bare = re.sub(r'\s*__asm__\("[^"]*"\)', "", t)
                    f = re.search(r"\(\s*\*\s*(?:const\s+)?(\w+)", bare) or re.search(r"(\w+)\s*(?:\[[^\]]*\]\s*)*$", bare)
                    L.append((os.path.basename(h)[:-2], f.group(1), t, al))
            _main_decl_cache[h] = L
        out += _main_decl_cache[h]
    return out


def write_if_changed(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path) and open(path).read() == text:
        return
    open(path, "w").write(text)


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", lambda m: " " * len(m.group(0)), s, flags=re.S)
    return re.sub(r"//[^\n]*", "", s)


def defined_functions(text):
    """Names of functions defined at file scope (identifier before the '(...)' that precedes a depth-0 '{')."""
    s = strip_comments(text)
    s = re.sub(r'"(\\.|[^"\\])*"', '""', s)
    out, depth = set(), 0
    for i, ch in enumerate(s):
        if ch == "{":
            if depth == 0:
                j = i - 1
                while j >= 0 and s[j].isspace():
                    j -= 1
                if j >= 0 and s[j] == ")":
                    lvl = 0
                    while j >= 0:
                        if s[j] == ")":
                            lvl += 1
                        elif s[j] == "(":
                            lvl -= 1
                            if lvl == 0:
                                break
                        j -= 1
                    m = re.search(r"(\w+)\s*$", s[:j])
                    if m:
                        out.add(m.group(1))
            depth += 1
        elif ch == "}":
            depth -= 1
    return out


def main():
    fs = protofix.load_functions(SYM)
    by_rel = {}
    for f in fs:
        f["rel"] = src_rel(f["src"])
        by_rel.setdefault(f["rel"], []).append(f)

    # decompiled source files and the DWARF functions they define
    native = {}  # rel -> set of names
    src_ext = {}  # rel -> .c / .cpp
    for dp, _, files in [w for top in ("src", "lib") for w in os.walk(os.path.join(PORT, top))]:
        if os.path.relpath(dp, PORT).replace("\\", "/").startswith("src/iop"):
            continue   # native IOP modules (NI5b): not EE game code, no prelude
        for fn in files:
            if not fn.endswith((".c", ".cpp")):
                continue
            path = os.path.join(dp, fn)
            rel, ext = os.path.splitext(os.path.relpath(path, PORT).replace("\\", "/"))
            names = {f["name"] for f in by_rel.get(rel, [])}
            native[rel] = defined_functions(open(path, encoding="utf-8").read()) & names
            src_ext[rel] = ext

    report = []
    for rel in sorted(native):
        defs = native[rel]
        fns = by_rel.get(rel, [])
        local_h = os.path.join(PORT, "include", "acnb", "local", rel + ".h")
        local_inc = f'#include "acnb/local/{rel}.h"' if os.path.exists(local_h) else '#include "acnb/funcs.h"'
        S = ["/* Generated by scripts/gen_bridge.py: source-port prelude force-included before port/%s%s. */"
             % (rel, src_ext[rel]), "#pragma once", '#include "acnb/port.h"']
        stat = {}
        for f in sorted(fns, key=lambda f: f["addr"]):
            if f["static"]:
                stat.setdefault(f["name"], []).append(f)
        for n, insts in stat.items():
            if n not in defs:
                S.append(f"#define {n} {n}__missing__{mangle(rel)}")
        # overlay files: their globals (incl. file / function statics) go into the overlay's linker group, which
        # port/platform/ovlreg snapshots at start-up and restores per Overlay_Load (phase 3 S6) - except the file's
        # globals at main addresses (MW small data: .sdata / .sbss sit in the main ELF and keep their values across
        # overlay loads), declared into plain .data first (before the headers: an attribute on a redeclaration warns)
        ovls = {f["overlay"] for f in fns}
        ovl = ovls.pop() if len(ovls) == 1 and "main" not in ovls else None
        if ovl:
            words = set(re.findall(r"\b\w+\b", open(os.path.join(PORT, rel + src_ext[rel]), encoding="utf-8").read()))
            keep = [(t, al) for _, n, t, al in main_data_decls(local_h) if n in words]
            if keep:
                S += ['#include "acnb/types.h"', "#ifdef __cplusplus", 'extern "C" {', "#endif"]
                S += [f'extern {t} __attribute__((section(".data"){", aligned(16)" if al else ""}));' for t, al in keep]
                S += ["#ifdef __cplusplus", "}", "#endif"]
        S += [local_inc, '#include "acnb/globals.h"']
        if ovl:
            S.append(f'#pragma clang section data=".data$ovl_{ovl}_m" bss=".bss$ovl_{ovl}_m"')
        write_if_changed(os.path.join(PORT, "gen", "sp", rel + ".h"), "\n".join(S) + "\n")
        seen = set()
        for f in sorted(fns, key=lambda f: f["addr"]):
            if f["name"] in defs and f["name"] not in seen:
                seen.add(f["name"])
                report.append(f"native {f['addr']:#010x} {f['overlay']:8} {rel}:{f['name']}")
    write_if_changed(os.path.join(PORT, "gen", "sp_natives.txt"), "\n".join(report) + "\n")
    print("gen_sp: %d native functions in %d files" % (len(report), len(native)))


if __name__ == "__main__":
    main()
