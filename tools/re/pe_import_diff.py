"""Compare the imports of two PE32 files, per imported DLL (case-insensitive). Read-only.

Usage: pe_import_diff.py <original.dll> <ours.dll>

For every DLL imported by either file, print the functions only the original imports ("-")
and the ones only ours imports ("+").
"""
import sys
sys.path.insert(0, sys.argv[0].rsplit("/", 1)[0])
from pe_imports import read


# The classic Winsock functions are imported by ordinal by old binaries; names for the ones we meet.
WS2_32_ORDINALS = {
    1: "accept", 2: "bind", 3: "closesocket", 4: "connect", 6: "getsockname", 8: "htonl", 9: "htons",
    10: "ioctlsocket", 11: "inet_addr", 12: "inet_ntoa", 13: "listen", 16: "recv", 17: "recvfrom",
    19: "send", 20: "sendto", 21: "setsockopt", 23: "socket", 51: "gethostbyaddr", 52: "gethostbyname",
    57: "gethostname", 111: "WSAGetLastError", 115: "WSAStartup", 116: "WSACleanup",
}


def imports(path):
    _, _, table, _ = read(path)
    result = {}
    for dll, funcs in table.items():
        dll = dll.lower()
        if dll in ("ws2_32.dll", "wsock32.dll"):
            funcs = [WS2_32_ORDINALS.get(int(f[1:]), f) if f.startswith("#") else f for f in funcs]
        result[dll] = set(funcs)
    return result


original, ours = imports(sys.argv[1]), imports(sys.argv[2])
for dll in sorted(set(original) | set(ours)):
    a, b = original.get(dll, set()), ours.get(dll, set())
    status = "identical" if a == b else "subset" if b < a else "differs"
    print(f"{dll}: original {len(a)}, ours {len(b)} -> {status}")
    for name in sorted(a - b):
        print(f"  - {name}")
    for name in sorted(b - a):
        print(f"  + {name}")
