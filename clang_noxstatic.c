#define ALONE \
    set -ex; exec ${CC:-gcc} -s -Os -fno-stack-protector \
    -W -Wall -o clang_noxstatic "$0"; exit 1
/*
 * clang_noxstatic: clang trampoline for .so file redirection
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
  /* LD0LIBRARY_PATH is needed so ld0.so doesn't consult /etc/ld.so.cache.
   * Please note that /lib/ld-linux.so.2 consults LD_LIBRARY_PATH, which ld0.so
   * ignores and vice versa. This is good so we can support dynamically linked
   * host-specific tools.
   */
  putenv("LD0LIBRARY_PATH=/dev/null/missing");
  /* clang was doing:
   * readlink("/proc/self/exe", ".../clang/binlib/ld-linux.so.2", ...)
   * ... and believed it's ld-linux.so.2. I edited the binary to /proc/self/exE
   * to fix it.
   */
  argp = args = malloc(sizeof(*args) * (argc + 12));
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
  /* clang calls itself with the -cc1 flag. Don't modify the argv then. */
  if (!argv[1] || 0 != strcmp(argv[1], "-cc1")) {
    char need_linker = 1;
    char **argi, *arg, c;
    archbit_t archbit, archbit_override;
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
    archbit = get_archbit_detected(argv);
    archbit_override = get_archbit_override(archbit);
    if (archbit_override == ARCHBIT_32) {
      *argp++ = "-m32";
      archbit = ARCHBIT_32;
    } else if (archbit_override == ARCHBIT_64) {
      *argp++ = "-m64";
      archbit = ARCHBIT_64;
    }
    if (archbit == ARCHBIT_64) {
      /* Let the compiler find our replacement for gnu/stubs-64.h on 32-bit
       * systems which don't provide it.
       */
      *argp++ = "-idirafter";  /* Add with low priority. */
      *argp++ = strdupcat(dir, "/../include64low", "");
    }
  }
  memcpy(argp, argv + 1, argc * sizeof(*argp));
  ldso0 = strdupcat(dir, "/../binlib/ld0.so", "");
  execv(ldso0, args);
  p = strdupcat("error: clang: exec failed: ", prog, "\n");
  (void)!write(2, p, strlen(p));
  return 120;
}
