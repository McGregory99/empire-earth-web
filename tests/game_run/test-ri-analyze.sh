#!/usr/bin/env bash
# Tests of tools/game_run/scripts/ri.py, the PE reader, the Wine relay-list builder and the relay-log summarizer used by the
# runtime inventory (runtime-inventory.sh).  Fixtures: minimal PE32 files and a synthetic +relay log (ri_fixture.py); nothing
# here touches the game, Wine or the prefix.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/world.sh"
RI="$(cd "$HERE/../../tools/game_run/scripts" && pwd)/ri.py"
T="$(mktemp -d "${TMPDIR:-/tmp}/ri test.XXXXXX")"
trap 'rm -rf "$T"' EXIT

python3 -I "$HERE/ri_fixture.py" "$T" || { echo "FAIL: cannot write the fixtures"; exit 2; }
GAME="$T/game"
WINE="$T/wine"
ri() { python3 -I "$RI" "$@"; }
# py <summary or lists json file> <python expression over d>: exit status 0 when it is true
py() { python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); sys.exit(0 if eval(sys.argv[2]) else 1)' "$1" "$2"; }

STOCK_EXCLUDE='ntdll.RtlEnterCriticalSection;kernel32.TlsGetValue;kernel32.SetLastError'
STOCK_FROM='winex11.drv;winemac.drv;user32;gdi32;advapi32;kernel32'

# ---- 1. the lists ------------------------------------------------------------------------------------------------------------
check "lists: the script exists" test -f "$RI"
ri lists --game-dir "$GAME" --wine-root "$WINE" --profile broad --existing-exclude "$STOCK_EXCLUDE" --existing-fromexclude "$STOCK_FROM" \
  >"$T/lists.json" 2>"$T/lists.err"
check_eq "lists: broad profile exits 0" "0" "$?"
check "lists: the game's kernel32 imports are traced" py "$T/lists.json" "'kernel32.CreateThread' in d['include'].split(';') and 'kernel32.SuspendThread' in d['include'].split(';')"
check "lists: advapi32 and user32 imports are traced" py "$T/lists.json" "'advapi32.RegOpenKeyExA' in d['include'].split(';') and 'user32.SetWindowsHookExA' in d['include'].split(';')"
check "lists: wsock32 ordinals become names through Wine's export table" py "$T/lists.json" "'wsock32.WSAStartup' in d['include'].split(';') and 'wsock32.socket' in d['include'].split(';')"
check "lists: only the cold msvcrt functions are traced" py "$T/lists.json" "'msvcrt.fopen' in d['include'].split(';') and 'msvcrt._controlfp' in d['include'].split(';') and 'msvcrt.memcpy' not in d['include'].split(';') and 'msvcrt.malloc' not in d['include'].split(';')"
check "lists: a hot function is left out (EnterCriticalSection, GetLastError)" py "$T/lists.json" "not ({'kernel32.EnterCriticalSection','kernel32.GetLastError','ntdll.RtlEnterCriticalSection'} & set(d['include'].split(';')))"
check "lists: the timer functions are left out of the broad profile" py "$T/lists.json" "not ({'kernel32.QueryPerformanceCounter','winmm.timeGetTime'} & set(d['include'].split(';')))"
check "lists: engine-private imports (low-level engine.dll) are not Win32" py "$T/lists.json" "not any(e.startswith('low-level') for e in d['include'].split(';'))"
check "lists: the compatibility shim's imports do not count (VirtualProtect)" py "$T/lists.json" "'kernel32.VirtualProtect' not in d['include'].split(';')"
check "lists: the extras are there (ExitProcess, the dsound wildcard)" py "$T/lists.json" "'kernel32.ExitProcess' in d['include'].split(';') and 'dsound.*' in d['include'].split(';')"
check "lists: every waveOut/midiOut/mixer/aux entry point is traced, whoever imports it (Wine's dsound calls waveOutGetPosition)" py "$T/lists.json" "{'winmm.waveOutGetPosition','winmm.waveOutMessage','winmm.waveOutGetNumDevs','winmm.midiOutGetNumDevs','winmm.mixerGetID','winmm.auxOutMessage'} <= set(d['include'].split(';'))"
check "lists: Wine's dsound and dinput are not hidden as callers (their calls into winmm show who drives the audio)" py "$T/lists.json" "not ({'dsound','dinput','dinput8'} & set(d['fromexclude'].split(';')))"
check "lists: the exclude list keeps the stock entries and adds the hot ones" py "$T/lists.json" "d['exclude'].split(';')[:3]==['ntdll.RtlEnterCriticalSection','kernel32.TlsGetValue','kernel32.SetLastError'] and 'kernel32.GetLastError' in d['exclude'].split(';')"
check "lists: the from-exclude list keeps the stock entries and adds Wine's internal callers" py "$T/lists.json" "d['fromexclude'].split(';')[:6]==['winex11.drv','winemac.drv','user32','gdi32','advapi32','kernel32'] and 'ntdll' in d['fromexclude'].split(';') and 'msvcrt' not in d['fromexclude'].split(';')"
check "lists: the static import counts per DLL are reported" py "$T/lists.json" "d['imports']['kernel32']['count']==19 and d['imports']['wsock32']['count']==2"
ri lists --game-dir "$GAME" --wine-root "$WINE" --profile timers >"$T/lists-t.json" 2>/dev/null
check "lists: the timers profile traces only the timing functions (and thread creation)" py "$T/lists-t.json" "set(d['include'].split(';')) >= {'kernel32.QueryPerformanceCounter','winmm.timeGetTime','kernel32.GetTickCount','kernel32.CreateThread'} and 'advapi32.RegOpenKeyExA' not in d['include'].split(';')"
ri lists --game-dir "$GAME" --wine-root "$WINE" --profile full >"$T/lists-f.json" 2>/dev/null
check "lists: the full profile traces the broad set and the timing functions" py "$T/lists-f.json" "set(d['include'].split(';')) >= {'kernel32.QueryPerformanceCounter','winmm.timeGetTime','kernel32.GetTickCount','kernel32.CreateThread','advapi32.RegOpenKeyExA','winmm.waveOutGetPosition'} and 'kernel32.GetLastError' not in d['include'].split(';')"
ri lists --game-dir "$GAME" --wine-root "$WINE" --profile nonsense >/dev/null 2>"$T/lists-bad.err"
check "lists: an unknown profile is refused (non-zero)" test $? -ne 0

# ---- 2. the summary ------------------------------------------------------------------------------------------------------------------
ri summarize --log "$T/relay.log" --game-dir "$GAME" --wine-root "$WINE" --phases "$T/phases.txt" --out "$T/out" >"$T/sum.out" 2>"$T/sum.err"
check_eq "summarize: exits 0" "0" "$?"
S="$T/out/summary.json"
check "summarize: writes summary.json and tables.md" test -s "$S" -a -s "$T/out/tables.md"
check "summarize: the game process is told apart from other processes (4 threads, not the start.exe one)" py "$S" "sorted(t['tid'] for t in d['threads'])==['0010','0014','0018','001c']"
check "summarize: LLE created the simulation thread, suspended, priority 1" py "$S" "[t for t in d['threads'] if t['tid']=='0014'][0] | {} and True"
check "summarize: thread 0014: creator module, start module, created suspended, priority" py "$S" "(lambda t: t['creator_module']=='Low-Level Engine.dll' and t['start_module']=='Low-Level Engine.dll' and t['created_suspended'] and t['priorities']==[1] and t['creator_tid']=='0010')([t for t in d['threads'] if t['tid']=='0014'][0])"
check "summarize: priorities set by Wine's own modules are kept apart from the game's" py "$S" "(lambda t: t['priorities']==[] and t['priorities_host']==[0])([t for t in d['threads'] if t['tid']=='0010'][0])"
check "summarize: thread 0018 (Miles) set its own priority through the pseudo handle" py "$S" "(lambda t: t['creator_module']=='Mss32.dll' and t['priorities']==[2])([t for t in d['threads'] if t['tid']=='0018'][0])"
check "summarize: thread 001c is Wine's timer thread (created by winmm, kind wine)" py "$S" "(lambda t: t['creator_module']=='winmm.dll' and t['start_module']=='winmm.dll')([t for t in d['threads'] if t['tid']=='001c'][0])"
check "summarize: every thread keeps its full per-function call counts" py "$S" "(lambda t: t['calls_detail']['kernel32.QueryPerformanceCounter']==4 and t['calls_detail']['kernel32.ReadFile']==2)([t for t in d['threads'] if t['tid']=='0014'][0])"
check "summarize: SuspendThread / ResumeThread / TerminateThread callers and targets" py "$S" "{(o['fn'],o['caller_module'],o['target_tid'],o['count']) for o in d['thread_ops']} >= {('SuspendThread','Low-Level Engine.dll','0014',1),('ResumeThread','Low-Level Engine.dll','0014',1),('ResumeThread','Empire Earth.exe','0014',1),('TerminateThread','Low-Level Engine.dll','0018',1)}"
check "summarize: timeSetEvent: period, resolution, flags, callback owner" py "$S" "d['timers']['timeSetEvent']==[{'delay':16,'resolution':1,'flags':1,'callback_module':'Low-Level Engine.dll','callback_rva':'0x5000','caller_module':'Low-Level Engine.dll','count':1}]"
check "summarize: timeBeginPeriod values" py "$S" "d['timers']['timeBeginPeriod']==[{'value':1,'caller_module':'Low-Level Engine.dll','count':1}]"
check "summarize: QueryPerformanceCounter in the idle window, by caller kind" py "$S" "(lambda r: r['idle']['game']==4 and r['idle']['wine']==2 and r['idle_seconds']>5)(d['timers']['rates']['QueryPerformanceCounter'])"
check "summarize: the data archive was read (2 reads, 12288 bytes asked) and mapped" py "$S" "(lambda f: f['reads']==2 and f['read_bytes']==12288 and f['mapped_bytes']==0x100000 and f['open_ok'] and f['access']=='read')([f for f in d['files']['opened'] if f['path'].endswith('data.ssa')][0])"
check "summarize: a failed open is reported as failed" py "$S" "(lambda f: not f['open_ok'])([f for f in d['files']['opened'] if f['path'].endswith('missing.cfg')][0])"
check "summarize: FindFirstFile patterns" py "$S" "any(p['pattern'].endswith('Data\\\\*.ssa') for p in d['files']['find'])"
check "summarize: fopen through the CRT" py "$S" "any(c['path']=='ee.log' and c['mode']=='wb' for c in d['files']['crt'])"
check "summarize: registry key paths are resolved through the handle" py "$S" "(lambda r: r['key']=='HKCU\\\\Software\\\\SSSI\\\\Empire Earth')([r for r in d['registry'] if r['op']=='RegQueryValueExA'][0])"
check "summarize: registry value names, writes and failed opens" py "$S" "{(r['op'],r.get('value'),r['ok']) for r in d['registry']} >= {('RegQueryValueExA','Rasterizer Name',True),('RegSetValueExA','Resolution',True),('RegOpenKeyExA',None,False)}"
check "summarize: modules: loaded list with kinds" py "$S" "{m['name']:m['kind'] for m in d['modules']}.get('Low-Level Engine.dll')=='game' and {m['name']:m['kind'] for m in d['modules']}.get('version.dll')=='compat' and {m['name']:m['kind'] for m in d['modules']}.get('dsound.dll')=='wine'"
check "summarize: LoadLibrary and GetProcAddress" py "$S" "{l['path'] for l in d['dynamic']['loadlibrary']} >= {'dsound.dll','TCP Protocol.dll'} and d['dynamic']['getprocaddress']['dsound.dll']['found']==['DirectSoundCreate'] and d['dynamic']['getprocaddress']['dsound.dll']['missing']==['DirectSoundNope']"
check "summarize: COM CLSIDs from the ole channel, attributed to the module that called CoCreateInstance" py "$S" "(lambda c: c['clsid']=='d2ac2881-b39b-11d1-8704-00600893b1bd' and c['name']=='DirectMusicPerformance' and c['caller_module']=='Low-Level Engine.dll' and c['kind']=='game' and c['count']==1)([c for c in d['com'] if c['clsid'].startswith('d2ac2881')][0])"
check "summarize: a CLSID logged without a traced caller (Wine's own) is listed as unattributed" py "$S" "any(c['clsid']=='bcde0395-e52f-467c-8e3d-c4579291692e' and c['caller_module']=='?' and c['count']==1 for c in d['com'])"
check "summarize: MCI commands by name" py "$S" "d['mci']==[{'command':'MCI_OPEN','code':'0x803','count':1}]"
check "summarize: audio and input entry points" py "$S" "d['audio']['waveOutOpen']==1 and d['input']['DirectInputCreateA'][0]['version']=='0x700'"
check "summarize: the window that was created" py "$S" "(lambda w: w['class']=='EmpireEarth' and w['title']=='Empire Earth' and w['width']==1024 and w['height']==768)(d['windows']['created'][0])"
check "summarize: window messages by the owner of the window procedure (game threads only)" py "$S" "{(m['module'],m['kind'],m['msg'],m['count']) for m in d['messages']}=={('Empire Earth.exe','game','WM_PAINT',2),('Low-Level Engine.dll','game','WM_ACTIVATEAPP',1)}"
check "summarize: modules are also listed once per name, with their load count" py "$S" "[m for m in d['modules_distinct'] if m['name']=='Mss32.dll'][0]['loads']==1 and len(d['modules_distinct'])<=len(d['modules'])"
check "summarize: the hook that was set" py "$S" "d['windows']['hooks']==[{'id':2,'proc_module':'Low-Level Engine.dll','caller_module':'Empire Earth.exe','count':1}]"
check "summarize: x87 control word calls (_controlfp)" py "$S" "d['fpu']==[{'args':['00010000','00030000'],'caller_module':'Empire Earth.exe','count':1}]"
check "summarize: imported vs called, kernel32 / user32 / wsock32" py "$S" "d['usage']['user32']['dead']==['PeekMessageA'] and d['usage']['user32']['called_via_compat']==['GetMessageA'] and d['usage']['wsock32']['dead']==['socket'] and d['usage']['kernel32']['called_by_game']==16 and d['usage']['kernel32']['dead']==['SetFilePointer']"
check "summarize: a call logged under the forward target (ntdll.RtlInitializeCriticalSection) counts for the kernel32 import" py "$S" "'InitializeCriticalSection' not in d['usage']['kernel32']['dead']"
check "summarize: hot functions that were not traced are named, not called dead" py "$S" "{'GetLastError','EnterCriticalSection'} <= set(d['usage']['kernel32']['not_traced']) and 'GetLastError' not in d['usage']['kernel32']['dead']"
check "summarize: phases are carried over" py "$S" "d['meta']['phases']['idle_end']-d['meta']['phases']['idle_start']>5"
check "summarize: tables.md has a section per inventory item" bash -c "for h in '## 1. Threads' '## 2. Timers' '## 3. Files' '## 4. Registry' '## 5. Modules' '## 6. Windows, input, audio' '## 7. Imported versus called'; do grep -qF \"\$h\" '$T/out/tables.md' || exit 1; done"
ri summarize --log "$T/relay.log" --game-dir "$GAME" --wine-root "$WINE" --out "$T/out2" >/dev/null 2>&1
check_eq "summarize: runs without a phases file (exit 0)" "0" "$?"
: >"$T/empty.log"
ri summarize --log "$T/empty.log" --game-dir "$GAME" --wine-root "$WINE" --out "$T/out3" >/dev/null 2>"$T/empty.err"
check "summarize: an empty log is refused (non-zero)" test $? -ne 0

summary
