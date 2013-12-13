#define ALONE \
    set -ex; exec ${CC:-gcc} -s -Os -fno-stack-protector \
    -W -Wall -o clang_noxstatic "$0"; exit 1

/*
 * clang.c: clang trampoline for .so file redirection
 * by pts@fazekas.hu at Fri Dec 13 22:17:42 CET 2013
 */

#define _GNU_SOURCE 1  /* Needed for get_current_dir_name() */
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
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
  memcpy(argp, argv + 1, argc * sizeof(*argp));
  ldso0 = strdupcat(dir, "/../binlib/ld0.so", "");
  execv(ldso0, args);
  p = strdupcat("error: clang: exec failed: ", prog, "\n");
  (void)!write(2, p, strlen(p));
  return 120;
}
