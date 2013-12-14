#define ALONE \
    set -ex; exec ${CC:-gcc} -s -Os -fno-stack-protector \
    -W -Wall -o clang_xstatic "$0"; exit 1
/*
 * clang_xstatic.c: clang trampoline for .so file redir and -static linking
 * by pts@fazekas.hu at Fri Dec 13 22:17:42 CET 2013
 *
 * Since our process is short-lived, we don't bother free()ing memory.
 */

#include "clang_common.ci"

int main(int argc, char **argv) {
  char *prog;
  char *ldso0;
  char *dir = argv[0][0] == '\0' ? strdup("x") : strdup(readlink_alloc_all(
      strstr(argv[0], "/") ? argv[0] : "/proc/self/exe"));
  char *p;
  char **args, **argp;
  for (p = dir + strlen(dir); p != dir && p[-1] != '/'; --p) {}
  if (p == dir) {
    strcpy(dir, ".");
  } else {
    p[-1] = '\0';
  }
  /* RPATH=$ORIGIN/../binlib (set manually in clang.bin) takes care of this */
  /* in clang.bin /proc/self/exE was modified to /proc/self/exe */
  /* Needed so ld0.so doesn't consult /etc/ld.so.cache */
  putenv("LD0LIBRARY_PATH=/dev/null/missing");
  /* clang was doing:
   * readlink("/proc/self/exe", ".../clang/binlib/ld-linux.so.2", ...)
   * ... and believed it's ld-linux.so.2. I edited the binary to /proc/self/exE
   * to fix it.
   */
  argp = args = malloc(sizeof(*args) * (argc + 10));
  *argp++ = argv[0];  /* No effect, will be ignored. */
  /* TODO(pts): Make clang.bin configurable. */
  *argp++ = prog = strdupcat(dir, "/clang.bin", "");
  if (argv[0][0] == '/') {
    *argp++ = argv[0];  /* ld0.so will put it to clang.bin's argv[0]. */
  } else {
    /* Clang 3.3 can't find itself (for with -cc1) unless its argv[0] is an
     * absolute pathname. So we make it absolute.
     */
    *argp++ = path_join(get_current_dir_name(), argv[0]);
  }
  /* It's important that we don't trigger when argv[1] is "-cc1", because
   * clang is calling itself in this case.
   */
  if (argv[1] && 0 == strcmp(argv[1], "-xstatic")) {
    ++argv;
    --argc;
    /* When adding more arguments here, increase the args malloc count. */
    /* We don't need get_autodetect_archflag(argv), we always send "-m32". */
    *argp++ = "-m32";
    *argp++ = "-static";
    /* TODO(pts): Get rid of this warning if compiling only .o files:
     * clang.bin: warning: argument unused during compilation: '-nostdinc'
     * -Qunused-arguments, but only for this flag.
     */
    *argp++ = "-Qunused-arguments";
    *argp++ = "-nostdinc";
    *argp++ = strdupcat("-I", dir, "/../lib/clang/cur/include");
    *argp++ = strdupcat("-I", dir, "/../xstatic/include");
    /* The linker would be ../xstatic/i486-linux-gnu/bin/ld, which is also
     * a trampoline binary of ours.
     */
    *argp++ = "-gcc-toolchain";
    *argp++ = strdupcat(dir, "/../xstatic", "");
    /* Run `clang -print-search-dirs' to confirm that it was properly
     * detected.
     */
  } else if (!argv[1] || 0 != strcmp(argv[1], "-cc1")) {
    char need_linker = 1;
    char **argi, *arg, c;
    for (argi = argv + 1; (arg = *argi); ++argi) {
      if (arg[0] != '-') continue;
      c = arg[1];
      /* E.g. with "-E" the linker won't be invoked. */
      if ((c == 'M' && arg[2] == 'M' && arg[3] == '\0') ||
          ((c == 'E' || c == 'S' || c == 'c' || c == 'M') && arg[2] == '\0')) {
        need_linker = 0;
      }
    }
    /* If !need_linker, avoid clang warning about unused linker input. */
    if (need_linker) {
      *argp++ = "-Wl,-nostdlib";  /* -L/usr and equivalents added by clang. */
    }
  }
  memcpy(argp, argv + 1, argc * sizeof(*argp));
  ldso0 = strdupcat(dir, "/../binlib/ld0.so", "");
  execv(ldso0, args);
  p = strdupcat("error: clang: exec failed: ", prog, "\n");
  (void)!write(2, p, strlen(p));
  return 120;
}
