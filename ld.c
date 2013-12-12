#define ALONE \
    set -ex; exec ${CC:-gcc} -s -Os -fno-stack-protector \
    -W -Wall -o ld "$0"; exit 1

/*
 * ld.c: ld trampoline for `clang -xstatic' linking
 * by pts@fazekas.hu at Thu Dec 12 02:57:47 CET 2013
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static char *strdupcat2(const char *a, const char *b) {
  const size_t sa = strlen(a), sb = strlen(b);
  char *p = malloc(sa + sb + 1);
  memcpy(p, a, sa);
  strcpy(p + sa, b);
  return p;
}

static char is_dirprefix(const char *s, const char *prefix) {
  const char const *p = prefix + strlen(prefix);
  return 0 == strncmp(s, prefix, p - prefix) && (*p == '\0' || *p == '/');
}

int main(int argc, char **argv) {
  /* All we do is exec()ing ld.bin with the following dropped from argv:
   *
   * -L/usr/lib...
   * -L/lib...
   * --hash-style=both (because not supported by old ld)
   * --build-id (because not supported by old ld)
   */
  char **args, **argp, *arg;
  char *argv0 = argv[0];

  argp = args = malloc(sizeof(*args) * (argc + 1));
  *argp++ = *argv++;
  for (; (arg = *argv); ++argv) {
    if (0 != strcmp(arg, "--hash-style=both") &&
        0 != strcmp(arg, "--build-id") &&
        !is_dirprefix(arg, "-L/usr/lib") &&
        !is_dirprefix(arg, "-L/lib")) {
      *argp++ = *argv;
    }
  }
  *argp = NULL;
  execv(strdupcat2(argv0, ".bin"), args);
  return 120;
}
