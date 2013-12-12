#define ALONE \
    set -ex; exec ${CC:-gcc} -s -O2 -W -Wall -o clang "$0"; exit 1

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

int main(int argc, char **argv) {
  char *dir = argv[0][0] == '\0' ? strdup("x") : strdup(argv[0]);
  char *p;
  char **args, **argp;
  for (p = dir; p != dir && p[-1] != '/'; --p) {}
  if (p == dir) {
    strcpy(dir, ".");
  } else {
    p[-1] = '\0';
  }
  putenv(strdupcat("LD_LIBRARY_PATH=", dir, "/../binlib"));
  argp = args = malloc(sizeof(*args) * (argc + 2));
  *argp++ = argv[0];  /* No effect, argv[0] would be ld-linux.so.2. */
  *argp++ = strdupcat(argv[0], ".bin", "");
  memcpy(argp, argv + 1, argc * sizeof(*argp));
  execv(strdupcat(dir, "/../binlib/ld-linux.so.2", ""), args);
  return 120;
}
