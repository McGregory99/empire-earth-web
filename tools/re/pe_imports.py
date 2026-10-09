"""Minimal PE32 reader: sections, imports (DLL -> functions), exports count. Read-only."""
import struct, sys


def read(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[pe:pe + 4] == b"PE\0\0"
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    ts = struct.unpack_from("<I", data, pe + 8)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    dirs = opt + 96
    exp_rva, _ = struct.unpack_from("<II", data, dirs)
    imp_rva, _ = struct.unpack_from("<II", data, dirs + 8)
    secs = []
    for i in range(nsec):
        o = opt + opt_size + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("latin1")
        vsize, va, rsize, rptr = struct.unpack_from("<IIII", data, o + 8)
        chars = struct.unpack_from("<I", data, o + 36)[0]
        secs.append((name, va, vsize, rptr, rsize, chars))

    def off(rva):
        for _, va, vs, rp, rs, _ in secs:
            if va <= rva < va + max(vs, rs):
                return rva - va + rp
        raise ValueError(hex(rva))

    def cstr(o):
        return data[o:data.index(b"\0", o)].decode("latin1")

    imports = {}
    o = off(imp_rva) if imp_rva else None  # a DLL without any import has no import directory
    while o is not None:
        ilt, _, _, name_rva, iat = struct.unpack_from("<IIIII", data, o)
        if not name_rva:
            break
        dll = cstr(off(name_rva))
        funcs, t = [], off(ilt or iat)
        while True:
            v = struct.unpack_from("<I", data, t)[0]
            if not v:
                break
            funcs.append(f"#{v & 0xFFFF}" if v & 0x80000000 else cstr(off(v) + 2))
            t += 4
        imports[dll] = funcs
        o += 20
    nexp = 0
    if exp_rva:
        nexp = struct.unpack_from("<I", data, off(exp_rva) + 24)[0]
    return ts, secs, imports, nexp


if __name__ == "__main__":
    import datetime
    verbose = "-v" in sys.argv
    for path in [a for a in sys.argv[1:] if a != "-v"]:
        ts, secs, imports, nexp = read(path)
        print(f"== {path.rsplit('/', 1)[-1]}  timestamp={datetime.datetime.fromtimestamp(ts, datetime.timezone.utc)}  exports={nexp}")
        for s in secs:
            print(f"   sec {s[0]:<8} va=0x{s[1]:06x} vsize=0x{s[2]:06x} raw=0x{s[4]:06x} chars=0x{s[5]:08x}")
        for dll, funcs in imports.items():
            shown = ", ".join(funcs if verbose else funcs[:12])
            print(f"   {dll} ({len(funcs)}): {shown}{'' if verbose or len(funcs) <= 12 else ', ...'}")
