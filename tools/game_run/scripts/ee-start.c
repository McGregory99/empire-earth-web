/* ee-start.exe: start a program with exactly the command line a driver wrote down.  `wine start /unix <exe> <args>` rewrites the
 * quotes of its arguments (a quote arrives as \"), and the game's command line parser needs them: -loadgame "<save>".  The
 * driver's Wine wrapper starts this helper in place of the game; the helper reads ee-start.cmd from its own directory:
 *
 *   line 1  Windows path of the program
 *   line 2  its arguments, verbatim (may be empty)
 *
 * and creates the program with the program's own folder as working directory and "<path>" <arguments> as command line, then
 * returns at once (the program lives on under the Wine server).  Exit status: 0 started, 2 no usable command file, 3 the
 * program could not be created.
 * Build: i686-w64-mingw32-gcc -mcrtdll=msvcrt-os -D_WIN32_WINNT=0x0600 -o ee-start.exe ee-start.c */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static void chomp(char *s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
}

int main(void) {
  char self[MAX_PATH], cmdfile[MAX_PATH], prog[MAX_PATH], args[2048], cmdline[MAX_PATH + 2048 + 8], dir[MAX_PATH];
  DWORD n = GetModuleFileNameA(NULL, self, sizeof self);
  char *slash;
  if (n == 0 || n >= sizeof self || !(slash = strrchr(self, '\\'))) return 2;
  *slash = 0;
  if (snprintf(cmdfile, sizeof cmdfile, "%s\\ee-start.cmd", self) >= (int)sizeof cmdfile) return 2;
  FILE *f = fopen(cmdfile, "r");
  if (!f) {
    fprintf(stderr, "ee-start: cannot read %s\n", cmdfile);
    return 2;
  }
  args[0] = 0;
  if (!fgets(prog, sizeof prog, f)) {
    fclose(f);
    fprintf(stderr, "ee-start: %s has no program on its first line\n", cmdfile);
    return 2;
  }
  if (!fgets(args, sizeof args, f)) args[0] = 0;
  fclose(f);
  chomp(prog);
  chomp(args);
  if (!prog[0]) return 2;
  snprintf(dir, sizeof dir, "%s", prog);
  if ((slash = strrchr(dir, '\\'))) *slash = 0;
  snprintf(cmdline, sizeof cmdline, "\"%s\"%s%s", prog, args[0] ? " " : "", args);
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si);
  si.cb = sizeof si;
  if (!CreateProcessA(prog, cmdline, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
    fprintf(stderr, "ee-start: cannot create %s (error %lu)\n", prog, (unsigned long)GetLastError());
    return 3;
  }
  printf("ee-start: started %s (pid %lu)\n", prog, (unsigned long)pi.dwProcessId);
  fflush(stdout);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return 0;
}
