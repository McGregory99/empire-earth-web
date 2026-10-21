/* ee-key.exe: press keys by scancode with SendInput, the way a player's keyboard does.  The game reads its keyboard through
 * DirectInput, which Wine feeds from the same low-level events; a scancode (not a virtual key) is what it sees.
 *
 *   ee-key.exe [--desktop <name>] [--window-class <class>] <scancode in hex> [<scancode> ...]
 *
 * With --window-class it first finds the visible top-level window of that class (waiting up to 5 s), and tries up to 5 times to
 * make it the foreground window (AttachThreadInput, AllowSetForegroundWindow, SetForegroundWindow, SetFocus), checking
 * GetForegroundWindow each time.  Focus confirmed: it presses with SendInput ("focus confirmed").  Not obtained (a Wine window
 * on macOS does not always get it): the keys go to that window as WM_KEYDOWN / WM_KEYUP messages ("focus not obtained,
 * posting"), so the press never depends on where the focus happens to be.
 *
 * Before pressing, it lists the visible top-level windows of the desktop it uses and marks the foreground one, so that a run
 * that loses its keys says where they went.  Each key goes down, stays down 60 ms, goes up, and the next one follows 150 ms later.  With --desktop the input goes to that
 * desktop (the tests use one of their own).  Exit status: 0 pressed, 2 usage or a scancode that is not hex, 3 the desktop
 * cannot be used, 4 SendInput failed, 6 no visible window of the class.
 * Build: i686-w64-mingw32-gcc -mcrtdll=msvcrt-os -D_WIN32_WINNT=0x0600 -o ee-key.exe ee-key.c -luser32 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int send_key(WORD scan, DWORD flags) {
  INPUT in;
  memset(&in, 0, sizeof in);
  in.type = INPUT_KEYBOARD;
  in.ki.wScan = scan;
  in.ki.dwFlags = KEYEVENTF_SCANCODE | flags;
  return SendInput(1, &in, sizeof in) == 1;
}

static BOOL CALLBACK list_window(HWND h, LPARAM l) {
  char title[80] = "", cls[48] = "";
  (void)l;
  if (!IsWindowVisible(h)) return TRUE;
  GetWindowTextA(h, title, sizeof title);
  GetClassNameA(h, cls, sizeof cls);
  printf("ee-key:   window %p class '%s' title '%s'%s\n", (void *)h, cls, title, h == GetForegroundWindow() ? " [foreground]" : "");
  return TRUE;
}

static const char *g_class;
static HWND g_found;

static BOOL CALLBACK find_window(HWND h, LPARAM l) {
  char cls[96] = "";
  (void)l;
  if (!IsWindowVisible(h)) return TRUE;
  GetClassNameA(h, cls, sizeof cls);
  if (strcmp(cls, g_class) != 0) return TRUE;
  g_found = h;
  return FALSE;
}

/* Try to make `h` the foreground window; 1 when GetForegroundWindow says it is. */
static int take_focus(HWND h) {
  for (int attempt = 0; attempt < 5; attempt++) {
    DWORD me = GetCurrentThreadId(), there = GetWindowThreadProcessId(h, NULL);
    BOOL attached = there != me && AttachThreadInput(me, there, TRUE);
    AllowSetForegroundWindow(ASFW_ANY);
    BringWindowToTop(h);
    SetForegroundWindow(h);
    SetFocus(h);
    if (attached) AttachThreadInput(me, there, FALSE);
    Sleep(200);
    if (GetForegroundWindow() == h) return 1;
  }
  return 0;
}

static void post_key(HWND h, WORD scan) {
  UINT vk = MapVirtualKeyA(scan, MAPVK_VSC_TO_VK);
  LPARAM down = 1 | ((LPARAM)scan << 16), up = down | (LPARAM)0xC0000000;
  PostMessageA(h, WM_KEYDOWN, vk, down);
  Sleep(60);
  PostMessageA(h, WM_KEYUP, vk, up);
}

int main(int argc, char **argv) {
  int i = 1;
  HWND target = NULL;
  int posting = 0;
  if (i + 1 < argc && strcmp(argv[i], "--desktop") == 0) {
    HDESK d = OpenDesktopA(argv[i + 1], 0, FALSE, GENERIC_ALL);
    if (!d || !SetThreadDesktop(d)) {
      fprintf(stderr, "ee-key: cannot use desktop '%s' (error %lu)\n", argv[i + 1], (unsigned long)GetLastError());
      return 3;
    }
    i += 2;
  }
  if (i + 1 < argc && strcmp(argv[i], "--window-class") == 0) {
    g_class = argv[i + 1];
    i += 2;
  }
  if (i >= argc) {
    fprintf(stderr, "usage: ee-key.exe [--desktop <name>] [--window-class <class>] <scancode in hex> [<scancode> ...]\n");
    return 2;
  }
  for (int j = i; j < argc; j++) {
    char *end;
    unsigned long v = strtoul(argv[j], &end, 16);
    if (!*argv[j] || *end || v == 0 || v > 0x7f) {
      fprintf(stderr, "ee-key: '%s' is not a scancode (1..7f, hex)\n", argv[j]);
      return 2;
    }
  }
  if (g_class) {
    for (int t = 0; t < 25 && !g_found; t++) {
      EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()), find_window, 0);
      if (!g_found) Sleep(200);
    }
    if (!g_found) {
      fprintf(stderr, "ee-key: no visible window of class '%s' on this desktop\n", g_class);
      return 6;
    }
    target = g_found;
    if (take_focus(target)) {
      printf("ee-key: focus confirmed: window %p is the foreground window\n", (void *)target);
    } else {
      posting = 1;
      printf("ee-key: focus not obtained, posting WM_KEYDOWN/WM_KEYUP to window %p\n", (void *)target);
    }
  }
  printf("ee-key: visible windows of this desktop:\n");
  EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()), list_window, 0);
  for (; i < argc; i++) {
    WORD scan = (WORD)strtoul(argv[i], NULL, 16);
    if (posting) {
      post_key(target, scan);
    } else {
      if (!send_key(scan, 0)) return 4;
      Sleep(60);
      if (!send_key(scan, KEYEVENTF_KEYUP)) return 4;
    }
    Sleep(150);
    printf("ee-key: pressed %02x\n", scan);
  }
  fflush(stdout);
  return 0;
}
