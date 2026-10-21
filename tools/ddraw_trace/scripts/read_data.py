#!/usr/bin/env python3
"""Reference reader of the ddraw_trace side data file (README.md, "Data file").

  read_data.py FILE summary [--json]        counts and bytes per record kind, checks the structure and every payload hash
  read_data.py FILE extract OFFSET OUT      write the payload of the record at byte OFFSET to OUT, print its header as JSON
                                            (--raw: write the record with its header; OFFSET and the length come from a
                                            trace line's "blob=@OFFSET+LENGTH")

Exit status 0 on success, 1 for a malformed file or a bad request. Only the standard library is used.
"""
import argparse
import json
import struct
import sys

FILE_MAGIC = b"DDTDATA1"
FILE_HDR = 16
REC_HDR = 96
REC_MAGIC = 0x31524444  # "DDR1" little endian
KINDS = {1: "vertices", 2: "indices", 3: "surface"}
FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK64 = 0xFFFFFFFFFFFFFFFF


class FormatError(Exception):
    pass


def fnv1a64(data):
    h = FNV_OFFSET
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & MASK64
    return h


def parse_header(buf, off):
    """The 96-byte record header at buf[off:] as a dict."""
    if off < 0:
        raise FormatError("negative offset %d (record offsets count from the start of the file)" % off)
    if off + REC_HDR > len(buf):
        raise FormatError("truncated record header at offset %d" % off)
    magic, kind, total, seq, obj, plen, phash = struct.unpack_from("<6IQ", buf, off)
    if magic != REC_MAGIC:
        raise FormatError("bad record magic at offset %d" % off)
    params = list(struct.unpack_from("<16I", buf, off + 32))
    if total < REC_HDR + plen or total % 8 or total - REC_HDR - plen >= 8:
        raise FormatError("inconsistent lengths at offset %d (total %d, payload %d)" % (off, total, plen))
    if off + total > len(buf):
        raise FormatError("truncated record at offset %d (%d bytes missing)" % (off, off + total - len(buf)))
    return {"offset": off, "kind": kind, "kind_name": KINDS.get(kind, "unknown"), "total": total, "seq": seq,
            "object": obj, "payload_length": plen, "payload_hash": "%016x" % phash, "params": params}


def payload_of(buf, rec):
    start = rec["offset"] + REC_HDR
    return buf[start:start + rec["payload_length"]]


def read_file(path):
    with open(path, "rb") as f:
        buf = f.read()
    if len(buf) < FILE_HDR or buf[:8] != FILE_MAGIC:
        raise FormatError("not a ddraw_trace data file (bad magic)")
    version = struct.unpack_from("<I", buf, 8)[0]
    if version != 1:
        raise FormatError("unsupported version %d" % version)
    return buf


def records(buf):
    off = FILE_HDR
    while off < len(buf):
        rec = parse_header(buf, off)
        yield rec
        off += rec["total"]


def cmd_summary(buf, args):
    per = {}
    n = bad_hash = 0
    for rec in records(buf):
        n += 1
        k = per.setdefault(rec["kind_name"], {"records": 0, "payload_bytes": 0, "file_bytes": 0})
        k["records"] += 1
        k["payload_bytes"] += rec["payload_length"]
        k["file_bytes"] += rec["total"]
        if "%016x" % fnv1a64(payload_of(buf, rec)) != rec["payload_hash"]:
            bad_hash += 1
    out = {"file_bytes": len(buf), "records": n, "payload_hash_mismatches": bad_hash, "kinds": per}
    if args.json:
        print(json.dumps(out, indent=2, sort_keys=True))
    else:
        print("file bytes: %d" % len(buf))
        print("records: %d" % n)
        for name in sorted(per):
            k = per[name]
            print("  %-9s %6d records %12d payload bytes %12d file bytes" % (name, k["records"], k["payload_bytes"], k["file_bytes"]))
        print("payload hash mismatches: %d" % bad_hash)
    return 1 if bad_hash else 0


def cmd_extract(buf, args):
    rec = parse_header(buf, args.offset)  # raises for an offset that is not a record start inside the file
    data = buf[rec["offset"]:rec["offset"] + rec["total"]] if args.raw else payload_of(buf, rec)
    if "%016x" % fnv1a64(payload_of(buf, rec)) != rec["payload_hash"]:
        raise FormatError("payload hash mismatch at offset %d" % args.offset)
    with open(args.out, "wb") as f:
        f.write(data)
    print(json.dumps(rec, sort_keys=True))
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file")
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("summary")
    s.add_argument("--json", action="store_true")
    e = sub.add_parser("extract")
    e.add_argument("offset", type=lambda v: int(v, 0))
    e.add_argument("out")
    e.add_argument("--raw", action="store_true")
    args = ap.parse_args(argv)
    try:
        buf = read_file(args.file)
        return cmd_summary(buf, args) if args.cmd == "summary" else cmd_extract(buf, args)
    except (FormatError, OSError) as ex:
        print("read_data: %s" % ex, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
