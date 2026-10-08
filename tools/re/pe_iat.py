"""Print 'PTR_<va> <dll>!<name>' for every IAT slot of a PE32 image. Read-only."""
import struct, sys
sys.path.insert(0, sys.argv[0].rsplit("/", 1)[0])
from pe_imports import read
path = sys.argv[1]
data = open(path, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
opt = pe + 24
base = struct.unpack_from("<I", data, opt + 28)[0]
imp_rva = struct.unpack_from("<I", data, opt + 96 + 8)[0]
_, secs, _, _ = read(path)
def off(rva):
    for _, va, vs, rp, rs, _ in secs:
        if va <= rva < va + max(vs, rs):
            return rva - va + rp
def cstr(o):
    return data[o:data.index(b"\0", o)].decode("latin1")
o = off(imp_rva)
while True:
    ilt, _, _, name_rva, iat = struct.unpack_from("<IIIII", data, o)
    if not name_rva:
        break
    dll = cstr(off(name_rva))
    t, i = off(ilt or iat), 0
    while True:
        v = struct.unpack_from("<I", data, t + 4 * i)[0]
        if not v:
            break
        name = f"Ordinal_{v & 0xFFFF}" if v & 0x80000000 else cstr(off(v) + 2)
        print(f"PTR_{base + iat + 4 * i:08x}\t{dll}\t{name}")
        i += 1
    o += 20
