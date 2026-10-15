/* Test program for tools/game_run/scripts/ee-close.c: a window that ends its message loop when it gets WM_CLOSE, optionally on a
 * desktop of its own (the game runs on the virtual desktop "EmpireEarth").  Exit code 7 means "closed by WM_CLOSE". */
#include <windows.h>
#include <stdio.h>

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_CLOSE) { DestroyWindow(h); return 0; }
  if (m == WM_DESTROY) { PostQuitMessage(7); return 0; }
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
  wc.lpszClassName = "EEVictim";
  if (!RegisterClassA(&wc)) return 11;
  HWND h = CreateWindowA("EEVictim", "victim", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
  if (!h) return 12;
  FILE *f = fopen("victim.ready", "w");
  if (f) fclose(f);
  MSG msg;
  while (GetMessageA(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageA(&msg); }
  return (int)msg.wParam;
}
