#define ALONE \
    set -ex; ${CC:-gcc} -s -Os -fno-stack-protector \
    -W -Wall -o clang_noxstatic "$0"; \
    ${CC:-gcc} -s -Os -fno-stack-protector \
    -W -Wall -DUSE_XSTATIC -o clang_xstatic "$0"; \
    exit 0
/*
 * clang_trampline.c: clang trampoline for .so file redir and -static linking
 * by pts@fazekas.hu at Fri Dec 13 22:17:42 CET 2013
 *
 * Since our process is short-lived, we don't bother free()ing memory.
 */

#define _GNU_SOURCE 1  /* Needed for get_current_dir_name() */

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

static char *strdupcat(const char *a, const char *b, const char *c) {
  const size_t sa = strlen(a), sb = strlen(b), sc = strlen(c);
  char *p = malloc(sa + sb + sc + 1);
  memcpy(p, a, sa);
  memcpy(p + sa, b, sb);
  strcpy(p + sa + sb, c);
  return p;
}

static char *readlink_alloc(const char *path) {
  ssize_t len = 256;
  ssize_t got;
  char *buf = malloc(len);
  for (;;) {
    if (0 > (got = readlink(path, buf, len))) {
      free(buf);
      return NULL;
    }
    if (got < len) {
      buf[got] = '\0';
      return buf;
    }
    buf = realloc(buf, len <<= 1);
  }
}

/** Resolve symlinks until a non-symlink is found. */
static char *readlink_alloc_all(const char *path) {
  char *path2, *path1 = strdup(path);
  while ((path2 = readlink_alloc(path1))) {
    if (path2[0] != '/') {
      char *p;
      for (p = path1 + strlen(path1); p != path1 && p[-1] != '/'; --p) {}
      if (p != path1) {
        *p = '\0';  /* Remove basename from path1. */
        p = strdupcat(path1, "", path2);
        free(path2);
        path2 = p;
      }
    }
    free(path1);
    path1 = path2;
  }
  return path1;
}

static char *path_join(char *a, char *b) {
  return !a ? b : (b[0] == '/') ? strdupcat("", "", b) : strdupcat(a, "/", b);
}

typedef enum archbit_t {
  ARCHBIT_UNKNOWN = -1,
  ARCHBIT_UNSPECIFIED = -2,
  ARCHBIT_DETECTED = -3,
  ARCHBIT_32 = 32,
  ARCHBIT_64 = 64,
} archbit_t;

static archbit_t get_archbit_detected(char **argv) {
  char **argi, *arg;
  for (argi = argv + 1; (arg = *argi); ++argi) {
    /* TODO(pts): Support Clang >3.3 flag `-arch i386'. */
    /* Please note that -march=k8 doesn't force 64-bit compilation mode. */
    /* Please note that we ignore -mcpu=, -march= and -mtune=, because neither
     * Clang nor GCC autoswitch from -m32 to -m64 just by -march=k8.
     */
    /* gcc: `-mcpu=' is deprecated. Use `-mtune=' or '-march=' instead. */
    if (0 == strcmp(arg, "-target") && argv[1]) {  /* Clang. */
      /* -target=... doesn't work in Clang, so we don't match it. */
      const char *p;
      for (p = argv[1]; *p != '-' && *p != '\0'; ++p) {
        /* Matches amd64 and x86_64. */
        if (p[0] == '6' && p[1] == '4') return ARCHBIT_64;
      }
      return ARCHBIT_32;
    } else if (0 == strcmp(arg, "-m32")) {
      return ARCHBIT_32;
    } else if (0 == strcmp(arg, "-m64")) {
      return ARCHBIT_64;
    }
  }
  return ARCHBIT_UNSPECIFIED;
}

static archbit_t get_archbit_override(archbit_t archbit_detected) {
  struct utsname ut;
  if (archbit_detected >= 0) return ARCHBIT_DETECTED;
  if (uname(&ut) < 0) return ARCHBIT_DETECTED;
  if (strstr(ut.machine, "64")) {  /* 64-bit kernel. */
    /* Since nowadays there isn't anything useful in /lib directly, let's
     * see if /bin/sh is a 64-bit ELF executable.
     */
    int fd = open("/bin/sh", O_RDONLY);
    int got;
    char buf[5];
    if (fd < 0) return ARCHBIT_DETECTED;
    if ((got = read(fd, buf, 5)) < 5) {
      close(fd);
      return ARCHBIT_DETECTED;
    }
    close(fd);
    /* 64-bit ELF binary. */
    if (0 == memcmp(buf, "\177ELF\002", 5)) return ARCHBIT_64;
  }
  return ARCHBIT_32;
}

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
  /* It's important that we don't trigger when argv[1] is "-cc1", because
   * clang is calling itself in this case.
   */
  if (0) {
#ifdef USE_XSTATIC
  } else if (argv[1] && 0 == strcmp(argv[1], "-xstatic")) {
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
#endif
  } else if (!argv[1] || 0 != strcmp(argv[1], "-cc1")) {
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
