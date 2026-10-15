#!/usr/bin/env python3
"""Helpers of the runtime Win32 inventory (runtime-inventory.sh).  Python 3 standard library only.

  ri.py lists     --game-dir D --wine-root R [--profile broad|timers] [--existing-exclude S] [--existing-fromexclude S]
      The Wine relay lists for one run, as JSON: RelayInclude (every Win32 function that the game's own binaries import, resolved
      through Wine's export tables, minus the hot ones), RelayExclude, RelayFromExclude, and the static import counts per DLL.
  ri.py summarize --log L --game-dir D --wine-root R [--phases P] [--profile broad|timers] --out DIR
      Turn a Wine +relay,+loaddll,+tid,+timestamp log into DIR/summary.json and DIR/tables.md (the tables of the inventory).

Nothing here reads game data: only the PE headers of the binaries (import and export tables, SizeOfImage) and the log of the run.
"""
import argparse
import json
import os
import re
import struct
import sys
from collections import Counter, OrderedDict, defaultdict

# ---- what is the game's, what is the compatibility stack's ---------------------------------------------------------------------

GAME_MODULES = {
    "empire earth.exe", "ee-aoc.exe", "low-level engine.dll", "mss32.dll", "miles sound system mixer.dll", "binkw32.dll",
    "dx7hrdisplay.dll", "dx7hrtnldisplay.dll", "tcp protocol.dll", "sxlrt233.dll", "default.dll", "language.dll",
}
# the 00xJS launcher's shim (version.dll), its ddraw proxy and D7VK/DXVK: the game does not run on these in Windows
COMPAT_MODULES = {
    "version.dll", "version_eeorig.dll", "ddraw.dll", "ddraw_eeorig.dll", "ddraw_.dll", "d3d9.dll", "dxgi.dll", "d3d11.dll",
    "d3d10core.dll", "ee-resize.exe",
}
# imports that are the engine's own or C++ runtime, not the Windows API
PRIVATE_DLLS = {"low-level engine", "sxlrt233", "mss32", "binkw32", "msvcp60", "miles sound system mixer"}

# Called too often to trace.  Their calls are "not traced", not "never called".
HOT_KERNEL = [
    "InterlockedIncrement", "InterlockedDecrement", "InterlockedExchange", "InterlockedCompareExchange", "InterlockedExchangeAdd",
    "GetLastError", "SetLastError", "GetCurrentThreadId", "GetCurrentThread", "GetCurrentProcess", "GetCurrentProcessId",
    "TlsGetValue", "TlsSetValue", "FlsGetValue", "FlsSetValue", "EnterCriticalSection", "LeaveCriticalSection",
    "TryEnterCriticalSection", "HeapAlloc", "HeapFree", "HeapReAlloc", "HeapSize", "GetProcessHeap", "LocalAlloc", "LocalFree",
]
# traced only by the "timers" profile: a spin loop on one of them could write gigabytes
TIMER_FUNCS = [
    ("kernel32", "QueryPerformanceCounter"), ("kernel32", "GetTickCount"), ("winmm", "timeGetTime"),
    ("kernel32", "GetSystemTimeAsFileTime"), ("kernel32", "GetLocalTime"), ("kernel32", "GetSystemTime"),
]
# of the C runtime only what touches the system and is rare; everything else (mem*, str*, malloc, printf, math) is hot
CRT_COLD = {
    "fopen", "_wfopen", "tmpfile", "remove", "_chmod", "_stat", "_utime", "_fullpath", "_splitpath", "getenv", "time", "gmtime",
    "localtime", "mktime", "_beginthreadex", "_endthreadex", "exit", "_exit", "abort", "_controlfp", "_control87", "_fpreset",
    "_CxxThrowException", "longjmp", "setlocale", "system", "_open", "_wopen", "_unlink", "_mkdir", "_rmdir", "_getcwd", "_chdir",
}
EXTRA_BROAD = [
    "kernel32.CreateThread", "kernel32.ExitThread", "kernel32.TerminateThread", "kernel32.ExitProcess", "kernel32.TerminateProcess",
    "kernel32.LoadLibraryA", "kernel32.LoadLibraryW", "kernel32.LoadLibraryExA", "kernel32.LoadLibraryExW", "kernel32.GetProcAddress",
    "kernel32.SetUnhandledExceptionFilter", "kernel32.RaiseException", "kernel32.SuspendThread", "kernel32.ResumeThread",
    "kernel32.SetThreadPriority", "kernel32.SetThreadAffinityMask", "kernel32.SetPriorityClass", "kernel32.SwitchToThread",
    "ole32.CoCreateInstance", "ole32.CoInitializeEx", "ole32.CoGetClassObject", "dsound.*", "dinput.*", "dinput8.*",
] + ["winmm." + n for n in (
    "waveOutGetNumDevs", "waveOutGetDevCapsA", "waveOutGetDevCapsW", "waveOutOpen", "waveOutClose", "waveOutPrepareHeader",
    "waveOutUnprepareHeader", "waveOutWrite", "waveOutPause", "waveOutRestart", "waveOutReset", "waveOutGetPosition",
    "waveOutGetVolume", "waveOutSetVolume", "waveOutGetID", "waveOutMessage", "waveOutBreakLoop", "waveOutGetPitch",
    "waveOutSetPitch", "waveOutGetPlaybackRate", "waveOutSetPlaybackRate", "waveInGetNumDevs", "waveInOpen", "waveInClose",
    "waveInStart", "waveInStop", "waveInReset", "waveInAddBuffer", "waveInPrepareHeader", "waveInUnprepareHeader",
    "midiOutGetNumDevs", "midiOutGetDevCapsA", "midiOutOpen", "midiOutClose", "midiOutShortMsg", "midiOutLongMsg",
    "midiOutPrepareHeader", "midiOutUnprepareHeader", "midiOutReset", "midiOutMessage", "midiInGetNumDevs", "midiStreamOpen",
    "mixerGetNumDevs", "mixerOpen", "mixerClose", "mixerGetID", "mixerGetDevCapsA", "mixerGetLineInfoA", "mixerGetLineControlsA",
    "mixerGetControlDetailsA", "mixerSetControlDetails", "mixerMessage", "auxGetNumDevs", "auxGetDevCapsA", "auxGetVolume",
    "auxSetVolume", "auxOutMessage", "mciSendCommandA", "mciSendCommandW", "mciSendStringA", "mciSendStringW", "timeSetEvent",
    "timeKillEvent", "timeBeginPeriod", "timeEndPeriod", "PlaySoundA", "PlaySoundW", "sndPlaySoundA", "joyGetNumDevs",
    "joyGetPosEx", "joyGetDevCapsA")]
EXTRA_TIMERS = ["kernel32.CreateThread", "kernel32.ExitProcess"]
# callers that are Wine's own plumbing or the graphics translation layers; calls from them are not the game's
FROM_ADD = [
    "ntdll", "kernelbase", "win32u", "imm32", "shell32", "shlwapi", "ole32", "oleaut32", "combase", "coml2", "rpcrt4", "sechost",
    "ws2_32", "wsock32", "mmdevapi", "ucrtbase", "winecoreaudio.drv", "ddraw_eeorig", "ddraw_",
    "d3d9", "dxgi", "d3d11", "d3d10core", "wined3d", "winevulkan", "vulkan-1",
]

PROFILES = ("broad", "timers", "full")


# ---- PE files -------------------------------------------------------------------------------------------------------------------

def _cstr(b, o):
    return b[o:b.index(b"\0", o)].decode("latin1")


def read_pe(path):
    """imports: OrderedDict lower-case dll (without .dll) -> [name or '#ordinal']; exports: OrderedDict name -> forwarder or None;
    by_ordinal: ordinal -> name; size, base."""
    with open(path, "rb") as f:
        b = f.read()
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    if b[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE file: %s" % path)
    nsec = struct.unpack_from("<H", b, pe + 6)[0]
    opt_size = struct.unpack_from("<H", b, pe + 20)[0]
    opt = pe + 24
    if struct.unpack_from("<H", b, opt)[0] != 0x10B:
        raise ValueError("not a PE32 file: %s" % path)
    base = struct.unpack_from("<I", b, opt + 28)[0]
    size = struct.unpack_from("<I", b, opt + 56)[0]
    dirs = opt + 96
    secs = []
    for i in range(nsec):
        o = opt + opt_size + 40 * i
        vsize, va, rsize, rptr = struct.unpack_from("<IIII", b, o + 8)
        secs.append((va, max(vsize, rsize), rptr))

    def off(rva):
        for va, sz, rp in secs:
            if va <= rva < va + sz:
                return rva - va + rp
        raise ValueError("bad RVA 0x%x in %s" % (rva, path))

    imports = OrderedDict()
    irva, _ = struct.unpack_from("<II", b, dirs + 8)
    if irva:
        o = off(irva)
        while True:
            ilt, _, _, name_rva, iat = struct.unpack_from("<IIIII", b, o)
            if not name_rva:
                break
            dll = _cstr(b, off(name_rva)).lower()
            if dll.endswith(".dll"):
                dll = dll[:-4]
            t = off(ilt or iat)
            names = imports.setdefault(dll, [])
            while True:
                v = struct.unpack_from("<I", b, t)[0]
                if not v:
                    break
                names.append("#%d" % (v & 0xFFFF) if v & 0x80000000 else _cstr(b, off(v & 0x7FFFFFFF) + 2))
                t += 4
            o += 20
    exports = OrderedDict()
    by_ordinal = {}
    erva, esize = struct.unpack_from("<II", b, dirs)
    if erva:
        o = off(erva)
        ord_base, nfuncs, nnames, funcs, names, ords = struct.unpack_from("<IIIIII", b, o + 16)
        for i in range(nnames):
            name = _cstr(b, off(struct.unpack_from("<I", b, off(names) + 4 * i)[0]))
            idx = struct.unpack_from("<H", b, off(ords) + 2 * i)[0]
            frva = struct.unpack_from("<I", b, off(funcs) + 4 * idx)[0]
            fwd = _cstr(b, off(frva)) if erva <= frva < erva + esize else None
            exports[name] = fwd
            by_ordinal[ord_base + idx] = name
    return {"imports": imports, "exports": exports, "by_ordinal": by_ordinal, "size": size, "base": base}


def dll_dirs(wine_root):
    return [os.path.join(wine_root, "lib", "wine", "i386-windows")]


def find_file(directories, name):
    """Case-insensitive lookup of <name> in the directories; None if absent."""
    low = name.lower()
    for d in directories:
        try:
            for f in os.listdir(d):
                if f.lower() == low:
                    return os.path.join(d, f)
        except OSError:
            pass
    return None


class WineExports:
    def __init__(self, wine_root):
        self.dirs = dll_dirs(wine_root)
        self.cache = {}

    def get(self, dll):
        if dll not in self.cache:
            p = find_file(self.dirs, dll + ".dll")
            try:
                self.cache[dll] = read_pe(p) if p else None
            except (ValueError, OSError, struct.error):
                self.cache[dll] = None
        return self.cache[dll]


def game_binaries(game_dir):
    out = []
    for f in sorted(os.listdir(game_dir)):
        if f.lower() in GAME_MODULES:
            out.append(os.path.join(game_dir, f))
    return out


# ---- the relay lists ----------------------------------------------------------------------------------------------------------------

def _split_list(s):
    return [x for x in (s or "").split(";") if x]


def _dedupe(seq):
    seen = set()
    out = []
    for x in seq:
        if x.lower() not in seen:
            seen.add(x.lower())
            out.append(x)
    return out


def static_imports(game_dir, wine_root):
    """dll -> OrderedDict name -> set of game modules, ordinals resolved through Wine's export tables; engine-private DLLs left out."""
    wx = WineExports(wine_root)
    result = OrderedDict()
    for path in game_binaries(game_dir):
        mod = os.path.basename(path)
        for dll, names in read_pe(path)["imports"].items():
            if dll in PRIVATE_DLLS or dll.startswith("api-ms-"):
                continue
            exp = wx.get(dll)
            for n in names:
                if n.startswith("#") and exp:
                    n = exp["by_ordinal"].get(int(n[1:]), n)
                result.setdefault(dll, OrderedDict()).setdefault(n, set()).add(mod)
    return result


def _is_hot(dll, fn):
    return dll in ("kernel32", "kernelbase", "ntdll") and fn.lower() in {h.lower() for h in HOT_KERNEL}


def _is_timer(dll, fn):
    return any(dll == d and fn.lower() == f.lower() for d, f in TIMER_FUNCS)


def build_lists(game_dir, wine_root, profile="broad", existing_exclude="", existing_fromexclude=""):
    if profile not in PROFILES:
        raise ValueError("unknown profile %r (use %s)" % (profile, " or ".join(PROFILES)))
    wx = WineExports(wine_root)
    imports = static_imports(game_dir, wine_root)
    include = []
    traced = defaultdict(set)  # dll -> function names the include list covers (for "not traced")

    def add(dll, fn):
        include.append("%s.%s" % (dll, fn))
        traced[dll].add(fn.lower())
        exp = wx.get(dll)
        fwd = exp["exports"].get(fn) if exp else None
        if fwd and "." in fwd:  # a forwarder: the relay thunk is named after the target
            tdll, tfn = fwd.rsplit(".", 1)
            tdll = tdll.lower()
            if tdll.endswith(".dll"):
                tdll = tdll[:-4]
            if not tfn.startswith("#") and not _is_hot(tdll, tfn):
                include.append("%s.%s" % (tdll, tfn))

    if profile in ("broad", "full"):
        for dll, names in imports.items():
            for fn in names:
                if fn.startswith("#"):
                    continue
                if dll == "msvcrt":
                    if fn in CRT_COLD:
                        add(dll, fn)
                    continue
                if _is_hot(dll, fn) or (_is_timer(dll, fn) and profile == "broad"):
                    continue
                add(dll, fn)
        extra = EXTRA_BROAD
        if profile == "full":
            for d_, f_ in TIMER_FUNCS:
                add(d_, f_)
    else:
        for d, f in TIMER_FUNCS:
            add(d, f)
        extra = EXTRA_TIMERS
    for e in extra:
        dll, fn = e.split(".", 1)
        include.append(e)
        traced[dll].add(fn.lower())

    hot = []
    for fn in HOT_KERNEL:
        hot.append("kernel32." + fn)
        exp = wx.get("kernel32")
        fwd = exp["exports"].get(fn) if exp else None
        if fwd and "." in fwd:
            hot.append(fwd.replace(".dll", "").rsplit(".", 1)[0].lower() + "." + fwd.rsplit(".", 1)[1])
    exclude = _dedupe(_split_list(existing_exclude) + hot)
    fromexclude = _dedupe(_split_list(existing_fromexclude) + FROM_ADD)
    summary = OrderedDict()
    for dll, names in imports.items():
        summary[dll] = {"count": len(names), "names": list(names)}
    return {
        "profile": profile,
        "include": ";".join(_dedupe(include)),
        "exclude": ";".join(exclude),
        "fromexclude": ";".join(fromexclude),
        "n_include": len(_dedupe(include)),
        "imports": summary,
        "traced": {k: sorted(v) for k, v in traced.items()},
    }


# ---- the log ---------------------------------------------------------------------------------------------------------------------------

LINE = re.compile(r"^(?:(?P<ts>\d+\.\d+):)?(?P<tid>[0-9a-f]{4,8}):(?P<rest>.*)$")
CALL = re.compile(r"^Call (?P<dll>[\w\-]+(?:\.drv)?)\.(?P<fn>[^\s(]+)\((?P<args>.*)\) ret=(?P<ret>[0-9a-fA-F]+)$")
RET = re.compile(r"^Ret  (?P<dll>[\w\-]+(?:\.drv)?)\.(?P<fn>[^\s(]+)\(\) retval=(?P<rv>[0-9a-fA-F]+) ret=(?P<ret>[0-9a-fA-F]+)$")
WNDPROC = re.compile(r"^Call window proc (?P<proc>[0-9A-Fa-f]+) \(hwnd=[0-9A-Fa-f]+,msg=(?P<msg>\w+),")
START = re.compile(r"^Starting thread proc (?P<proc>[0-9a-fA-F]+) \(arg=(?P<arg>[0-9a-fA-F]+)\)$")
LOADED = re.compile(r'Loaded L"(?P<path>.+?)" at (?P<base>[0-9a-fA-F]+): (?P<kind>\w+)')
UNLOADED = re.compile(r'Unloaded module L"(?P<path>.+?)" : (?P<kind>\w+)')
GUID = re.compile(r"\{([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})\}")
COM_LINE = re.compile(r"^trace:ole:(?:CoCreateInstance|CoGetClassObject) \{([0-9a-fA-F-]{36})\}")
CHANNEL = re.compile(r"^(?:trace|fixme|err|warn):(?P<chan>[\w]+):(?P<msg>.*)$")

KNOWN_CLSIDS = {
    "d2ac2881-b39b-11d1-8704-00600893b1bd": "DirectMusicPerformance",
    "d2ac2892-b39b-11d1-8704-00600893b1bd": "DirectMusicLoader",
    "636b9f10-0c7d-11d1-95b2-0020afdc7421": "DirectMusic",
    "58c2b4d0-46e7-11d1-89ac-00a0c9054129": "DirectMusicSynth",
    "480ff4b0-28b2-11d1-bef7-00c04fbf8fef": "DirectMusicCollection",
    "d2ac2882-b39b-11d1-8704-00600893b1bd": "DirectMusicSegment",
    "d2ac2883-b39b-11d1-8704-00600893b1bd": "DirectMusicSegmentState",
    "d2ac2884-b39b-11d1-8704-00600893b1bd": "DirectMusicGraph",
    "d2ac2885-b39b-11d1-8704-00600893b1bd": "DirectMusicStyle",
}
MCI = {
    0x803: "MCI_OPEN", 0x804: "MCI_CLOSE", 0x805: "MCI_ESCAPE", 0x806: "MCI_PLAY", 0x807: "MCI_SEEK", 0x808: "MCI_STOP",
    0x809: "MCI_PAUSE", 0x80A: "MCI_INFO", 0x80B: "MCI_GETDEVCAPS", 0x80C: "MCI_SPIN", 0x80D: "MCI_SET", 0x80E: "MCI_STEP",
    0x80F: "MCI_RECORD", 0x810: "MCI_SYSINFO", 0x811: "MCI_BREAK", 0x813: "MCI_SAVE", 0x814: "MCI_STATUS", 0x830: "MCI_CUE",
    0x840: "MCI_REALIZE", 0x841: "MCI_WINDOW", 0x842: "MCI_PUT", 0x843: "MCI_WHERE", 0x844: "MCI_FREEZE", 0x845: "MCI_UNFREEZE",
    0x850: "MCI_LOAD", 0x851: "MCI_CUT", 0x852: "MCI_COPY", 0x853: "MCI_PASTE", 0x854: "MCI_UPDATE", 0x855: "MCI_RESUME",
    0x856: "MCI_DELETE",
}
ROOTS = {0x80000000: "HKCR", 0x80000001: "HKCU", 0x80000002: "HKLM", 0x80000003: "HKU", 0x80000005: "HKCC"}
DISPOSITION = {1: "CREATE_NEW", 2: "CREATE_ALWAYS", 3: "OPEN_EXISTING", 4: "OPEN_ALWAYS", 5: "TRUNCATE_EXISTING"}
ALIAS_GROUPS = [{"kernel32", "kernelbase", "ntdll"}, {"wsock32", "ws2_32"}, {"user32", "win32u"}, {"gdi32", "win32u"},
                {"advapi32", "kernelbase", "sechost"}]
AUDIO = re.compile(r"^(?:waveOut|waveIn|mixer|aux|midiOut|midiIn|DirectSound)")
KINDS = ("game", "compat", "wine", "other", "unknown")


def split_args(s):
    """The comma-separated arguments of a relay line.  A string argument is printed as `<pointer> "text"` or `<pointer> L"text"`."""
    out, cur, in_str, i = [], [], False, 0
    while i < len(s):
        c = s[i]
        if in_str:
            cur.append(c)
            if c == "\\" and i + 1 < len(s):
                cur.append(s[i + 1])
                i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
            cur.append(c)
        elif c == ",":
            out.append("".join(cur))
            cur = []
        else:
            cur.append(c)
        i += 1
    out.append("".join(cur))
    return out


_TOKEN = re.compile(r'^(?P<val>[0-9a-fA-F]+)?(?: ?(?P<wide>L)?"(?P<str>.*)")?$', re.S)


def unescape(s):
    return s.replace('\\"', '"').replace("\\\\", "\\")


class Arg:
    __slots__ = ("raw", "val", "str")

    def __init__(self, tok):
        self.raw = tok
        m = _TOKEN.match(tok)
        self.val = None
        self.str = None
        if m:
            if m.group("val"):
                try:
                    self.val = int(m.group("val"), 16)
                except ValueError:
                    pass
            if m.group("str") is not None:
                self.str = unescape(m.group("str"))
        elif tok.strip():
            try:
                self.val = int(tok.strip(), 16)
            except ValueError:
                pass


def parse_args(s):
    return [Arg(t) for t in split_args(s)] if s else []


def sgn(v, bits=32):
    return v - (1 << bits) if v is not None and v >= (1 << (bits - 1)) else v


class Frame:
    __slots__ = ("dll", "fn", "args", "ret", "tid", "ts", "mod", "extra")

    def arg(self, i):
        return self.args[i] if i < len(self.args) else Arg("")

    def val(self, i, default=0):
        v = self.arg(i).val
        return default if v is None else v

    def text(self, i):
        return self.arg(i).str


def base_name(path):
    return path.replace("\\\\", "\\").rsplit("\\", 1)[-1].rsplit("/", 1)[-1]


class Module:
    def __init__(self, path, base, kind_flag, size, ts, tid):
        self.path = path.replace("\\\\", "\\")
        self.name = base_name(self.path)
        self.base = base
        self.size = size
        self.flag = kind_flag
        low = self.name.lower()
        if low in GAME_MODULES:
            self.kind = "game"
        elif low in COMPAT_MODULES:
            self.kind = "compat"
        elif kind_flag == "builtin" or self.path.lower().startswith("c:\\windows\\"):
            self.kind = "wine"
        else:
            self.kind = "other"
        self.ts = ts
        self.tid = tid
        self.unloaded = False


class Summarizer:
    def __init__(self, game_dir, wine_root, profile):
        self.game_dir = game_dir
        self.dirs = [game_dir] + dll_dirs(wine_root)
        self.size_cache = {}
        self.lists = build_lists(game_dir, wine_root, profile)
        self.imports = static_imports(game_dir, wine_root)
        self.mods = []
        self.mod_cache = {}
        self.game_tids = set()
        self.root_tid = None
        self.pending_loads = defaultdict(list)
        self.stacks = defaultdict(list)
        self.threads = OrderedDict()
        self.pending_create = []
        self.handles = {}  # CreateThread handle -> tid
        self.last_begin = {}
        self.thread_ops = []
        self.prio_events = []
        self.thread_top = defaultdict(Counter)
        self.calls = defaultdict(lambda: defaultdict(lambda: {"total": 0, "game": 0, "by_module": Counter(), "by_kind": Counter(), "phase": defaultdict(Counter)}))
        self.timer_set = Counter()
        self.timer_begin = Counter()
        self.timer_end = Counter()
        self.files = OrderedDict()
        self.file_handles = {}
        self.map_handles = {}
        self.find = Counter()
        self.crt = Counter()
        self.fs_misc = Counter()
        self.registry = Counter()
        self.reg_handles = {}
        self.reg_pending = defaultdict(list)
        self.loadlib = Counter()
        self.hmod = {}
        self.gpa = defaultdict(lambda: {"found": set(), "missing": set()})
        self.com = Counter()
        self.mci = Counter()
        self.audio = Counter()
        self.input = defaultdict(Counter)
        self.windows = Counter()
        self.hooks = Counter()
        self.fpu = Counter()
        self.exits = Counter()
        self.messages = Counter()
        self.com_caller = {}
        self.channels = defaultdict(Counter)
        self.windows_other = Counter()
        self.phases = {}
        self.ts_first = None
        self.ts_last = None
        self.lines = 0
        self.relevant = 0
        self.unlinked = Counter()

    # -- modules --
    def module_size(self, path):
        name = base_name(path)
        if name.lower() not in self.size_cache:
            p = find_file(self.dirs, name)
            try:
                self.size_cache[name.lower()] = read_pe(p)["size"] if p else None
            except (ValueError, OSError, struct.error):
                self.size_cache[name.lower()] = None
        return self.size_cache[name.lower()]

    def add_module(self, m):
        self.mods.append(m)
        self.mod_cache.clear()

    def find_module(self, addr):
        if addr in self.mod_cache:
            return self.mod_cache[addr]
        best = None
        later = sorted((x.base for x in self.mods if not x.unloaded))
        for m in self.mods:
            if m.unloaded or addr < m.base:
                continue
            if m.size:
                end = m.base + m.size
            else:
                nxt = [b for b in later if b > m.base]
                end = min(nxt[0] if nxt else m.base + 0x400000, m.base + 0x400000)
            if addr < end and (best is None or m.base > best.base):
                best = m
        self.mod_cache[addr] = best
        return best

    def mod_name(self, addr):
        m = self.find_module(addr)
        if m:
            return m.name, m.kind
        return ("?x64" if addr >= 0x100000000 else "?"), ("wine" if addr >= 0x100000000 else "unknown")

    def phase_of(self, ts):
        if ts is None or "idle_start" not in self.phases:
            return "all"
        if ts < self.phases["idle_start"]:
            return "startup"
        if "idle_end" in self.phases and ts >= self.phases["idle_end"]:
            return "shutdown"
        return "idle"

    # -- parse --
    def feed(self, line):
        self.lines += 1
        m = LINE.match(line)
        if not m:
            return
        ts = float(m.group("ts")) if m.group("ts") else None
        tid = m.group("tid")
        rest = m.group("rest")
        if ts is not None:
            if self.ts_first is None:
                self.ts_first = ts
            self.ts_last = ts
        if rest.startswith("Call "):
            c = CALL.match(rest)
            if c:
                self.on_call(tid, ts, c)
            elif tid in self.game_tids and rest.startswith("Call window proc "):
                w = WNDPROC.match(rest)
                if w:
                    mn, mk = self.mod_name(int(w.group("proc"), 16))
                    self.messages[(mn, mk, w.group("msg"))] += 1
        elif rest.startswith("Ret  "):
            r = RET.match(rest)
            if r:
                self.on_ret(tid, ts, r)
        elif rest.startswith("Starting thread proc"):
            s = START.match(rest)
            if s:
                self.on_start(tid, ts, s)
        elif "loaddll" in rest:
            self.on_loaddll(tid, ts, rest)
        elif tid in self.game_tids:
            self.on_other(tid, ts, rest)

    def on_loaddll(self, tid, ts, rest):
        mm = LOADED.search(rest)
        if mm:
            base = int(mm.group("base"), 16)
            path = mm.group("path")
            mod = Module(path, base, mm.group("kind"), self.module_size(path), ts, tid)
            if tid not in self.game_tids and mod.name.lower() in ("empire earth.exe", "ee-aoc.exe") or (
                    tid not in self.game_tids and self.root_tid is None and mod.name.lower().endswith(".exe") and mod.kind == "other"
                    and "windows" not in mod.path.lower()):
                self.root_tid = tid
                self.game_tids.add(tid)
                for old in self.pending_loads.pop(tid, []):
                    self.add_module(old)
            if tid in self.game_tids:
                self.add_module(mod)
            else:
                self.pending_loads[tid].append(mod)
            return
        mm = UNLOADED.search(rest)
        if mm and tid in self.game_tids:
            name = base_name(mm.group("path"))
            for m in reversed(self.mods):
                if m.name.lower() == name.lower() and not m.unloaded:
                    m.unloaded = True
                    self.mod_cache.clear()
                    break

    def on_start(self, tid, ts, s):
        if tid in self.game_tids:
            return
        proc, arg = int(s.group("proc"), 16), int(s.group("arg"), 16)
        for p in self.pending_create:
            if p["start"] == proc and p["arg"] == arg and p["tid"] is None:
                self.game_tids.add(tid)
                th = self.threads.setdefault(tid, self.new_thread(tid))
                th.update({
                    "creator_tid": p["creator_tid"], "creator_module": p["creator_module"], "creator_kind": p["creator_kind"],
                    "via": p["via"], "start_addr": "0x%x" % p["real_start"], "arg": "0x%x" % arg,
                    "created_suspended": p["suspended"], "first_ts": ts,
                })
                sm = self.find_module(p["real_start"])
                th["start_module"] = sm.name if sm else self.mod_name(p["real_start"])[0]
                th["start_kind"] = sm.kind if sm else self.mod_name(p["real_start"])[1]
                th["start_rva"] = "0x%x" % (p["real_start"] - sm.base) if sm else None
                p["tid"] = tid
                return

    def new_thread(self, tid):
        return {"tid": tid, "creator_tid": None, "creator_module": None, "creator_kind": None, "via": "CreateThread",
                "start_module": None, "start_kind": None, "start_rva": None, "start_addr": None, "arg": None,
                "created_suspended": False, "priorities": [], "priorities_host": [], "first_ts": None}

    def on_call(self, tid, ts, c):
        if tid not in self.game_tids:
            return
        f = Frame()
        f.dll = c.group("dll").lower()
        if f.dll.endswith(".dll"):
            f.dll = f.dll[:-4]
        f.fn = c.group("fn")
        f.args = parse_args(c.group("args"))
        f.ret = int(c.group("ret"), 16)
        f.tid = tid
        f.ts = ts
        f.extra = {}
        f.mod = self.mod_name(f.ret)
        self.relevant += 1
        self.stacks[tid].append(f)
        if tid not in self.threads:
            th = self.threads[tid] = self.new_thread(tid)
            if tid == self.root_tid:
                th["start_module"] = "main thread"
        e = self.calls[f.dll][f.fn]
        e["total"] += 1
        e["by_module"][f.mod[0]] += 1
        e["phase"][self.phase_of(ts)][f.mod[1]] += 1
        e["by_kind"][f.mod[1]] += 1
        if f.mod[1] == "game":
            e["game"] += 1
        self.thread_top[tid][f.dll + "." + f.fn] += 1
        if AUDIO.match(f.fn):
            self.audio[f.fn] += 1
        h = getattr(self, "call_" + f.fn.lower(), None)
        if h:
            h(f)

    def on_ret(self, tid, ts, r):
        st = self.stacks.get(tid)
        if not st:
            return
        fn = r.group("fn").lower()
        while st:
            f = st.pop()
            if f.fn.lower() == fn:
                h = getattr(self, "ret_" + fn, None)
                if h:
                    h(f, int(r.group("rv"), 16))
                return

    def on_other(self, tid, ts, rest):
        cm = COM_LINE.match(rest)
        if cm:
            who = self.com_caller.pop(tid, ("?", "unknown"))
            self.com[(cm.group(1).lower(), who[0], who[1])] += 1
        ch = CHANNEL.match(rest)
        if ch:
            norm = re.sub(r"\b(?:0x)?[0-9a-fA-F]{6,16}\b", "<hex>", ch.group("msg"))[:200]
            self.channels[ch.group("chan")][norm] += 1

    # -- helpers --
    def is_game(self, f):
        return f.mod[1] == "game"

    # -- threads --
    def call_createthread(self, f):
        start = f.val(2)
        real, via, cm, ck = start, "CreateThread", f.mod[0], f.mod[1]
        lb = self.last_begin.pop(f.tid, None)
        if lb and f.mod[0].lower() == "msvcrt.dll":
            real, via, cm, ck = lb["start"], "msvcrt._beginthreadex", lb["module"], lb["kind"]
        p = {"creator_tid": f.tid, "creator_module": cm, "creator_kind": ck, "start": start, "real_start": real, "arg": f.val(3),
             "suspended": bool(f.val(4) & 4), "via": via, "tid": None, "handle": None}
        f.extra["create"] = p
        self.pending_create.append(p)

    def ret_createthread(self, f, rv):
        p = f.extra.get("create")
        if p is not None and rv:
            p["handle"] = rv
            self.handles[rv] = p  # the thread's own id is filled in when its first line shows up (it may start after a resume)

    def call__beginthreadex(self, f):
        self.last_begin[f.tid] = {"start": f.val(2), "module": f.mod[0], "kind": f.mod[1]}

    def target_of(self, f, handle):
        """The thread a handle names: a tid (pseudo handles = the calling thread), the creation record (its tid may still be
        unknown because the thread has not started yet), or None."""
        if handle in (0xFFFFFFFE, 0xFFFFFFFF):
            return f.tid
        return self.handles.get(handle)

    def call_setthreadpriority(self, f):
        self.prio_events.append((self.target_of(f, f.val(0)), sgn(f.val(1)), f.mod[1]))

    def _thread_op(self, f):
        self.thread_ops.append((f.fn, f.mod[0], f.tid, self.target_of(f, f.val(0))))

    call_suspendthread = call_resumethread = call_terminatethread = _thread_op

    def call_exitthread(self, f):
        self.exits[("ExitThread", f.mod[0], f.tid)] += 1

    def call_exitprocess(self, f):
        self.exits[("ExitProcess", f.mod[0], f.tid)] += 1

    def call_terminateprocess(self, f):
        self.exits[("TerminateProcess", f.mod[0], f.tid)] += 1

    # -- timers --
    def call_timesetevent(self, f):
        cb = f.val(2)
        cm = self.find_module(cb)
        self.timer_set[(f.val(0), f.val(1), f.val(4), cm.name if cm else "?", "0x%x" % (cb - cm.base) if cm else "0x%x" % cb, f.mod[0])] += 1

    def call_timebeginperiod(self, f):
        self.timer_begin[(f.val(0), f.mod[0])] += 1

    def call_timeendperiod(self, f):
        self.timer_end[(f.val(0), f.mod[0])] += 1

    # -- files --
    def file_entry(self, path, fn):
        key = path.lower()
        return self.files.setdefault(key, {
            "path": path, "fn": fn, "access": None, "disposition": None, "open_count": 0, "open_ok": False, "open_fail": 0,
            "callers": set(), "reads": 0, "read_bytes": 0, "writes": 0, "write_bytes": 0, "mapped_bytes": 0, "maps": 0, "seeks": 0,
        })

    def _open(self, f):
        path = f.text(0)
        if path is None:
            return
        acc = f.val(1)
        e = self.file_entry(path, f.fn)
        e["access"] = {0x80000000: "read", 0x40000000: "write", 0xC0000000: "read/write"}.get(acc, "0x%x" % acc)
        e["disposition"] = DISPOSITION.get(f.val(4), "0x%x" % f.val(4))
        e["callers"].add(f.mod[0])
        e["open_count"] += 1
        f.extra["file"] = e

    call_createfilea = call_createfilew = _open

    def _open_ret(self, f, rv):
        e = f.extra.get("file")
        if e is None:
            return
        if rv in (0xFFFFFFFF, 0):
            e["open_fail"] += 1
        else:
            e["open_ok"] = True
            self.file_handles[rv] = e

    ret_createfilea = ret_createfilew = _open_ret

    def call_readfile(self, f):
        e = self.file_handles.get(f.val(0))
        if e:
            e["reads"] += 1
            e["read_bytes"] += f.val(2)

    def call_writefile(self, f):
        e = self.file_handles.get(f.val(0))
        if e:
            e["writes"] += 1
            e["write_bytes"] += f.val(2)

    def call_setfilepointer(self, f):
        e = self.file_handles.get(f.val(0))
        if e:
            e["seeks"] += 1

    def call_closehandle(self, f):
        self.file_handles.pop(f.val(0), None)
        self.map_handles.pop(f.val(0), None)
        self.handles.pop(f.val(0), None)

    def call_createfilemappinga(self, f):
        f.extra["map_of"] = self.file_handles.get(f.val(0))

    call_createfilemappingw = call_createfilemappinga

    def ret_createfilemappinga(self, f, rv):
        if rv and f.extra.get("map_of") is not None:
            self.map_handles[rv] = f.extra["map_of"]

    ret_createfilemappingw = ret_createfilemappinga

    def call_mapviewoffile(self, f):
        e = self.map_handles.get(f.val(0))
        if e:
            e["maps"] += 1
            e["mapped_bytes"] += f.val(4)

    def _find(self, f):
        p = f.text(0)
        if p is not None:
            self.find[(f.fn, p, f.mod[0])] += 1

    call_findfirstfilea = call_findfirstfilew = call_findfirstfileexa = call_findfirstfileexw = _find

    def _crt(self, f):
        p = f.text(0)
        if p is not None:
            self.crt[(f.fn, p, f.text(1) or "", f.mod[0])] += 1

    call_fopen = call__wfopen = call__stat = call__chmod = call_remove = call__utime = call__fullpath = call__open = _crt

    def _misc(self, f):
        p = f.text(0)
        if p is not None:
            self.fs_misc[(f.fn, p, f.mod[0])] += 1

    call_createdirectorya = call_removedirectorya = call_deletefilea = call_copyfilea = call_movefilea = call_setfileattributesa = _misc
    call_getfileattributesa = call_getfileattributesexa = call_gettemppatha = call_gettempfilenamea = call_setcurrentdirectorya = _misc
    call_getshortpathnamea = call_getdiskfreespacea = call_getvolumeinformationa = _misc
    call_getfileversioninfoa = call_getfileversioninfosizea = _misc

    def call_createprocessa(self, f):  # (application, command line, ...): the command line carries the program when the first is null
        p = f.text(0) or f.text(1)
        if p is not None:
            self.fs_misc[(f.fn, p, f.mod[0])] += 1

    def call_shellexecutea(self, f):  # (hwnd, operation, file, parameters, directory, show)
        p = f.text(2)
        if p is not None:
            self.fs_misc[(f.fn, p, f.mod[0])] += 1

    # -- registry --
    def key_of(self, f, handle, tid):
        if handle in ROOTS:
            return ROOTS[handle]
        if handle in self.reg_handles:
            return self.reg_handles[handle]
        pend = self.reg_pending[tid]
        if pend:
            self.reg_handles[handle] = pend.pop()
            return self.reg_handles[handle]
        return "<handle 0x%x>" % handle

    def _reg_open(self, f):
        parent = self.key_of(f, f.val(0), f.tid)
        sub = f.text(1) or ""
        f.extra["reg"] = (parent + ("\\" + sub if sub else ""))

    call_regopenkeyexa = call_regcreatekeyexa = call_regopenkeyexw = call_regcreatekeyexw = _reg_open

    def _reg_open_ret(self, f, rv):
        key = f.extra.get("reg")
        self.registry[(f.fn, key, None, rv == 0, rv, f.mod[0], None)] += 1
        if rv == 0:
            self.reg_pending[f.tid].append(key)

    ret_regopenkeyexa = ret_regcreatekeyexa = ret_regopenkeyexw = ret_regcreatekeyexw = _reg_open_ret

    def _reg_value(self, f):
        f.extra["reg"] = self.key_of(f, f.val(0), f.tid)
        f.extra["val"] = f.text(1)
        if f.fn.lower().startswith("regsetvalueex"):
            f.extra["data"] = f.text(4)
            f.extra["type"] = f.val(3)

    call_regqueryvalueexa = call_regsetvalueexa = call_regdeletevaluea = call_regqueryvalueexw = call_regsetvalueexw = _reg_value

    def _reg_value_ret(self, f, rv):
        data = f.extra.get("data")
        if data is None and f.extra.get("type") is not None:
            data = "type %d" % f.extra["type"]
        self.registry[(f.fn, f.extra.get("reg"), f.extra.get("val") or "", rv == 0, rv, f.mod[0], data)] += 1

    ret_regqueryvalueexa = ret_regsetvalueexa = ret_regdeletevaluea = ret_regqueryvalueexw = ret_regsetvalueexw = _reg_value_ret

    def call_regdeletekeya(self, f):
        f.extra["reg"] = self.key_of(f, f.val(0), f.tid) + "\\" + (f.text(1) or "")

    def ret_regdeletekeya(self, f, rv):
        self.registry[(f.fn, f.extra.get("reg"), None, rv == 0, rv, f.mod[0], None)] += 1

    def call_regclosekey(self, f):
        self.reg_handles.pop(f.val(0), None)

    # -- dynamic loading --
    def _loadlib(self, f):
        f.extra["path"] = f.text(0)

    call_loadlibrarya = call_loadlibraryexa = call_loadlibraryw = call_loadlibraryexw = _loadlib

    def _loadlib_ret(self, f, rv):
        p = f.extra.get("path")
        if p is None:
            return
        self.loadlib[(p, f.fn, f.mod[0], rv != 0)] += 1
        if rv:
            self.hmod[rv] = base_name(p)

    ret_loadlibrarya = ret_loadlibraryexa = ret_loadlibraryw = ret_loadlibraryexw = _loadlib_ret

    def call_getprocaddress(self, f):
        h = f.val(0)
        m = self.find_module(h)
        mod = (m.name if m and m.base == h else self.hmod.get(h)) or "0x%x" % h
        a = f.arg(1)
        f.extra["gpa"] = (mod, a.str if a.str is not None else "#%d" % (a.val or 0))

    def ret_getprocaddress(self, f, rv):
        g = f.extra.get("gpa")
        if g:
            self.gpa[g[0]]["found" if rv else "missing"].add(g[1])

    # -- COM: the ole channel prints the CLSID on the line after the call, on the same thread --
    def call_cocreateinstance(self, f):
        self.com_caller[f.tid] = f.mod

    call_cogetclassobject = call_cocreateinstance

    # -- sound, input, windows, math --
    def call_mcisendcommanda(self, f):
        self.mci[f.val(1)] += 1

    call_mcisendcommandw = call_mcisendcommanda

    def _audio(self, f):
        self.audio[f.fn] += 1

    def call_directinputcreatea(self, f):
        self.input[f.fn][("0x%x" % f.val(1), f.mod[0])] += 1

    call_directinputcreatew = call_directinputcreateex = call_directinput8create = call_directinputcreatea

    def call_createwindowexa(self, f):
        self.windows[(f.text(1) or "", f.text(2) or "", f.val(3), f.val(0), sgn(f.val(4)), sgn(f.val(5)), sgn(f.val(6)), sgn(f.val(7)), f.mod[0])] += 1

    def call_setwindowshookexa(self, f):
        pm = self.find_module(f.val(1))
        self.hooks[(sgn(f.val(0)), pm.name if pm else "?", f.mod[0])] += 1

    def _fpu(self, f):
        self.fpu[(tuple("%08x" % f.val(i) for i in range(2)), f.mod[0])] += 1

    call__controlfp = call__control87 = call__fpreset = _fpu


# ---- summary ----------------------------------------------------------------------------------------------------------------------

def read_phases(path):
    ph = {}
    if path and os.path.exists(path):
        for line in open(path):
            p = line.split()
            if len(p) >= 3:
                try:
                    ph[p[0]] = float(p[2])
                except ValueError:
                    pass
    return ph


def group_ok(call_dll, imp_dll):
    if call_dll == imp_dll:
        return True
    return any(call_dll in g and imp_dll in g for g in ALIAS_GROUPS)


def finish(s):
    d = OrderedDict()
    idle_s = None
    if "idle_start" in s.phases and "idle_end" in s.phases:
        idle_s = round(s.phases["idle_end"] - s.phases["idle_start"], 3)
    d["meta"] = {"lines": s.lines, "traced_calls": s.relevant, "first_ts": s.ts_first, "last_ts": s.ts_last, "phases": s.phases,
                 "profile": s.lists["profile"], "root_tid": s.root_tid, "idle_seconds": idle_s,
                 "n_include": s.lists["n_include"]}
    d["modules"] = [{"name": m.name, "path": m.path, "base": "0x%x" % m.base, "size": m.size, "kind": m.kind, "flag": m.flag,
                     "load_tid": m.tid, "t": m.ts, "unloaded": m.unloaded} for m in s.mods]
    for tgt, prio, kind in s.prio_events:
        tid = tgt["tid"] if isinstance(tgt, dict) else tgt
        if tid in s.threads:
            s.threads[tid]["priorities" if kind in ("game", "compat") else "priorities_host"].append(prio)
    distinct = OrderedDict()
    for m in s.mods:
        e = distinct.setdefault(m.name.lower(), {"name": m.name, "kind": m.kind, "loads": 0, "bases": [], "size": m.size})
        e["loads"] += 1
        if "0x%x" % m.base not in e["bases"]:
            e["bases"].append("0x%x" % m.base)
    d_modules_distinct = list(distinct.values())
    th = []
    for tid, t in s.threads.items():
        t = dict(t)
        t["calls"] = sum(s.thread_top[tid].values())
        t["top_calls"] = s.thread_top[tid].most_common(6)
        t["calls_detail"] = dict(s.thread_top[tid])
        th.append(t)
    d["modules_distinct"] = d_modules_distinct
    d["threads"] = th
    ops = Counter()
    for fn, mod, ctid, tgt in s.thread_ops:
        ops[(fn, mod, ctid, tgt["tid"] if isinstance(tgt, dict) else tgt)] += 1
    d["thread_ops"] = [{"fn": k[0], "caller_module": k[1], "caller_tid": k[2], "target_tid": k[3], "count": n}
                       for k, n in sorted(ops.items(), key=lambda kv: (kv[0][0], kv[0][1], kv[0][2], str(kv[0][3])))]
    d["exits"] = [{"fn": k[0], "caller_module": k[1], "caller_tid": k[2], "count": n} for k, n in s.exits.items()]
    rates = {}
    for fn in ("QueryPerformanceCounter", "GetTickCount", "timeGetTime", "GetSystemTimeAsFileTime", "GetLocalTime", "GetSystemTime",
               "Sleep", "WaitForSingleObject", "WaitForMultipleObjects", "PeekMessageA", "GetMessageA"):
        for dll, fns in s.calls.items():
            if fn in fns:
                e = fns[fn]
                r = rates.setdefault(fn, {"total": 0, "by_module": Counter(), "startup": Counter(), "idle": Counter(), "shutdown": Counter(),
                                          "all": Counter()})
                r["total"] += e["total"]
                r["by_module"].update(e["by_module"])
                for ph, c in e["phase"].items():
                    r[ph].update(c)
    for fn, r in rates.items():
        r["by_module"] = dict(r["by_module"].most_common(8))
        for ph in ("startup", "idle", "shutdown", "all"):
            r[ph] = dict(r[ph])
        r["idle_seconds"] = idle_s
        if idle_s:
            r["idle_per_second_game"] = round(r["idle"].get("game", 0) / idle_s, 2)
            r["idle_per_second_all"] = round(sum(r["idle"].values()) / idle_s, 2)
    d["timers"] = {
        "timeSetEvent": [{"delay": k[0], "resolution": k[1], "flags": k[2], "callback_module": k[3], "callback_rva": k[4],
                          "caller_module": k[5], "count": n} for k, n in s.timer_set.items()],
        "timeBeginPeriod": [{"value": k[0], "caller_module": k[1], "count": n} for k, n in s.timer_begin.items()],
        "timeEndPeriod": [{"value": k[0], "caller_module": k[1], "count": n} for k, n in s.timer_end.items()],
        "rates": rates,
    }
    files = []
    for e in s.files.values():
        e = dict(e)
        e["callers"] = sorted(e["callers"])
        files.append(e)
    d["files"] = {
        "opened": files,
        "find": [{"fn": k[0], "pattern": k[1], "caller_module": k[2], "count": n} for k, n in s.find.items()],
        "crt": [{"fn": k[0], "path": k[1], "mode": k[2], "caller_module": k[3], "count": n} for k, n in s.crt.items()],
        "misc": [{"fn": k[0], "path": k[1], "caller_module": k[2], "count": n} for k, n in s.fs_misc.items()],
    }
    d["registry"] = [{"op": k[0], "key": k[1], "value": k[2], "ok": k[3], "error": k[4], "caller_module": k[5], "data": k[6], "count": n}
                     for k, n in s.registry.items()]
    d["dynamic"] = {
        "loadlibrary": [{"path": k[0], "fn": k[1], "caller_module": k[2], "ok": k[3], "count": n} for k, n in s.loadlib.items()],
        "getprocaddress": {m: {"found": sorted(v["found"]), "missing": sorted(v["missing"])} for m, v in s.gpa.items()},
    }
    d["com"] = [{"clsid": k[0], "name": KNOWN_CLSIDS.get(k[0]), "caller_module": k[1], "kind": k[2], "count": n}
                for k, n in sorted(s.com.items(), key=lambda kv: (-kv[1], kv[0]))]
    d["mci"] = [{"command": MCI.get(c, "0x%x" % c), "code": "0x%x" % c, "count": n} for c, n in sorted(s.mci.items())]
    d["audio"] = dict(s.audio)
    d["input"] = {fn: [{"version": k[0], "caller_module": k[1], "count": n} for k, n in c.items()] for fn, c in s.input.items()}
    d["windows"] = {
        "created": [{"class": k[0], "title": k[1], "style": "0x%x" % k[2], "ex_style": "0x%x" % k[3], "x": k[4], "y": k[5],
                     "width": k[6], "height": k[7], "caller_module": k[8], "count": n} for k, n in s.windows.items()],
        "hooks": [{"id": k[0], "proc_module": k[1], "caller_module": k[2], "count": n} for k, n in s.hooks.items()],
    }
    d["messages"] = [{"module": k[0], "kind": k[1], "msg": k[2], "count": n} for k, n in s.messages.most_common()]
    d["fpu"] = [{"args": list(k[0]), "caller_module": k[1], "count": n} for k, n in s.fpu.items()]
    d["channels"] = {c: [[m, n] for m, n in cnt.most_common(60)] for c, cnt in s.channels.items()}
    # per DLL: every traced function with its totals
    d["calls"] = {dll: {fn: {"total": e["total"], "game": e["game"], "by_module": dict(e["by_module"].most_common(5)), "by_kind": dict(e["by_kind"]),
                             "startup": sum(e["phase"].get("startup", Counter()).values()), "idle": sum(e["phase"].get("idle", Counter()).values()),
                             "shutdown": sum(e["phase"].get("shutdown", Counter()).values())}
                        for fn, e in sorted(fns.items())} for dll, fns in sorted(s.calls.items())}
    # imported versus called
    usage = OrderedDict()
    called_game = defaultdict(set)
    called_any = defaultdict(set)
    called_compat = defaultdict(set)  # the 00xJS shim / ddraw proxy forward some of the game's calls (CreateWindowExA, LoadLibraryExA, ...)
    for dll, fns in s.calls.items():
        for fn, e in fns.items():
            called_any[dll].add(fn.lower())
            if e["game"]:
                called_game[dll].add(fn.lower())
            elif e["by_kind"].get("compat"):
                called_compat[dll].add(fn.lower())
    traced = s.lists["traced"]
    for dll, names in s.imports.items():
        imported = [n for n in names if not n.startswith("#")]
        cg = [n for n in imported if any(group_ok(cd, dll) and n.lower() in fs for cd, fs in called_game.items())]
        cc = [n for n in imported if n not in cg and any(group_ok(cd, dll) and n.lower() in fs for cd, fs in called_compat.items())]
        ca = [n for n in imported if any(group_ok(cd, dll) and n.lower() in fs for cd, fs in called_any.items())]
        tr = set(traced.get(dll, []))
        wild = "*" in tr
        not_traced = [n for n in imported if n not in cg and n.lower() not in tr and not wild]
        dead = [n for n in imported if n not in cg and n not in cc and n not in not_traced]
        called_names = {n.lower() for n in imported}
        extra = sorted({fn for cd, fs in called_game.items() if group_ok(cd, dll) for fn in fs if fn not in called_names})
        usage[dll] = {"imported": len(names), "called_by_game": len(cg), "called_via_compat": cc, "called_total": len(ca), "dead": dead,
                      "not_traced": not_traced, "called_not_imported": extra}
    d["usage"] = usage
    d["include"] = s.lists["include"]
    return d


# ---- tables.md -----------------------------------------------------------------------------------------------------------------------

def md_table(head, rows):
    if not rows:
        return "(none observed)\n"
    out = ["| " + " | ".join(head) + " |", "|" + "|".join("---" for _ in head) + "|"]
    for r in rows:
        out.append("| " + " | ".join(str(x).replace("|", "\\|") for x in r) + " |")
    return "\n".join(out) + "\n"


def render(d):
    o = []
    m = d["meta"]
    o.append("# Runtime Win32 inventory: tables (generated by tools/game_run/scripts/ri.py)\n")
    o.append("Log lines: %d; traced calls by the game's threads: %d; profile: %s (%d relay entries); phases (Wine ticks): %s\n"
             % (m["lines"], m["traced_calls"], m["profile"], m["n_include"], json.dumps(m["phases"])))
    o.append("\n## 1. Threads\n")
    rows = []
    for t in d["threads"]:
        rows.append([t["tid"], t["creator_tid"] or "-", "%s (%s)" % (t["creator_module"] or "-", t["creator_kind"] or "-"), t["via"],
                     "%s %s" % (t["start_module"], t["start_rva"] or ""), "yes" if t["created_suspended"] else "no",
                     ",".join(str(p) for p in t["priorities"]) or "-", t["calls"],
                     ", ".join("%s x%d" % kv for kv in t["top_calls"][:3])])
    o.append(md_table(["tid", "created by tid", "created by module", "via", "start (module rva)", "suspended", "priority set", "traced calls", "top calls"], rows))
    o.append("\nSuspendThread / ResumeThread / TerminateThread / exits:\n\n")
    o.append(md_table(["call", "caller module", "caller tid", "target tid", "count"],
                      [[x["fn"], x["caller_module"], x["caller_tid"], x["target_tid"] or "?", x["count"]] for x in d["thread_ops"]] +
                      [[x["fn"], x["caller_module"], x["caller_tid"], "-", x["count"]] for x in d["exits"]]))
    o.append("\n## 2. Timers\n")
    o.append("timeSetEvent:\n\n" + md_table(["delay ms", "resolution", "flags", "callback", "caller", "count"],
                                              [[x["delay"], x["resolution"], "0x%x" % x["flags"], "%s %s" % (x["callback_module"], x["callback_rva"]),
                                                x["caller_module"], x["count"]] for x in d["timers"]["timeSetEvent"]]))
    o.append("\ntimeBeginPeriod / timeEndPeriod:\n\n" + md_table(["call", "value ms", "caller", "count"],
                                                                 [["timeBeginPeriod", x["value"], x["caller_module"], x["count"]] for x in d["timers"]["timeBeginPeriod"]] +
                                                                 [["timeEndPeriod", x["value"], x["caller_module"], x["count"]] for x in d["timers"]["timeEndPeriod"]]))
    o.append("\nCall counts of the timing and polling functions (by caller kind; startup / idle / shutdown):\n\n")
    rr = []
    for fn, r in d["timers"]["rates"].items():
        rr.append([fn, r["total"], json.dumps(r["startup"]), json.dumps(r["idle"]), json.dumps(r["shutdown"]), r.get("idle_per_second_game", "-"),
                   json.dumps(r["by_module"])])
    o.append(md_table(["function", "total", "startup", "idle", "shutdown", "idle/s (game)", "callers"], rr))
    o.append("\n## 3. Files\n")
    o.append(md_table(["path", "first fn", "access", "disposition", "opens (ok/failed)", "reads (bytes asked)", "writes (bytes)", "mapped views (bytes)", "callers"],
                      [[e["path"], e["fn"], e["access"], e["disposition"], "%d (%s/%d)" % (e["open_count"], "ok" if e["open_ok"] else "0", e["open_fail"]),
                        "%d (%d)" % (e["reads"], e["read_bytes"]), "%d (%d)" % (e["writes"], e["write_bytes"]),
                        "%d (%d)" % (e["maps"], e["mapped_bytes"]), ", ".join(e["callers"])] for e in d["files"]["opened"]]))
    o.append("\nFindFirstFile patterns:\n\n" + md_table(["fn", "pattern", "caller", "count"], [[x["fn"], x["pattern"], x["caller_module"], x["count"]] for x in d["files"]["find"]]))
    o.append("\nC runtime file calls:\n\n" + md_table(["fn", "path", "mode", "caller", "count"], [[x["fn"], x["path"], x["mode"], x["caller_module"], x["count"]] for x in d["files"]["crt"]]))
    o.append("\nOther file-system and process calls with a path:\n\n" + md_table(["fn", "path", "caller", "count"], [[x["fn"], x["path"], x["caller_module"], x["count"]] for x in d["files"]["misc"]]))
    o.append("\n## 4. Registry\n")
    o.append(md_table(["op", "key", "value", "ok (error)", "data", "caller", "count"],
                      [[x["op"], x["key"], x["value"] or "", "yes" if x["ok"] else "no (%d)" % x["error"], x["data"] or "", x["caller_module"], x["count"]] for x in d["registry"]]))
    o.append("\n## 5. Modules\n")
    o.append(md_table(["module", "kind", "loads", "bases", "size"],
                      [[x["name"], x["kind"], x["loads"], ", ".join(x["bases"][:3]), x["size"] or "?"] for x in d["modules_distinct"]]))
    o.append("\nLoadLibrary calls:\n\n" + md_table(["path", "fn", "caller", "ok", "count"], [[x["path"], x["fn"], x["caller_module"], x["ok"], x["count"]] for x in d["dynamic"]["loadlibrary"]]))
    o.append("\nGetProcAddress by module:\n\n" + md_table(["module", "found", "missing"], [[k, ", ".join(v["found"]), ", ".join(v["missing"])] for k, v in sorted(d["dynamic"]["getprocaddress"].items())]))
    o.append("\nCOM CLSIDs (+ole channel), by the traced caller of CoCreateInstance / CoGetClassObject (? = not a traced caller: Wine's own):\n\n" +
             md_table(["clsid", "name", "caller", "kind", "count"], [[x["clsid"], x["name"] or "", x["caller_module"], x["kind"], x["count"]] for x in d["com"]]))
    o.append("\n## 6. Windows, input, audio\n")
    o.append(md_table(["class", "title", "style", "ex style", "x,y", "size", "caller", "count"],
                      [[x["class"], x["title"], x["style"], x["ex_style"], "%d,%d" % (x["x"], x["y"]), "%dx%d" % (x["width"], x["height"]), x["caller_module"], x["count"]] for x in d["windows"]["created"]]))
    o.append("\nWindow messages delivered to window procedures, by the module that owns the procedure:\n\n" +
             md_table(["module", "kind", "message", "count"], [[x["module"], x["kind"], x["msg"], x["count"]] for x in d["messages"][:40]]))
    o.append("\nHooks: " + json.dumps(d["windows"]["hooks"]) + "\n")
    o.append("\nDirectInput: " + json.dumps(d["input"]) + "\n")
    o.append("\nMCI commands: " + json.dumps(d["mci"]) + "\n")
    o.append("\nwaveOut / mixer / aux / DirectSound entry points: " + json.dumps(d["audio"]) + "\n")
    o.append("\nx87 control word calls: " + json.dumps(d["fpu"]) + "\n")
    for c, rows in sorted(d["channels"].items()):
        if c in ("dsound", "dinput", "mci", "winmm", "ole", "fixme", "err"):
            o.append("\nChannel %s (message shape, count):\n\n" % c + md_table(["message", "count"], rows[:25]))
    o.append("\n## 7. Imported versus called\n")
    o.append(md_table(["DLL", "imported (distinct, all game binaries)", "called by game modules", "called only through the compat shim", "called by anyone", "dead (traced, never called)", "not traced (hot or timer)", "called but not imported"],
                      [[k, v["imported"], v["called_by_game"], len(v["called_via_compat"]), v["called_total"], len(v["dead"]), len(v["not_traced"]), len(v["called_not_imported"])] for k, v in d["usage"].items()]))
    for k, v in d["usage"].items():
        if v["dead"]:
            o.append("\nDead imports, %s: %s\n" % (k, ", ".join(v["dead"])))
    return "".join(o)


def summarize(args):
    if not os.path.exists(args.log):
        print("no such log: %s" % args.log, file=sys.stderr)
        return 2
    s = Summarizer(args.game_dir, args.wine_root, args.profile)
    s.phases = read_phases(args.phases)
    with open(args.log, "r", errors="replace") as f:
        for line in f:
            s.feed(line.rstrip("\n"))
    if s.relevant == 0 and not s.mods:
        print("the log has no relay or loaddll lines of a game process: %s" % args.log, file=sys.stderr)
        return 2
    d = finish(s)
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "summary.json"), "w") as f:
        json.dump(d, f, indent=1, sort_keys=False, default=str)
    with open(os.path.join(args.out, "tables.md"), "w") as f:
        f.write(render(d))
    print("summary: %d lines, %d traced calls, %d threads, %d modules -> %s" % (s.lines, s.relevant, len(d["threads"]), len(d["modules"]), args.out))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("lists")
    a.add_argument("--game-dir", required=True)
    a.add_argument("--wine-root", required=True)
    a.add_argument("--profile", default="broad")
    a.add_argument("--existing-exclude", default="")
    a.add_argument("--existing-fromexclude", default="")
    b = sub.add_parser("summarize")
    b.add_argument("--log", required=True)
    b.add_argument("--game-dir", required=True)
    b.add_argument("--wine-root", required=True)
    b.add_argument("--phases", default="")
    b.add_argument("--profile", default="broad")
    b.add_argument("--out", required=True)
    args = ap.parse_args(argv)
    if args.cmd == "lists":
        try:
            r = build_lists(args.game_dir, args.wine_root, args.profile, args.existing_exclude, args.existing_fromexclude)
        except ValueError as e:
            print(str(e), file=sys.stderr)
            return 2
        json.dump(r, sys.stdout)
        sys.stdout.write("\n")
        return 0
    return summarize(args)


if __name__ == "__main__":
    sys.exit(main())
