#define ALONE \
    set -ex; exec ${CC:-gcc} -s -Os -fno-stack-protector \
    -W -Wall -o clang "$0"; exit 1

/*
 * clang.c: clang trampoline for .so file redirection
 * by pts@fazekas.hu at Thu Dec 12 01:41:42 CET 2013
 */

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

int main(int argc, char **argv) {
  /* TODO(pts): What if argv[0] doesn't contain a slash (e.g. bash before
   * rehash)? In that case a readlink on /proc/self/exe solves it.
   */
  char *prog;
  char *dir = argv[0][0] == '\0' ? strdup("x") :
      strdup(readlink_alloc_all(argv[0]));
  char *p;
  char **args, **argp;
  char is_cxx;
  for (p = dir + strlen(dir); p != dir && p[-1] != '/'; --p) {}
  is_cxx = strstr(p, "++") ? 1 : 0;
  if (p == dir) {
    strcpy(dir, ".");
  } else {
    p[-1] = '\0';
  }
  putenv(strdupcat("LD_LIBRARY_PATH=", dir, "/../binlib"));
  /* clang was doing:
   * readlink("/proc/self/exe", ".../clang/binlib/ld-linux.so.2", ...)
   * ... and believed it's ld-linux.so.2. I edited the binary to /proc/self/exE
   * to fix it.
   */
  argp = args = malloc(sizeof(*args) * (argc + 10));
  *argp++ = argv[0];  /* No effect, `clang.bin: error: no input files'. */
  *argp++ = prog = strdupcat(dir, "/clang.bin", "");
  /* Needed, because clang can't detect C++ness from clang.bin. */
  if (is_cxx) *argp++ = "-ccc-cxx";
  if (argv[1] && 0 == strcmp(argv[1], "-xstatic")) {
    ++argv;
    --argc;
    /* When adding more arguments here, increase the args malloc count. */
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
  }
  memcpy(argp, argv + 1, argc * sizeof(*argp));
  execv(strdupcat(dir, "/../binlib/ld-linux.so.2", ""), args);
  p = strdupcat("error: clang: exec failed: ", prog, "\n");
  write(2, p, strlen(p));
  return 120;
}
