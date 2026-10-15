"""Fixtures for tests/game_run/test-ri-analyze.sh: minimal PE32 files and a synthetic Wine +relay log.

Nothing here comes from the real game.  The PE files carry only the import and export tables that tools/game_run/scripts/ri.py
reads; the log has the shape of Wine 11's +relay,+loaddll,+tid,+timestamp output (see the pilot notes in odd/tasks).

    python3 -I ri_fixture.py <directory>      writes <directory>/game, <directory>/wine, <directory>/relay.log, <directory>/phases.txt
"""
import os
import struct
import sys
from collections import OrderedDict

IMAGE_RVA = 0x1000
RAW_OFF = 0x200


def build_pe(path, imports=None, exports=None, image_base=0x400000, size_of_image=0x20000):
    """imports: OrderedDict dll -> [name or '#ordinal'].  exports: OrderedDict name -> None | 'DLL.func' (a forwarder) | ('#', ordinal)."""
    imports = imports or {}
    exports = exports or {}
    blob = bytearray()

    def here():
        return IMAGE_RVA + len(blob)

    def put_str(s):
        rva = here()
        blob.extend(s.encode("latin1") + b"\0")
        if len(blob) % 2:
            blob.append(0)
        return rva

    import_rva = import_size = export_rva = export_size = 0
    if imports:
        # strings and thunks first, descriptors last
        descs = []
        for dll, names in imports.items():
            hint_rvas = []
            for n in names:
                if n.startswith("#"):
                    hint_rvas.append(0x80000000 | int(n[1:]))
                else:
                    r = here()
                    blob.extend(struct.pack("<H", 0))
                    put_str(n)
                    hint_rvas.append(r)
            ilt = here()
            for v in hint_rvas:
                blob.extend(struct.pack("<I", v))
            blob.extend(struct.pack("<I", 0))
            iat = here()
            for v in hint_rvas:
                blob.extend(struct.pack("<I", v))
            blob.extend(struct.pack("<I", 0))
            descs.append((put_str(dll), ilt, iat))
        import_rva = here()
        for name_rva, ilt, iat in descs:
            blob.extend(struct.pack("<IIIII", ilt, 0, 0, name_rva, iat))
        blob.extend(b"\0" * 20)
        import_size = here() - import_rva
    if exports:
        names = [n for n, v in exports.items() if not (isinstance(v, tuple))]
        entries = list(exports.items())
        # function table: one slot per export, ordinals 1..n in listed order (tuple ('#', k) pins the ordinal k)
        base = 1
        slots = {}
        nxt = 1
        for n, v in entries:
            if isinstance(v, tuple):
                slots[n] = v[1]
            else:
                slots[n] = nxt
                nxt += 1
        top = max(slots.values())
        start = here()
        blob.extend(b"\0" * 40)
        funcs_rva = here()
        blob.extend(b"\0" * (4 * top))
        sorted_names = sorted(slots)
        names_rva = here()
        blob.extend(b"\0" * (4 * len(sorted_names)))
        ords_rva = here()
        blob.extend(b"\0" * (2 * len(sorted_names)))
        dll_name_rva = put_str("fixture.dll")
        name_rvas = {}
        for n in sorted_names:
            name_rvas[n] = put_str(n)
        fwd_rvas = {}
        for n, v in entries:
            if isinstance(v, str):
                fwd_rvas[n] = put_str(v)
        exp_end = here()
        struct.pack_into("<IIHHIIIIIII", blob, start - IMAGE_RVA, 0, 0, 0, 0, dll_name_rva, base, top, len(sorted_names),
                         funcs_rva, names_rva, ords_rva)
        for n, v in entries:
            slot = slots[n] - base
            rva = fwd_rvas[n] if isinstance(v, str) else 0x1500  # a "code" address outside the export directory
            struct.pack_into("<I", blob, funcs_rva - IMAGE_RVA + 4 * slot, rva)
        for i, n in enumerate(sorted_names):
            struct.pack_into("<I", blob, names_rva - IMAGE_RVA + 4 * i, name_rvas[n])
            struct.pack_into("<H", blob, ords_rva - IMAGE_RVA + 2 * i, slots[n] - base)
        export_rva, export_size = start, exp_end - start

    dos = bytearray(0x40)
    dos[0:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 0x40)
    coff = struct.pack("<HHIIIHH", 0x14C, 1, 0, 0, 0, 0xE0, 0x2102)
    opt = bytearray(0xE0)
    struct.pack_into("<H", opt, 0, 0x10B)
    struct.pack_into("<I", opt, 28, image_base)
    struct.pack_into("<I", opt, 32, 0x1000)
    struct.pack_into("<I", opt, 36, 0x200)
    struct.pack_into("<I", opt, 56, size_of_image)
    struct.pack_into("<I", opt, 60, 0x200)
    struct.pack_into("<I", opt, 92, 16)
    struct.pack_into("<II", opt, 96, export_rva, export_size)
    struct.pack_into("<II", opt, 96 + 8, import_rva, import_size)
    raw_size = (len(blob) + 0x1FF) & ~0x1FF
    sec = struct.pack("<8sIIIIIIHHI", b".rdata", len(blob), IMAGE_RVA, raw_size, RAW_OFF, 0, 0, 0, 0, 0x40000040)
    out = bytes(dos) + b"PE\0\0" + coff + bytes(opt) + sec
    out += b"\0" * (RAW_OFF - len(out)) + bytes(blob) + b"\0" * (raw_size - len(blob))
    with open(path, "wb") as f:
        f.write(out)


def write_world(root):
    game = os.path.join(root, "game")
    wine = os.path.join(root, "wine", "lib", "wine", "i386-windows")
    os.makedirs(game, exist_ok=True)
    os.makedirs(wine, exist_ok=True)
    od = OrderedDict
    build_pe(os.path.join(game, "Empire Earth.exe"), od([
        ("kernel32.dll", ["CreateThread", "ResumeThread", "QueryPerformanceCounter", "CreateFileA", "ReadFile", "CloseHandle",
                          "EnterCriticalSection", "GetLastError", "LoadLibraryA", "GetProcAddress", "FindFirstFileA"]),
        ("advapi32.dll", ["RegOpenKeyExA", "RegQueryValueExA", "RegSetValueExA", "RegCloseKey"]),
        ("wsock32.dll", ["#115", "#23"]),
        ("user32.dll", ["CreateWindowExA", "PeekMessageA", "SetWindowsHookExA", "GetMessageA"]),
        ("msvcrt.dll", ["fopen", "memcpy", "malloc", "_controlfp"]),
        ("shell32.dll", ["ShellExecuteA"]),
        ("low-level engine.dll", ["?Foo@@YAXXZ"]),
    ]), image_base=0x400000, size_of_image=0x200000)
    build_pe(os.path.join(game, "Low-Level Engine.dll"), od([
        ("kernel32.dll", ["CreateThread", "SuspendThread", "ResumeThread", "TerminateThread", "SetThreadPriority", "CreateFileA",
                          "CreateFileMappingA", "MapViewOfFile", "ReadFile", "SetFilePointer", "FindFirstFileA", "CloseHandle",
                          "LoadLibraryExA", "QueryPerformanceCounter"]),
        ("winmm.dll", ["timeSetEvent", "timeBeginPeriod", "timeGetTime", "mciSendCommandA"]),
        ("dinput.dll", ["DirectInputCreateA"]),
        ("ole32.dll", ["CoCreateInstance"]),
        ("advapi32.dll", ["RegOpenKeyExA"]),
    ]), image_base=0x10000000, size_of_image=0x100000)
    build_pe(os.path.join(game, "Mss32.dll"), od([
        ("kernel32.dll", ["CreateThread", "SetThreadPriority", "LoadLibraryA", "GetProcAddress"]),
        ("winmm.dll", ["waveOutOpen", "waveOutWrite"]),
    ]), image_base=0x20000000, size_of_image=0x80000)
    # 00xJS compatibility shim and D7VK: they are not the game's, their imports must not enter the lists
    build_pe(os.path.join(game, "version.dll"), od([("kernel32.dll", ["VirtualProtect"])]), image_base=0x30000000)
    build_pe(os.path.join(wine, "kernel32.dll"), od([], ), od([
        ("CreateThread", None), ("ResumeThread", None), ("SuspendThread", None), ("TerminateThread", None),
        ("SetThreadPriority", None), ("CreateFileA", None), ("CreateFileMappingA", None), ("MapViewOfFile", None),
        ("ReadFile", None), ("SetFilePointer", None), ("FindFirstFileA", None), ("CloseHandle", None), ("LoadLibraryA", None),
        ("LoadLibraryExA", None), ("GetProcAddress", None), ("QueryPerformanceCounter", None), ("GetLastError", None),
        ("EnterCriticalSection", "NTDLL.RtlEnterCriticalSection"), ("ExitProcess", None),
    ]), image_base=0x7B000000)
    build_pe(os.path.join(wine, "wsock32.dll"), None, od([("WSAStartup", ("#", 115)), ("socket", ("#", 23))]), image_base=0x7B800000)

    log = os.path.join(root, "relay.log")
    t = [90.0]

    def L(tid, text, dt=0.001):
        t[0] += dt
        return "%.3f:%s:%s" % (t[0], tid, text)

    def call(tid, fn, args, ret, retval=None, dt=0.001):
        out = [L(tid, "Call %s(%s) ret=%s" % (fn, args, ret), dt)]
        if retval is not None:
            out.append(L(tid, "Ret  %s() retval=%s ret=%s" % (fn, retval, ret), 0.0005))
        return out

    lines = []
    # an unrelated process (start.exe) creating a thread
    lines.append(L("0002", 'trace:loaddll:build_module Loaded L"C:\\\\windows\\\\system32\\\\start.exe" at 0000000140000000: builtin'))
    lines += call("0002", "KERNEL32.CreateThread", "00000000,00000000,140003d80,00000000,00000000,00000000", "140001771", "000000ac")
    # the game
    t[0] = 99.999
    lines.append(L("0010", 'trace:loaddll:build_module Loaded L"C:\\\\windows\\\\syswow64\\\\kernel32.dll" at 7B000000: builtin'))
    lines.append(L("0010", 'trace:loaddll:build_module Loaded L"C:\\\\windows\\\\syswow64\\\\winmm.dll" at 7BA60000: builtin'))
    lines.append(L("0010", 'trace:loaddll:build_module Loaded L"C:\\\\GOG Games\\\\Empire Earth Gold Edition\\\\Empire Earth\\\\Empire Earth.exe" at 00400000: native'))
    lines.append(L("0010", 'trace:loaddll:build_module Loaded L"C:\\\\GOG Games\\\\Empire Earth Gold Edition\\\\Empire Earth\\\\Low-Level Engine.dll" at 10000000: native'))
    lines.append(L("0010", 'trace:loaddll:build_module Loaded L"C:\\\\GOG Games\\\\Empire Earth Gold Edition\\\\Empire Earth\\\\Mss32.dll" at 20000000: native'))
    lines.append(L("0010", 'trace:loaddll:build_module Loaded L"C:\\\\GOG Games\\\\Empire Earth Gold Edition\\\\Empire Earth\\\\version.dll" at 30000000: native'))
    # LLE creates a suspended simulation thread, sets its priority, resumes it; the exe suspends it once
    lines += call("0010", "KERNEL32.CreateThread", "00000000,00000000,10004000,00123456,00000004,00000000", "10001111", "000000c4")
    lines.append(L("0014", "Starting thread proc 10004000 (arg=00123456)"))
    lines += call("0010", "KERNEL32.SetThreadPriority", "000000c4,00000001", "10001122", "00000001")
    lines += call("0010", "KERNEL32.ResumeThread", "000000c4", "10001133", "00000001")
    lines += call("0010", "KERNEL32.SuspendThread", "000000c4", "10001144", "00000000")
    lines += call("0010", "KERNEL32.ResumeThread", "000000c4", "004a0010", "00000001")
    # Miles creates a mixer thread that raises its own priority
    lines += call("0010", "KERNEL32.CreateThread", "00000000,00000000,20003000,00000000,00000000,00000000", "20001010", "000000c8")
    lines.append(L("0018", "Starting thread proc 20003000 (arg=00000000)"))
    lines += call("0018", "KERNEL32.SetThreadPriority", "fffffffe,00000002", "20003010", "00000001")
    lines += call("0014", "KERNEL32.TerminateThread", "000000c8", "10004100", "00000001")
    # winmm's timer thread for timeSetEvent
    lines += call("0010", "winmm.timeBeginPeriod", "00000001", "10002000", "00000000")
    lines += call("0010", "KERNEL32.SetThreadPriority", "fffffffe,00000000", "7ba70000", "00000001")  # Wine's own code (winmm range) on the main thread
    lines += call("0010", "winmm.timeSetEvent", "00000010,00000001,10005000,00000000,00000001", "10002010")
    lines += call("0010", "KERNEL32.CreateThread", "00000000,00000000,7ba6aba0,7ba60000,00000000,00000000", "7ba7fae3", "000000cc")
    lines.append(L("001c", "Starting thread proc 7BA6ABA0 (arg=7BA60000)"))
    lines.append(L("0010", "Ret  winmm.timeSetEvent() retval=00000010 ret=10002010", 0.0005))
    # files
    lines += call("0014", "KERNEL32.CreateFileA", '00340550 "C:\\\\GOG Games\\\\Empire Earth Gold Edition\\\\Empire Earth\\\\Data\\\\data.ssa",80000000,00000001,00000000,00000003,00000080,00000000', "10003000", "000000d0")
    lines += call("0014", "KERNEL32.ReadFile", "000000d0,0012f000,00001000,0012fe00,00000000", "10003010", "00000001")
    lines += call("0014", "KERNEL32.ReadFile", "000000d0,0012f000,00002000,0012fe00,00000000", "10003010", "00000001")
    lines += call("0014", "KERNEL32.CreateFileMappingA", "000000d0,00000000,00000002,00000000,00000000,00000000", "10003020", "000000d4")
    lines += call("0014", "KERNEL32.MapViewOfFile", "000000d4,00000004,00000000,00000000,00100000", "10003030", "01230000")
    lines += call("0010", "KERNEL32.CreateFileA", '00340550 "C:\\\\GOG Games\\\\Empire Earth Gold Edition\\\\Empire Earth\\\\Users\\\\missing.cfg",c0000000,00000000,00000000,00000002,00000080,00000000', "004a1000", "ffffffff")
    lines += call("0010", "KERNEL32.FindFirstFileA", '00340550 "C:\\\\GOG Games\\\\Empire Earth Gold Edition\\\\Empire Earth\\\\Data\\\\*.ssa",0012f000', "004a1100", "000000d8")
    lines += call("0010", "msvcrt.fopen", '00340550 "ee.log",00340600 "wb"', "004a1200", "00ab0000")
    lines += call("0010", "KERNEL32.CloseHandle", "000000d0", "10003040", "00000001")
    # registry: open, read twice through the handle, write, close
    lines += call("0010", "ADVAPI32.RegOpenKeyExA", '80000001,00340550 "Software\\\\SSSI\\\\Empire Earth",00000000,00020019,0012fe40', "10006000", "00000000")
    lines += call("0010", "ADVAPI32.RegQueryValueExA", '000000e0,00340600 "Rasterizer Name",00000000,0012fe44,0012fe50,0012fe48', "10006010", "00000000")
    lines += call("0010", "ADVAPI32.RegSetValueExA", '000000e0,00340600 "Resolution",00000000,00000004,0012fe60,00000004', "10006020", "00000000")
    lines += call("0010", "ADVAPI32.RegOpenKeyExA", '80000002,00340550 "Software\\\\Nope",00000000,00020019,0012fe40', "10006000", "00000002")
    lines += call("0010", "ADVAPI32.RegCloseKey", "000000e0", "10006030", "00000000")
    # modules, dynamic resolution
    lines.append(L("0010", 'trace:loaddll:build_module Loaded L"C:\\\\windows\\\\syswow64\\\\dsound.dll" at 7C000000: builtin'))
    lines += call("0018", "KERNEL32.LoadLibraryA", '00340550 "dsound.dll"', "20002000", "7c000000")
    lines += call("0018", "KERNEL32.GetProcAddress", '7c000000,00340600 "DirectSoundCreate"', "20002010", "7c001234")
    lines += call("0018", "KERNEL32.GetProcAddress", '7c000000,00340600 "DirectSoundNope"', "20002010", "00000000")
    lines += call("0010", "KERNEL32.LoadLibraryExA", '00340550 "TCP Protocol.dll",00000000,00000008', "10002100", "0d000000")
    lines += call("0010", "winmm.mciSendCommandA", "00000000,00000803,00002000,0012fe00", "10002200", "00000124")
    lines += call("0018", "winmm.waveOutOpen", "0012fd00,ffffffff,0012fd10,00000000,00000000,00000000", "20002300", "00000000")
    lines += call("0010", "dinput.DirectInputCreateA", "00400000,00000700,0012fd00,00000000", "10002300", "00000000")
    lines += call("0010", "ole32.CoCreateInstance", "0012f000,00000000,00000001,0012f010,0012fd00", "10002400", "00000000")
    lines.append(L("0010", "trace:ole:CoCreateInstance {d2ac2881-b39b-11d1-8704-00600893b1bd}, 0000000000000000, 0x1, {0000000c-0000-0000-c000-000000000046}, 0012fd00."))
    lines.append(L("0010", "trace:ole:CoCreateInstanceEx {d2ac2881-b39b-11d1-8704-00600893b1bd}, 0000000000000000, 0x1, 0000000000000000, 1, 000000000031F640"))
    lines.append(L("0010", "trace:ole:CoCreateInstance {bcde0395-e52f-467c-8e3d-c4579291692e}, 0000000000000000, 0x1, {a95664d2-9614-4f35-a746-de8db63617e6}, 0012fd00."))
    lines += call("0010", "USER32.CreateWindowExA", '00000008,00340550 "EmpireEarth",00340600 "Empire Earth",90000000,00000000,00000000,00000400,00000300,00000000,00000000,00400000,00000000', "004a2000", "00010020")
    lines += call("0010", "USER32.SetWindowsHookExA", "00000002,10007000,00000000,00000010", "004a2500", "00000050")
    lines.append(L("0010", "Call window proc 004A0010 (hwnd=00010020,msg=WM_PAINT,wp=00000000,lp=00000000)"))
    lines.append(L("0010", "Call window proc 004A0010 (hwnd=00010020,msg=WM_PAINT,wp=00000000,lp=00000000)"))
    lines.append(L("0010", "Call window proc 10007000 (hwnd=00010020,msg=WM_ACTIVATEAPP,wp=00000001,lp=00000000)"))
    lines.append(L("0002", "Call window proc 00006FFFFFF48C70 (hwnd=0000000000010020,msg=WM_NCCREATE,wp=00000000,lp=0031ebb8)"))
    lines += call("0010", "USER32.GetMessageA", "0012f000,00000000,00000000,00000000", "30000100", "00000001")  # through the 00xJS shim
    lines += call("0010", "wsock32.WSAStartup", "00000101,0012fc00", "004a3000", "00000000")
    lines += call("0010", "msvcrt._controlfp", "00010000,00030000", "004a3100", "0008001f")
    # the window between idle_start and idle_end: 4 QPC calls by LLE, 2 by Wine's winmm (host)
    idle_start = t[0] + 0.5
    t[0] = idle_start
    for _ in range(4):
        lines += call("0014", "KERNEL32.QueryPerformanceCounter", "0012fe94", "10009000", "00000001", dt=1.0)
    for _ in range(2):
        lines += call("001c", "KERNEL32.QueryPerformanceCounter", "0012fe94", "7ba7f992", "00000001", dt=1.0)
    idle_end = t[0] + 0.5
    t[0] = idle_end
    lines += call("0010", "KERNEL32.ExitProcess", "00000000", "7bc0aaaa", None)
    lines.append(L("0010", 'trace:loaddll:free_modref Unloaded module L"C:\\\\GOG Games\\\\Empire Earth Gold Edition\\\\Empire Earth\\\\Mss32.dll" : native'))
    # noise that must not break the parser
    lines.append(L("0010", "Call PE DLL (proc=00006FFFFFF42D10,module=00006FFFFFF30000 L\"ntdll.dll\",reason=PROCESS_ATTACH,res=000000000031FB00)"))
    lines.append("fixme:dsound:something not implemented")
    with open(log, "w") as f:
        f.write("\n".join(lines) + "\n")
    with open(os.path.join(root, "phases.txt"), "w") as f:
        f.write("launch 1792070000 99.000\nmenu 1792070100 %.3f\nidle_start 1792070101 %.3f\nidle_end 1792070131 %.3f\nexited 1792070140 %.3f\n"
                % (idle_start - 1, idle_start, idle_end, idle_end + 1))


if __name__ == "__main__":
    write_world(sys.argv[1])
