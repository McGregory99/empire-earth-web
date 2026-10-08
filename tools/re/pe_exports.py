"""Dump PE export names and group MSVC-mangled names by class. Read-only."""
import collections, re, struct, sys
sys.path.insert(0, sys.argv[0].rsplit("/", 1)[0])
from pe_imports import read
path = sys.argv[1]
data = open(path, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
opt = pe + 24
exp_rva = struct.unpack_from("<I", data, opt + 96)[0]
_, secs, _, _ = read(path)
def off(rva):
    for _, va, vs, rp, rs, _ in secs:
        if va <= rva < va + max(vs, rs):
            return rva - va + rp
o = off(exp_rva)
n_names, names_rva = struct.unpack_from("<I", data, o + 24)[0], struct.unpack_from("<I", data, o + 32)[0]
names = []
for i in range(n_names):
    r = struct.unpack_from("<I", data, off(names_rva) + 4 * i)[0]
    s = off(r); names.append(data[s:data.index(b"\0", s)].decode("latin1"))
cls = collections.Counter()
for nm in names:
    m = re.match(r"\?{1,2}[^@]*@([A-Za-z_][A-Za-z0-9_]*)@@", nm) or re.match(r"\?\?[0-9_][A-Z]?([A-Za-z_][A-Za-z0-9_]*)@@", nm)
    if m: cls[m.group(1)] += 1
print(f"exports={len(names)} classes={len(cls)}")
pref = collections.Counter(re.match(r"[A-Z]+?(?=[A-Z][a-z])|[A-Z]+", c).group(0) if re.match(r"[A-Z]", c) else "?" for c in cls)
print("class prefixes:", dict(pref.most_common(12)))
print("top classes:", ", ".join(f"{c}({k})" for c, k in cls.most_common(45)))
