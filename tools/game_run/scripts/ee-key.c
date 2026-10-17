/* ee-key.exe: press keys by scancode with SendInput, the way a player's keyboard does.  The game reads its keyboard through
 * DirectInput, which Wine feeds from the same low-level events; a scancode (not a virtual key) is what it sees.
 *
 *   ee-key.exe [--desktop <name>] [--hold <release file>] <scancode in hex> [<scancode> ...]
 *
 * Each key goes down, stays down 60 ms, goes up, and the next one follows 150 ms later.  With --desktop the input goes to that
 * desktop (the tests use one of their own).  Exit status: 0 pressed, 2 usage or a scancode that is not hex, 3 the desktop
 * cannot be used, 4 SendInput failed.
 *
 * With --hold the keys go down at once and stay down until <release file> exists (at most 300 s), then go up.  A key that is
 * already down when the game's loop starts is seen at its very first poll, so the game reacts at a fixed point of its start,
 * not at a tick that depends on when a press happens to arrive.
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

int main(int argc, char **argv) {
  int i = 1;
  if (i + 1 < argc && strcmp(argv[i], "--desktop") == 0) {
    HDESK d = OpenDesktopA(argv[i + 1], 0, FALSE, GENERIC_ALL);
    if (!d || !SetThreadDesktop(d)) {
      fprintf(stderr, "ee-key: cannot use desktop '%s' (error %lu)\n", argv[i + 1], (unsigned long)GetLastError());
      return 3;
    }
    i += 2;
  }
  const char *hold = NULL;
  if (i + 1 < argc && strcmp(argv[i], "--hold") == 0) {
    hold = argv[i + 1];
    i += 2;
  }
  if (i >= argc) {
    fprintf(stderr, "usage: ee-key.exe [--desktop <name>] [--hold <release file>] <scancode in hex> [<scancode> ...]\n");
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
  if (hold) {
    int first = i;
    for (int j = first; j < argc; j++)
      if (!send_key((WORD)strtoul(argv[j], NULL, 16), 0)) return 4;
    printf("ee-key: holding");
    for (int j = first; j < argc; j++) printf(" %s", argv[j]);
    printf("\n");
    fflush(stdout);
    for (int waited = 0; waited < 300 * 50 && GetFileAttributesA(hold) == INVALID_FILE_ATTRIBUTES; waited++) Sleep(20);
    for (int j = argc - 1; j >= first; j--) {
      if (!send_key((WORD)strtoul(argv[j], NULL, 16), KEYEVENTF_KEYUP)) return 4;
      printf("ee-key: released %s\n", argv[j]);
    }
    fflush(stdout);
    return 0;
  }
  for (; i < argc; i++) {
    WORD scan = (WORD)strtoul(argv[i], NULL, 16);
    if (!send_key(scan, 0)) return 4;
    Sleep(60);
    if (!send_key(scan, KEYEVENTF_KEYUP)) return 4;
    Sleep(150);
    printf("ee-key: pressed %02x\n", scan);
  }
  fflush(stdout);
  return 0;
}
