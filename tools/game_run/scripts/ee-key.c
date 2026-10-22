/* ee-key.exe: press keys by scancode with SendInput, the way a player's keyboard does.  The game reads its keyboard through
 * DirectInput, which Wine feeds from the same low-level events; a scancode (not a virtual key) is what it sees.
 *
 *   ee-key.exe [--desktop <name>] [--window-class <class>] <action> [<action> ...]
 *
 * An action is one of
 *   <scancode>[/<ms>]   a key, hex (1..7f); an extended key (the arrows, Home, End, ...) with the e0 prefix as e0xx (e04b = left
 *                       arrow); /<ms> keeps it down that long (default 60): a held arrow scrolls the camera
 *   wheel:<+n|-n>       n notches of the mouse wheel over the window's centre (positive = away from the user)
 *   click:<fx>,<fy>     a left click at that point of the window's client area, as fractions 0..1 of its width and height
 *   drag:<fx1>,<fy1>,<fx2>,<fy2>   press at the first point, move to the second, release (a selection box)
 * The mouse actions are relative to a window, so they need --window-class.
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

static void post_key(HWND h, WORD scan, int ext, DWORD hold_ms) {
  UINT vk = MapVirtualKeyA(scan, MAPVK_VSC_TO_VK);
  LPARAM ex = ext ? 0x01000000 : 0;
  LPARAM down = 1 | ((LPARAM)scan << 16) | ex, up = down | (LPARAM)0xC0000000;
  PostMessageA(h, WM_KEYDOWN, vk, down);
  Sleep(hold_ms);
  PostMessageA(h, WM_KEYUP, vk, up);
}

typedef enum { A_KEY, A_WHEEL, A_CLICK, A_DRAG } Kind;
typedef struct {
  Kind kind;
  WORD scan;
  int ext;
  DWORD hold_ms;
  int notches;
  double x1, y1, x2, y2;
} Action;

static int parse_fraction(const char *s, char **end, double *out) {
  double v = strtod(s, end);
  if (*end == s || v < 0.0 || v > 1.0) return 0;
  *out = v;
  return 1;
}

/* 0 = not an action */
static int parse_action(const char *a, Action *x) {
  char *end;
  memset(x, 0, sizeof *x);
  x->hold_ms = 60;
  if (strncmp(a, "wheel:", 6) == 0) {
    long n = strtol(a + 6, &end, 10);
    if (end == a + 6 || *end || n == 0 || n < -50 || n > 50) return 0;
    x->kind = A_WHEEL;
    x->notches = (int)n;
    return 1;
  }
  if (strncmp(a, "click:", 6) == 0) {
    x->kind = A_CLICK;
    if (!parse_fraction(a + 6, &end, &x->x1) || *end != ',') return 0;
    if (!parse_fraction(end + 1, &end, &x->y1) || *end) return 0;
    return 1;
  }
  if (strncmp(a, "drag:", 5) == 0) {
    x->kind = A_DRAG;
    if (!parse_fraction(a + 5, &end, &x->x1) || *end != ',') return 0;
    if (!parse_fraction(end + 1, &end, &x->y1) || *end != ',') return 0;
    if (!parse_fraction(end + 1, &end, &x->x2) || *end != ',') return 0;
    if (!parse_fraction(end + 1, &end, &x->y2) || *end) return 0;
    return 1;
  }
  {
    unsigned long v = strtoul(a, &end, 16);
    x->kind = A_KEY;
    if (!*a || end == a || v == 0) return 0;
    if (v > 0xff) { /* e0xx */
      if ((v >> 8) != 0xe0 || (v & 0xff) == 0 || (v & 0xff) > 0x7f) return 0;
      x->ext = 1;
      v &= 0xff;
    } else if (v > 0x7f) {
      return 0;
    }
    x->scan = (WORD)v;
    if (*end == '/') {
      char *e2;
      unsigned long ms = strtoul(end + 1, &e2, 10);
      if (e2 == end + 1 || *e2 || ms == 0 || ms > 60000) return 0;
      x->hold_ms = (DWORD)ms;
    } else if (*end) {
      return 0;
    }
  }
  return 1;
}

static int needs_window(const Action *x) { return x->kind != A_KEY; }

static int send_mouse(DWORD flags, LONG sx, LONG sy, LONG data) {
  INPUT in;
  memset(&in, 0, sizeof in);
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = flags | MOUSEEVENTF_ABSOLUTE;
  in.mi.dx = sx;
  in.mi.dy = sy;
  in.mi.mouseData = (DWORD)data;
  return SendInput(1, &in, sizeof in) == 1;
}

/* Screen point of a fraction of the window's client area, in SendInput's 0..65535 absolute space. */
static void abs_point(HWND h, double fx, double fy, LONG *ax, LONG *ay) {
  RECT rc;
  POINT p;
  int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
  GetClientRect(h, &rc);
  p.x = (LONG)(fx * (rc.right - 1));
  p.y = (LONG)(fy * (rc.bottom - 1));
  ClientToScreen(h, &p);
  *ax = (LONG)(((double)p.x * 65535.0) / (sw > 1 ? sw - 1 : 1)) + 1;
  *ay = (LONG)(((double)p.y * 65535.0) / (sh > 1 ? sh - 1 : 1)) + 1;
}

static LPARAM client_lparam(HWND h, double fx, double fy) {
  RECT rc;
  GetClientRect(h, &rc);
  return MAKELPARAM((WORD)(fx * (rc.right - 1)), (WORD)(fy * (rc.bottom - 1)));
}

static int do_mouse(HWND h, const Action *x, int posting) {
  LONG ax, ay, bx, by;
  if (x->kind == A_WHEEL) {
    if (posting) {
      RECT rc;
      POINT c;
      GetClientRect(h, &rc);
      c.x = rc.right / 2;
      c.y = rc.bottom / 2;
      ClientToScreen(h, &c);
      PostMessageA(h, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)(x->notches * WHEEL_DELTA)), MAKELPARAM((WORD)c.x, (WORD)c.y));
      return 1;
    }
    abs_point(h, 0.5, 0.5, &ax, &ay);
    if (!send_mouse(MOUSEEVENTF_MOVE, ax, ay, 0)) return 0;
    Sleep(100);
    return send_mouse(MOUSEEVENTF_WHEEL, ax, ay, x->notches * WHEEL_DELTA);
  }
  if (posting) {
    LPARAM a = client_lparam(h, x->x1, x->y1), b = x->kind == A_DRAG ? client_lparam(h, x->x2, x->y2) : a;
    PostMessageA(h, WM_MOUSEMOVE, 0, a);
    Sleep(60);
    PostMessageA(h, WM_LBUTTONDOWN, MK_LBUTTON, a);
    Sleep(80);
    if (x->kind == A_DRAG) {
      PostMessageA(h, WM_MOUSEMOVE, MK_LBUTTON, b);
      Sleep(150);
    }
    PostMessageA(h, WM_LBUTTONUP, 0, b);
    return 1;
  }
  abs_point(h, x->x1, x->y1, &ax, &ay);
  if (x->kind == A_DRAG) abs_point(h, x->x2, x->y2, &bx, &by);
  else { bx = ax; by = ay; }
  if (!send_mouse(MOUSEEVENTF_MOVE, ax, ay, 0)) return 0;
  Sleep(100);
  if (!send_mouse(MOUSEEVENTF_LEFTDOWN, ax, ay, 0)) return 0;
  Sleep(80);
  if (x->kind == A_DRAG) {
    if (!send_mouse(MOUSEEVENTF_MOVE, bx, by, 0)) return 0;
    Sleep(150);
  }
  return send_mouse(MOUSEEVENTF_LEFTUP, bx, by, 0);
}

int main(int argc, char **argv) {
  int i = 1, n;
  HWND target = NULL;
  int posting = 0;
  Action *acts;
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
    fprintf(stderr, "usage: ee-key.exe [--desktop <name>] [--window-class <class>] <scancode[/ms] | wheel:n | click:fx,fy | drag:fx1,fy1,fx2,fy2> ...\n");
    return 2;
  }
  n = argc - i;
  acts = calloc((size_t)n, sizeof *acts);
  if (!acts) return 2;
  for (int j = 0; j < n; j++) {
    if (!parse_action(argv[i + j], &acts[j])) {
      fprintf(stderr, "ee-key: '%s' is not an action (a hex scancode 1..7f or e0xx, optionally /<ms>; wheel:+n; click:fx,fy; drag:fx1,fy1,fx2,fy2 with fractions 0..1)\n", argv[i + j]);
      return 2;
    }
    if (needs_window(&acts[j]) && !g_class) {
      fprintf(stderr, "ee-key: '%s' is relative to a window and needs --window-class\n", argv[i + j]);
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
    {
      RECT rc;
      GetClientRect(target, &rc);
      printf("ee-key: client area %ldx%ld\n", (long)rc.right, (long)rc.bottom);
    }
  }
  printf("ee-key: visible windows of this desktop:\n");
  EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()), list_window, 0);
  for (int j = 0; j < n; j++) {
    const Action *x = &acts[j];
    if (x->kind == A_KEY) {
      if (posting) {
        post_key(target, x->scan, x->ext, x->hold_ms);
      } else {
        DWORD f = x->ext ? KEYEVENTF_EXTENDEDKEY : 0;
        if (!send_key(x->scan, f)) return 4;
        Sleep(x->hold_ms);
        if (!send_key(x->scan, f | KEYEVENTF_KEYUP)) return 4;
      }
      Sleep(150);
      if (x->hold_ms != 60) printf("ee-key: pressed %s%02x (held %lu ms)\n", x->ext ? "e0" : "", x->scan, (unsigned long)x->hold_ms);
      else printf("ee-key: pressed %s%02x\n", x->ext ? "e0" : "", x->scan);
    } else {
      if (!do_mouse(target, x, posting)) return 4;
      Sleep(150);
      printf("ee-key: %s %s\n", x->kind == A_WHEEL ? "wheel" : x->kind == A_CLICK ? "clicked" : "dragged", argv[i + j]);
    }
  }
  fflush(stdout);
  return 0;
}
