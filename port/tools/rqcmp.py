# rq frame-hash compare (ACNB_RQ_HASH=1 `rq frame N: C records hash H` lines): rqcmp.py A.rq.txt B.rq.txt [N]
# prints the frame counts, the number of differing frames and the first N differences (default 6).
import sys
def load(p):
    r = {}
    for l in open(p, errors="replace"):
        i = l.find("rq frame ")
        if i < 0: continue
        f = l[i:].split()
        r[int(f[2].rstrip(":"))] = (f[3], f[6])
    return r
a, b = load(sys.argv[1]), load(sys.argv[2])
n = int(sys.argv[3]) if len(sys.argv) > 3 else 6
d = [v for v in sorted(set(a) & set(b)) if a[v] != b[v]]
print(f"frames {len(a)} / {len(b)}, common {len(set(a) & set(b))}, differ {len(d)}")
for v in d[:n]: print(f"  vb {v}: {a[v][0]} rec {a[v][1]} | {b[v][0]} rec {b[v][1]}")
