"""Import policy for the clang-built renderer plugin (task P2.1).

Usage: python3 -I check_imports.py <dll> [--require NAME ...]

The plugin must run on the VC6-era runtime the game ships against:

* only DLLs on ALLOWED_DLLS may be imported (msvcrt.dll, the engine itself, kernel32.dll);
* nothing from the UCRT / VC++ redistributables (api-ms-win-crt-*, ucrtbase, vcruntime*,
  msvcp*, msvcr*) may appear, whatever the DLL it comes from;
* every name given with --require must be imported from msvcrt.dll, which proves that the
  construct it stands for (pure virtual call, RTTI cast, operator new/delete) was really
  compiled in and linked, not optimized away.

Exit status 0 = policy holds.
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "re"))
from pe_imports import read  # noqa: E402

ALLOWED_DLLS = {"msvcrt.dll", "low-level engine.dll", "kernel32.dll"}
FORBIDDEN = re.compile(r"^(api-ms-win-|ucrtbase|vcruntime|msvcp|msvcr\d|concrt)", re.IGNORECASE)


def main(argv):
    path = argv[1]
    required = []
    if "--require" in argv:
        required = argv[argv.index("--require") + 1:]
    _, _, imports, _ = read(path)
    failures = []
    by_dll = {dll.lower(): funcs for dll, funcs in imports.items()}
    for dll in sorted(by_dll):
        if FORBIDDEN.match(dll):
            failures.append(f"imports forbidden runtime DLL {dll}")
        elif dll not in ALLOWED_DLLS:
            failures.append(f"imports {dll}, which is not on the allow-list {sorted(ALLOWED_DLLS)}")
    msvcrt = set(by_dll.get("msvcrt.dll", []))
    for name in required:
        if name not in msvcrt:
            failures.append(f"msvcrt.dll import {name} is missing")
    for dll in sorted(by_dll):
        print(f"  {dll} ({len(by_dll[dll])}): {', '.join(sorted(by_dll[dll]))}")
    for failure in failures:
        print(f"FAIL: {failure}")
    if not failures:
        print("PASS: imports are limited to msvcrt.dll, the engine and kernel32.dll")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
