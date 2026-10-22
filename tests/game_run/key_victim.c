/* Test program for tools/game_run/scripts/ee-key.c: a focused window that appends the scancode of every
 * key-down it gets to key_victim.out (one hex byte per line; an extended key as e0xx), and ends after the key 0x01 (Esc)
 * or after 20 s.  key_victim.mouse gets one line per mouse event and per key-up/-down (with the tick count of the moment). */
#include <windows.h>
#include <stdio.h>

static void note(const char *fmt, UINT code) {
  FILE *f = fopen("key_victim.out", "a");
  if (f) {
    fprintf(f, fmt, code);
    fclose(f);
  }
}

static void mnote(const char *what, long a, long b) {
  FILE *f = fopen("key_victim.mouse", "a");
  if (f) {
    fprintf(f, "%s %ld,%ld %lu\n", what, a, b, (unsigned long)GetTickCount());
    fclose(f);
  }
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_LBUTTONDOWN) { mnote("down", (short)LOWORD(l), (short)HIWORD(l)); return 0; }
  if (m == WM_LBUTTONUP) { mnote("up", (short)LOWORD(l), (short)HIWORD(l)); return 0; }
  if (m == WM_MOUSEWHEEL) { mnote("wheel", (short)HIWORD(w), 0); return 0; }
  if (m == WM_KEYUP || m == WM_SYSKEYUP) { mnote("keyup", (l >> 16) & 0xff, (l >> 24) & 1); return 0; }
  if (m == WM_KEYDOWN || m == WM_SYSKEYDOWN) {
    UINT sc = (l >> 16) & 0xff;
    if (!((l >> 30) & 1)) { /* not an auto-repeat */
      mnote("keydown", sc, (l >> 24) & 1);
      note(((l >> 24) & 1) ? "e0%02x\n" : "%02x\n", sc);
    }
    if (sc == 0x01) PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcA(h, m, w, l);
}

int main(int argc, char **argv) {
  if (argc > 1) {
    HDESK d = CreateDesktopA(argv[1], NULL, NULL, 0, GENERIC_ALL, NULL);
    if (!d || !SetThreadDesktop(d)) return 10;
  }
  WNDCLASSA wc = {0};
  wc.lpfnWndProc = wndproc;
  wc.hInstance = GetModuleHandleA(NULL);
  wc.lpszClassName = "EEKeyVictim";
  if (!RegisterClassA(&wc)) return 11;
  HWND h = CreateWindowA("EEKeyVictim", "victim", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 300, 300, NULL, NULL, wc.hInstance, NULL);
  if (!h) return 12;
  SetForegroundWindow(h);
  SetFocus(h);
  FILE *f = fopen("key_victim.ready", "w");
  if (f) fclose(f);
  DWORD end = GetTickCount() + 20000;
  MSG msg;
  while (GetTickCount() < end) {
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) return 0;
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
    }
    Sleep(10);
  }
  return 1;
}
