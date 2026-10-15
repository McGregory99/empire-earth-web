/* ee-close.exe: ask a program to close, the way the user's close button does.  Used by runtime-inventory.sh to end the game
 * cleanly (a run that ends with `wineserver -k` shows the kill, not the game's own shutdown).
 *
 *   ee-close.exe [--quit] <program.exe> [desktop ...]
 *
 * Posts WM_CLOSE to every top-level window of <program.exe> (matched by the file name of its image, case-insensitive) on each
 * named desktop; the game runs on the Wine virtual desktop "EmpireEarth", so the desktops are tried in the order given.  With no
 * desktop the current one is used.  With --quit it posts WM_QUIT to the thread that owns each such window instead (the game's
 * message loop ends on WM_QUIT too, without the window-procedure path).  Exit status: 0 a message was posted, 3 no window found,
 * 2 usage.
 * Build: i686-w64-mingw32-gcc -mcrtdll=msvcrt-os -D_WIN32_WINNT=0x0600 -o ee-close.exe ee-close.c -luser32 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char *g_want;
static int g_posted;
static int g_quit;
static DWORD g_threads[64];
static int g_nthreads;

static BOOL CALLBACK each_window(HWND h, LPARAM lp) {
  (void)lp;
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!p) return TRUE;
  char path[MAX_PATH];
  DWORD n = MAX_PATH;
  BOOL ok = QueryFullProcessImageNameA(p, 0, path, &n);
  CloseHandle(p);
  if (!ok) return TRUE;
  const char *base = strrchr(path, '\\');
  base = base ? base + 1 : path;
  if (_stricmp(base, g_want) != 0) return TRUE;
  char cls[96] = "";
  GetClassNameA(h, cls, sizeof cls);
  if (g_quit) {
    DWORD tid = GetWindowThreadProcessId(h, NULL);
    for (int i = 0; i < g_nthreads; i++)
      if (g_threads[i] == tid) return TRUE;
    if (g_nthreads < 64) g_threads[g_nthreads++] = tid;
    if (PostThreadMessageA(tid, WM_QUIT, 0, 0)) {
      printf("posted WM_QUIT to thread %lu (window %p, class '%s') of pid %lu\n", (unsigned long)tid, (void *)h, cls, (unsigned long)pid);
      g_posted++;
    }
    return TRUE;
  }
  if (PostMessageA(h, WM_CLOSE, 0, 0)) {
    printf("posted WM_CLOSE to window %p (class '%s', visible %d) of pid %lu\n", (void *)h, cls, IsWindowVisible(h), (unsigned long)pid);
    g_posted++;
  }
  return TRUE;
}

int main(int argc, char **argv) {
  int first = 1;
  if (argc > 1 && strcmp(argv[1], "--quit") == 0) {
    g_quit = 1;
    first = 2;
  }
  if (argc < first + 1) {
    fprintf(stderr, "usage: ee-close.exe [--quit] <program.exe> [desktop ...]\n");
    return 2;
  }
  g_want = argv[first];
  if (argc == first + 1) EnumWindows(each_window, 0);
  for (int i = first + 1; i < argc; i++) {
    HDESK d = OpenDesktopA(argv[i], 0, FALSE, DESKTOP_ENUMERATE | DESKTOP_READOBJECTS);
    if (!d) {
      printf("desktop '%s': cannot open (error %lu)\n", argv[i], (unsigned long)GetLastError());
      continue;
    }
    int before = g_posted;
    EnumDesktopWindows(d, each_window, 0);
    printf("desktop '%s': %d window(s) closed\n", argv[i], g_posted - before);
    CloseDesktop(d);
  }
  fflush(stdout);
  return g_posted > 0 ? 0 : 3;
}
