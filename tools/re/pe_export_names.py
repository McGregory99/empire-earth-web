"""Print all export names of a PE32 DLL. Read-only."""
import struct, sys
sys.path.insert(0, sys.argv[0].rsplit("/", 1)[0])
from pe_imports import read
path = sys.argv[1]
data = open(path, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
exp_rva = struct.unpack_from("<I", data, pe + 24 + 96)[0]
_, secs, _, _ = read(path)
def off(rva):
    for _, va, vs, rp, rs, _ in secs:
        if va <= rva < va + max(vs, rs):
            return rva - va + rp
o = off(exp_rva)
n = struct.unpack_from("<I", data, o + 24)[0]
names_rva = struct.unpack_from("<I", data, o + 32)[0]
for i in range(n):
    r = struct.unpack_from("<I", data, off(names_rva) + 4 * i)[0]
    s = off(r); print(data[s:data.index(b"\0", s)].decode("latin1"))
