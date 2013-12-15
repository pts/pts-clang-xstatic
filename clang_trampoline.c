#define ALONE \
    set -ex; ${CC:-gcc} -s -Os -fno-stack-protector \
    -W -Wall -o clang_noxstatic "$0"; \
    ${CC:-gcc} -s -Os -fno-stack-protector \
    -W -Wall -DUSE_XSTATIC -o clang_xstatic "$0"; \
    exit 0
/*
 * clang_trampline.c: clang and ld trampoline for .so file redir and -xstatic
 * by pts@fazekas.hu at Fri Dec 13 22:17:42 CET 2013
 *
 * This program examines its argv, modifies some args, and exec()s "clang.bin"
 * or "ld.bin".
 *
 * Since our process is short-lived, we don't bother free()ing memory.
 */

#define _GNU_SOURCE 1  /* Needed for get_current_dir_name() */

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

/** Return a newly malloc()ed string containing prefix + escaped(argv) +
 * suffix, the caller takes ownership.
 */
static char *escape_argv(const char *prefix, char **argv, char *suffix) {
  size_t sprefix = strlen(prefix);
  /* An upper limit of the output size. */
  size_t size = sprefix + strlen(suffix);
  char **argi, *arg;
  char *out, *p, c;
  for (argi = argv; (arg = *argi); ++argi) {
    size += 3 + strlen(arg);  /* Space (or '\0') plus the single quotes. */
    for (; *arg; ++arg) {
      if (*arg == '\'') size += 3;
    }
  }
  p = out = malloc(size);
  memcpy(p, prefix, sprefix);
  p += sprefix;
  for (argi = argv; (arg = *argi); ++argi) {
    char is_simple = (*arg != '=') && (*arg != '\0');
    if (is_simple) {
      for (; (c = *arg) != '\0' || (c == '+' || c == '-' || c == '_' ||
             c == '=' || c == '.' || c == '/' ||
             ((c | 32) - 'a' + 0U) <= 'z' - 'a' + 0U ||
             (c - '0' + 0U) <= 9U); ++arg) {}
      is_simple = (*arg == '\0');
      arg = *argi;
    }
    if (is_simple) {  /* Append unescaped. */
      sprefix = strlen(arg);
      memcpy(p, arg, sprefix);
      p += sprefix;
    } else {  /* Escape with single quotes. */
      *p++ = '\'';
      for (; (c = *arg); ++arg) {
        if (c == '\'') {
          *p++ = '\'';
          *p++ = '\\';
          *p++ = '\'';
        }
        *p++ = c;
      }
      *p++ = '\'';
    }
    *p++ = ' ';
  }
  strcpy(--p, suffix);  /* `--' to replace the space. */
  return out;
}

/** Return true iff we've received a linker command-line. */
static char detect_linker(char **argv) {
  char **argi, *arg;
  for (argi = argv + 1; (arg = *argi) &&
       /* We could also use `-z relro' or `-m elf_i386' or ther values. */
       0 != strcmp(arg, "--eh-frame-hdr") &&  /* By clang. */
       0 != strcmp(arg, "--build-id") &&  /* By clang and gcc. */
       0 != strncmp(arg, "--hash-style=", 13) &&
       0 != strcmp(arg, "--do-xclangld") &&
       0 != strcmp(arg, "-dynamic-linker") &&  /* By clang. */
       0 != strcmp(arg, "--start-group") &&  /* By gcc/clang -static. */
       0 != strcmp(arg, "--as-needed") &&  /* By gcc/clang witout -static. */
       0 != strcmp(arg, "--no-as-needed"); ++argi) {}
  return ! !*argi;
}

static void fdprint(int fd, const char *msg) {
  const size_t smsg = strlen(msg);
  if (smsg != 0U + write(fd, msg, smsg)) exit(121);
}

static char is_dirprefix(const char *s, const char *prefix) {
  const char const *p = prefix + strlen(prefix);
  return 0 == strncmp(s, prefix, p - prefix) && (*p == '\0' || *p == '/');
}

typedef enum ldmode_t {
  LM_XCLANGLD = 0,  /* Use the ld and -lgcc shipped with clang. */
  LM_XSYSLD = 1,
  LM_XSTATIC = 2,
} ldmode_t;

int main(int argc, char **argv) {
  char *prog, *ldso0, *argv0;
  char *dir = argv[0][0] == '\0' ? strdup("x") : strdup(readlink_alloc_all(
      strstr(argv[0], "/") ? argv[0] : "/proc/self/exe"));
  char *p;
  char **args, **argp;
  const char *febemsg = "frontend";  /* Or "backend". */
  char is_verbose = 0;
  ldmode_t ldmode;
  char **argi, *arg;

  for (p = dir + strlen(dir); p != dir && p[-1] != '/'; --p) {}
  if (p == dir) {
    strcpy(dir, ".");
  } else {
    p[-1] = '\0';
  }
  if (detect_linker(argv)) {  /* clang trampoline runs as as ld. */
    /* Please note that we are only invoked for `clang -xstatic', because
     * without -xstatic, the ld symlink is outside of the clang program search
     * path, because -gcc-toolchain was not specified.
     */
    /* All we do is exec()ing ld.bin with the following dropped from argv:
     *
     * -L/usr/lib...
     * -L/lib...
     * --hash-style=both (because not supported by old ld)
     * --build-id (because not supported by old ld)
     * -z relro (because it increases the binary size and it's useless for static)
     */
    argp = args = malloc(sizeof(*args) * (argc + 4));
    *argp++ = argv0 = *argv;
    ldmode = LM_XSTATIC;
    for (argi = argv + 1; (arg = *argi); ++argi) {
      if (0 == strcmp(arg, "--do-xclangld")) {
        ldmode = LM_XCLANGLD;
      }
    }
    if (ldmode == LM_XCLANGLD) {
      for (argi = argv + 1; (arg = *argi); ++argi) {
        if (0 != strcmp(arg, "--do-xclangld")) {
          *argp++ = *argi;
        }
      }
    } else {  /* ldmode == LM_XSTATIC. */
      *argp++ = "-nostdlib";  /* No system directories to find .a files. */
      /* We put xstaticld with
       libgcc.a first, because clang puts
       * /usr/lib/gcc/i486-linux-gnu/4.4 with libgcc.a before /usr/lib with
       * libc.a .
       */
      *argp++ = strdupcat("-L", dir, "/../xstaticld");
      *argp++ = strdupcat("-L", dir, "/../usr/lib");
      for (argi = argv + 1; (arg = *argi); ++argi) {
        if (0 == strcmp(arg, "-z") &&
            argi[1] && 0 == strcmp(argi[1], "relro")) {
          ++argi;  /* Skip and drop both arguments: -z relro */
        } else if (0 != strcmp(arg, "--hash-style=both") &&
            0 != strcmp(arg, "--build-id") &&
            /* -L/usr/local/lib is not emitted by clang 3.3; we play safe. */
            !is_dirprefix(arg, "-L/usr/local/lib") &&
            !is_dirprefix(arg, "-L/usr/lib") &&
            !is_dirprefix(arg, "-L/lib")) {
          *argp++ = *argi;
        }
      }
    }
    *argp = NULL;
    prog = strdupcat(argv0, ".bin", "");  /* "ld.bin". */
    args[0] = prog;
#if 0  /* Can't detect if `clang -v' was specified, so don't pollute. */
    fdprint(2, escape_argv("info: running ld:\n", args, "\n"));
#endif
    execv(prog, args);
    fdprint(2, strdupcat("error: clang-ld: exec failed: ", prog, "\n"));
    return 120;
  }

  /* RPATH=$ORIGIN/../binlib (set manually in clang.bin) takes care. */
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
  argp = args = malloc(sizeof(*args) * (argc + 16));
  *argp++ = argv0 = argv[0];  /* No effect, will be ignored. */
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
  for (argi = argv + 1; (arg = *argi) && 0 != strcmp(arg, "-v"); ++argi) {}
  if (*argi) is_verbose = 1;
  /* It's important that -xstatic doesn't trigger when argv[1] is "-cc1",
   * because clang is calling itself in this case.
   */
  if (argv[1] &&
      (0 == strcmp(argv[1], "-xstatic") || 0 == strcmp(argv[1], "--xstatic"))) {
    ++argv;
    --argc;
    ldmode = LM_XSTATIC;
  } else if (argv[1] &&
      (0 == strcmp(argv[1], "-xsysld") || 0 == strcmp(argv[1], "--xsysld"))) {
    ++argv;
    --argc;
    ldmode = LM_XSYSLD;
  } else {
    ldmode = LM_XCLANGLD;
  }
#if USE_XSTATIC
  if (0) {
#else
  if (ldmode == LM_XSTATIC) {
    fdprint(2, "error: flag not supported: -xstatic\n");
    exit(122);
#endif
  } else if (!argv[1] || (!argv[2] && 0 == strcmp(argv[1], "-v"))) {
    /* Don't add any flags, because the user wants some version info, and with
     * `-Wl,... -v' gcc and clang won't display version info.
     */
    is_verbose = 0;
#if USE_XSTATIC
  } else if (ldmode == LM_XSTATIC) {
    struct stat st;
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
    *argp++ = "-nostdinc++";
    *argp++ = "-isystem";
    p = strdupcat(dir, "/../clanginclude", "");
    if (0 != stat(p, &st) || !S_ISDIR(st.st_mode)) {
     dir_missing:
      fdprint(2, strdupcat(
          "error: directory missing for -xstatic, please install: ", p, "\n"));
      return 123;
    }
    *argp++ = p;
    /* TODO(pts): Move C++ includes before C includes (-cxx-isystem doesn't
     * matter, it will be added after -isystem) just like in regular clang.
     */
    *argp++ = "-cxx-isystem";
    *argp++ = strdupcat(dir, "/../usr/c++include", "");
    *argp++ = "-isystem";
    p = strdupcat(dir, "/../usr/include", "");
    if (0 != stat(p, &st) || !S_ISDIR(st.st_mode)) goto dir_missing;
    *argp++ = p;
    p = strdupcat(dir, "/../xstaticld/ld.bin", "");
    if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) {
     file_missing:
      fdprint(2, strdupcat(
          "error: file missing for -xstatic, please install: ", p, "\n"));
      return 123;
    }
    p = strdupcat(dir, "/../usr/lib/libc.a", "");
    if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) goto file_missing;
    p = strdupcat(dir, "/../usr/lib/crt1.o", "");
    if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) goto file_missing;
    p = strdupcat(dir, "/../xstaticld/crtbeginT.o", "");
    if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) goto file_missing;
    /* The linker would be ../xstaticld/ld, which is also a trampoline binary of
     * ours.
     */
    *argp++ = strdupcat("-B", dir, "/../xstaticld");
    /* Run `clang -print-search-dirs' to confirm that it was properly
     * detected.
     */
#endif
  } else if (argv[1] && 0 == strcmp(argv[1], "-cc1")) {
    febemsg = "backend";
  } else {
    char need_linker = 1, c;
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
    if (ldmode == LM_XCLANGLD) {
      /* Use libgcc.a, srtbegin*.o etc. from our clangld directory. */
      *argp++ = strdupcat("-B", dir, "/../clangld");
      *argp++ = "-static-libgcc";
      if (need_linker) {
        *argp++ = "-Wl,--do-xclangld";
      }
    } else {
      if (need_linker) {
        *argp++ = "-Wl,-nostdlib";  /* -L/usr and equivalents added by clang. */
      }
    }
    *argp++ = "-isystem";
    *argp++ = strdupcat(dir, "/../clanginclude", "");
    /* If !need_linker, avoid clang warning about unused linker input. */
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
  if (is_verbose) {
    args[0] = ldso0;
    fdprint(2, escape_argv(
        strdupcat("info: running clang ", febemsg, ":\n"), args, "\n"));
  }
  args[0] = argv0;
  execv(ldso0, args);
  fdprint(2, strdupcat("error: clang: exec failed: ", prog, "\n"));
  return 120;
}
