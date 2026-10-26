# Logical state hash compare:
#   stcmp.py A.st.txt B.st.txt [N]        counts, first differing vb, the areas that differ there and at the next N
#                                         differing vbs (default 6); "full only" when the core total matches (then
#                                         also "first core difference: vb V" - statecheck.ps1 drills at both)
#   stcmp.py --dump A_dir B_dir [N] [VB]  diff two ACNB_STATE_DUMP files (state_<VB>.txt; VB default = the first
#                                         common one that differs): first N differing fields (default 20), floats with
#                                         their ulp distance
# Line: state vb V fr F ov O: TOTAL area H|- .. full H|-   (areas keyed by name, so a field file change shows as
# "area only in A / B"). Exit 0 = identical over the common vbs, 1 = a difference, 2 = usage / no lines.
import os
import re
import sys


def load(p):
    r = {}
    for l in open(p, errors="replace"):
        i = l.find("state vb ")
        if i < 0:
            continue
        head, _, tail = l[i:].strip().partition(": ")
        f = head.split()
        t = tail.split()
        if len(f) < 7 or not t:
            continue
        areas = dict(zip(t[1::2], t[2::2]))
        r[int(f[2])] = (f[4], f[6], t[0], areas)
    return r


def cmp_lines(a_path, b_path, n):
    a, b = load(a_path), load(b_path)
    common = sorted(set(a) & set(b))
    print(f"vblanks {len(a)} / {len(b)}, common {len(common)}" + (f" ({common[0]}..{common[-1]})" if common else ""))
    if not common:
        return 2
    diff = []
    for v in common:
        (fa, oa, ta, aa), (fb, ob, tb, ab) = a[v], b[v]
        names = list(dict.fromkeys(list(aa) + list(ab)))
        ds = [k for k in names if aa.get(k) != ab.get(k)]
        core = ta != tb or fa != fb or oa != ob
        if core or ds:
            diff.append((v, core, ds, fa, fb, oa, ob))
    print(f"differ {len(diff)}" + ("" if diff else " - identical"))
    for v, core, ds, fa, fb, oa, ob in diff[: n + 1]:
        tag = "" if core else " (full only: internal drift not yet in the core areas)" if ds == ["full"] else " (total equal)"
        extra = f" fr {fa}|{fb}" if fa != fb else ""
        extra += f" ov {oa}|{ob}" if oa != ob else ""
        def side(k):
            x, y = a[v][3].get(k, "absent"), b[v][3].get(k, "absent")
            return f"{k} {x}|{y}"
        print(f"  vb {v}:{extra} " + ", ".join(side(k) for k in ds) + tag)
    if diff:
        first = diff[0]
        print(f"first difference: vb {first[0]}, areas {' '.join(first[2]) or '(total / fr / ov only)'}")
        if not first[1]:
            fc = next((d for d in diff if d[1]), None)   # the first was full only: where the core first differs
            print(f"first core difference: vb {fc[0]}, areas {' '.join(fc[2])}" if fc else "first core difference: none (full only)")
    return 1 if diff else 0


def load_dump(p):
    r = {}
    for l in open(p, errors="replace"):
        if l.startswith("#") or " = " not in l:
            continue
        k, _, v = l.rstrip("\n").partition(" = ")
        r[k] = v
    return r


def ulp(x, y):
    m = re.match(r"0x([0-9a-f]{8}) ", x), re.match(r"0x([0-9a-f]{8}) ", y)
    if not (m[0] and m[1]):
        return ""
    def key(h):
        i = int(h, 16)
        return i if i < 0x80000000 else -(i & 0x7fffffff)
    return f"  [{abs(key(m[0].group(1)) - key(m[1].group(1)))} ulp]"


def cmp_dump(da, db, n, vb):
    if vb is None:
        va = {int(f[6:-4]) for f in os.listdir(da) if re.match(r"state_\d+\.txt$", f)}
        vbs = sorted(va & {int(f[6:-4]) for f in os.listdir(db) if re.match(r"state_\d+\.txt$", f)})
        if not vbs:
            print("no common state_<vb>.txt files")
            return 2
    else:
        vbs = [vb]
    for v in vbs:
        a, b = load_dump(os.path.join(da, f"state_{v}.txt")), load_dump(os.path.join(db, f"state_{v}.txt"))
        keys = list(dict.fromkeys(list(a) + list(b)))
        d = [k for k in keys if a.get(k) != b.get(k)]
        info = [k for k in d if k.split(" ", 1)[0].endswith("/info")]   # dump-only rows (untyped words, not hashed)
        d = [k for k in d if k not in info]
        if not d and vb is None:
            continue
        print(f"vb {v}: fields {len(a)} / {len(b)}, differ {len(d)}" + (f" (+ {len(info)} info rows, not hashed)" if info else ""))
        for k in d[:n]:
            x, y = a.get(k, "absent"), b.get(k, "absent")
            print(f"  {k}: {x} | {y}{ulp(x, y)}")
        return 1 if d else 0
    print(f"dumps identical ({len(vbs)} vblanks)")
    return 0


def main(av):
    if len(av) >= 3 and av[0] == "--dump":
        return cmp_dump(av[1], av[2], int(av[3]) if len(av) > 3 else 20, int(av[4]) if len(av) > 4 else None)
    if len(av) < 2:
        print(__doc__ or "usage: stcmp.py A.st.txt B.st.txt [N] | --dump A_dir B_dir [N] [VB]")
        return 2
    return cmp_lines(av[0], av[1], int(av[2]) if len(av) > 2 else 6)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
