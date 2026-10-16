/* Test program for tools/game_run/scripts/ee-start.c: records the command line and the current directory it was started with
 * (start_victim.out in its own directory), then waits for the file start_victim.stop (at most 30 s). */
#include <windows.h>
#include <stdio.h>

int main(void) {
  char cwd[MAX_PATH];
  GetCurrentDirectoryA(sizeof cwd, cwd);
  FILE *f = fopen("start_victim.out", "w");
  if (f) {
    fprintf(f, "CMDLINE=%s\nCWD=%s\n", GetCommandLineA(), cwd);
    fclose(f);
  }
  for (int i = 0; i < 300; i++) {
    if (GetFileAttributesA("start_victim.stop") != INVALID_FILE_ATTRIBUTES) return 0;
    Sleep(100);
  }
  return 1;
}
