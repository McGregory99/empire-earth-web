"""List SSA archive contents and summarize by extension. Read-only."""
import collections, os, struct, sys


def u32(f):
    return struct.unpack("<I", f.read(4))[0]


def index(path):
    with open(path, "rb") as f:
        assert f.read(4) == b"rass", "bad magic"
        major, minor, dso = u32(f), u32(f), u32(f)
        entries = []
        while f.tell() < dso + 16:
            n = u32(f)
            name = f.read(n).rstrip(b"\0").decode("iso-8859-15")
            start, end, size = u32(f), u32(f), u32(f)
            entries.append((name, start, size))
        out = []
        for name, start, size in entries:
            f.seek(start)
            head = f.read(16)
            compressed = head.startswith(b"PK01")
            raw_size = struct.unpack("<I", head[4:8])[0] if compressed else size
            magic = head[8:12] if compressed else head[:4]
            out.append((name, size, raw_size, compressed, magic))
        return (major, minor), out


def ext(name):
    base = name.replace("\\", "/").rsplit("/", 1)[-1]
    return base.rsplit(".", 1)[-1].lower() if "." in base else "(none)"


if __name__ == "__main__":
    for path in sys.argv[1:]:
        ver, files = index(path)
        print(f"== {path}  v{ver[0]}.{ver[1]}  files={len(files)}")
        by = collections.defaultdict(lambda: [0, 0, 0, 0])
        dirs = collections.Counter()
        for name, size, raw, comp, _ in files:
            b = by[ext(name)]
            b[0] += 1; b[1] += size; b[2] += raw; b[3] += comp
            dirs[name.replace("\\", "/").split("/")[0]] += 1
        print("  ext        count   stored_MB  raw_MB  compressed")
        for e, (c, s, r, k) in sorted(by.items(), key=lambda x: -x[1][1]):
            print(f"  {e:<9} {c:6} {s/1e6:10.1f} {r/1e6:7.1f} {k:6}")
        print("  top dirs:", dict(dirs.most_common(12)))
        if "--names" in os.environ.get("SSA_OPTS", ""):
            for name, *_ in files:
                print("   ", name)
