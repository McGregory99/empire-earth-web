#!/usr/bin/env bash
# Tests of scripts/read_data.py, the reference reader of the ddraw_trace data file: a hand-built synthetic file, malformed
# variants, and (if the unit tests produced one) the file the proxy wrote for the mock surface scenario.
#   test-read-data.sh <tools/ddraw_trace dir>
set -u
TOOL="$(cd "${1:?usage: test-read-data.sh <tools/ddraw_trace dir>}" && pwd)"
READ="$TOOL/scripts/read_data.py"
fails=0
checks=0
check_eq() { # check_eq <description> <expected> <actual>
  checks=$((checks + 1))
  if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1 (expected '$2', got '$3')"; fails=$((fails + 1)); fi
}
check_ok() { # check_ok <description> <command...>
  local d="$1"; shift; checks=$((checks + 1))
  if "$@" >/dev/null 2>&1; then echo "ok   $d"; else echo "FAIL $d"; fails=$((fails + 1)); fi
}
check_fails() { # check_fails <description> <stderr substring> <command...>
  local d="$1" want="$2" err; shift 2; checks=$((checks + 1))
  err="$("$@" 2>&1 >/dev/null)"; local rc=$?
  if [ "$rc" -ne 0 ] && [[ "$err" == *"$want"* ]]; then echo "ok   $d"; else echo "FAIL $d (rc=$rc, stderr '$err')"; fails=$((fails + 1)); fi
}

work="$(mktemp -d "${TMPDIR:-/tmp}/ddt-data.XXXXXX")"
trap 'rm -rf "$work"' EXIT

# Build a file with the documented layout: 16-byte header, then records of 96-byte header + payload padded to 8.
python3 -I - "$work" <<'PY'
import struct, sys
w = sys.argv[1]
def fnv(d):
    h = 0xCBF29CE484222325
    for b in d:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h
def rec(seq, kind, obj, payload, params):
    total = 96 + ((len(payload) + 7) & ~7)
    hdr = struct.pack("<6IQ", 0x31524444, kind, total, seq, obj, len(payload), fnv(payload))
    hdr += struct.pack("<16I", *(params + [0] * (16 - len(params))))
    return hdr + payload + b"\0" * (total - 96 - len(payload))
recs = [rec(0, 1, 7, bytes(range(72)), [0x152, 36, 0, 2, 0]),
        rec(1, 2, 0, struct.pack("<3H", 2, 1, 0), [3]),
        rec(2, 3, 9, bytes([0xAB]) * 20, [0, 0, 5, 1, 5, 1, 20, 1, 1])]
data = b"DDTDATA1" + struct.pack("<II", 1, 0) + b"".join(recs)
open(w + "/ok.data", "wb").write(data)
open(w + "/trunc.data", "wb").write(data[:-5])
open(w + "/magic.data", "wb").write(b"DDTDATX1" + data[8:])
bad = bytearray(data); bad[16 + 96 + 3] ^= 0xFF  # flip one payload byte of record 0
open(w + "/badhash.data", "wb").write(bytes(bad))
open(w + "/offsets.txt", "w").write("%d %d %d\n" % (16, 16 + len(recs[0]), 16 + len(recs[0]) + len(recs[1])))
PY
read -r o0 o1 o2 <"$work/offsets.txt"

out="$(python3 -I "$READ" "$work/ok.data" summary)"
check_eq "summary: record count" "records: 3" "$(printf '%s\n' "$out" | grep '^records:')"
check_eq "summary: vertices row" "  vertices       1 records           72 payload bytes          168 file bytes" "$(printf '%s\n' "$out" | grep 'vertices')"
check_eq "summary: indices row" "  indices        1 records            6 payload bytes          104 file bytes" "$(printf '%s\n' "$out" | grep 'indices')"
check_eq "summary: surface row" "  surface        1 records           20 payload bytes          120 file bytes" "$(printf '%s\n' "$out" | grep 'surface')"
check_eq "summary: no hash mismatch" "payload hash mismatches: 0" "$(printf '%s\n' "$out" | grep mismatches)"
check_eq "summary --json: total records" "3" "$(python3 -I "$READ" "$work/ok.data" summary --json | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["records"])')"

check_ok "extract the indices record" python3 -I "$READ" "$work/ok.data" extract "$o1" "$work/idx.bin"
check_eq "extracted payload is exactly the 6 index bytes" "020001000000" "$(xxd -p "$work/idx.bin")"
hdr="$(python3 -I "$READ" "$work/ok.data" extract "$o2" "$work/surf.bin")"
check_eq "extract prints the header: kind" "surface" "$(printf '%s' "$hdr" | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["kind_name"])')"
check_eq "extract prints the header: object" "9" "$(printf '%s' "$hdr" | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["object"])')"
check_eq "extract: 20 payload bytes without the padding" "20" "$(wc -c <"$work/surf.bin" | tr -d ' ')"
python3 -I "$READ" "$work/ok.data" extract "$o0" "$work/raw.bin" --raw >/dev/null
check_eq "extract --raw: header + payload + padding" "168" "$(wc -c <"$work/raw.bin" | tr -d ' ')"

check_fails "an offset inside a record is refused" "bad record magic" python3 -I "$READ" "$work/ok.data" extract $((o1 + 8)) "$work/x.bin"
check_fails "an offset past the end is refused" "truncated record header" python3 -I "$READ" "$work/ok.data" extract 100000 "$work/x.bin"
check_fails "a negative offset is refused" "negative offset" python3 -I "$READ" "$work/ok.data" extract -1 "$work/x.bin"
check_fails "a truncated file is reported" "truncated record" python3 -I "$READ" "$work/trunc.data" summary
check_fails "a wrong magic is reported" "bad magic" python3 -I "$READ" "$work/magic.data" summary
checks=$((checks + 1)); python3 -I "$READ" "$work/badhash.data" summary >"$work/bh.txt" 2>&1; rc=$?
if [ "$rc" -eq 1 ] && grep -q 'mismatches: 1' "$work/bh.txt"; then echo "ok   a flipped payload byte is a hash mismatch (exit 1)"; else echo "FAIL a flipped payload byte is a hash mismatch"; fails=$((fails + 1)); fi
check_fails "extract refuses a record whose hash does not match" "hash mismatch" python3 -I "$READ" "$work/badhash.data" extract "$o0" "$work/x.bin"

# the file the proxy wrote for the mock surface scenario (make test-unit), if present
real="$TOOL/build/testbed/trace-data-surf.txt.data"
if [ -f "$real" ]; then
  out="$(python3 -I "$READ" "$real" summary)"
  check_eq "proxy file: 6 records" "records: 6" "$(printf '%s\n' "$out" | grep '^records:')"
  check_eq "proxy file: all surface records" "6" "$(printf '%s\n' "$out" | awk '/surface/ {print $2}')"
  check_eq "proxy file: payload hashes agree with the headers" "payload hash mismatches: 0" "$(printf '%s\n' "$out" | grep mismatches)"
  ref="$(grep -m1 'surf#4.Unlock' "$TOOL/build/testbed/trace-data-surf.txt" | sed -n 's/.*blob=@\([0-9]*\)+\([0-9]*\).*/\1 \2/p')"
  python3 -I "$READ" "$real" extract "${ref% *}" "$work/b.bin" >/dev/null
  check_eq "proxy file: the 4x4 RGB565 texture b is 32 bytes of the offset its trace line names" "32" "$(wc -c <"$work/b.bin" | tr -d ' ')"
else
  echo "skip proxy file (run make test-unit first)"
fi

echo "$([ "$fails" -eq 0 ] && echo PASS || echo FAIL): $checks checks, $fails failed"
[ "$fails" -eq 0 ]
