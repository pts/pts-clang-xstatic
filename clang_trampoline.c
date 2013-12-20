#define ALONE \
    set -ex; ${CC:-gcc} -s -Os -fno-stack-protector \
    -W -Wall -o clang_trampoline "$0"; exit 0
/*
 * clang_trampline.c: clang and ld trampoline for .so file redir and -xstatic
 * by pts@fazekas.hu at Fri Dec 13 22:17:42 CET 2013
 *
 * This program examines its argv, modifies some args, and exec()s "clang.bin"
 * or "ld.bin".
 *
 * Since our process is short-lived, we don't bother free()ing memory.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define _GNU_SOURCE 1  /* Needed for get_current_dir_name() */

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
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
       /* It's important that we don't match --do-xstaticcld here. */
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
  const size_t sprefix = strlen(prefix);
  return 0 == strncmp(s, prefix, sprefix) && (
      s[sprefix] == '\0' || s[sprefix] == '/');
}

static char is_argprefix(const char *s, const char *prefix) {
  const size_t sprefix = strlen(prefix);
  return 0 == strncmp(s, prefix, sprefix) && (
      s[sprefix] == '\0' || s[sprefix] == '=');
}

static char ends_with(const char *arg, const char *suffix) {
  const size_t ssuffix = strlen(suffix);
  const size_t sarg = strlen(arg);
  return sarg >= ssuffix && 0 == strcmp(arg + sarg - ssuffix, suffix);
}

static char check_bflags(char **argv, char is_xstaticcld_ok) {
  char **argi, *arg;
  char had_xstaticcld = 0;
  for (argi = argv + 1; (arg = *argi); ++argi) {
    if (arg[0] == '-' && arg[1] == 'B' &&
        is_xstaticcld_ok && ends_with(arg, "/xstaticcld")) {
      had_xstaticcld = 1;
    } else if (
        (arg[0] == '-' && arg[1] == 'B') ||
        (!is_xstaticcld_ok && is_argprefix(arg, "--sysroot")) ||
        is_argprefix(arg, "--gcc-toolchain")) {
      fdprint(2, strdupcat(
          "error: flag not supported in this ldmode: ", arg, "\n"));
      exit(122);
    }
  }
  return had_xstaticcld;
}

static char detect_need_linker(char **argv) {
  char **argi, *arg, c;
  for (argi = argv + 1; (arg = *argi); ++argi) {
    if (arg[0] != '-') continue;
    c = arg[1];
    /* E.g. with "-E" the linker won't be invoked. */
    if ((c == 'M' && arg[2] == 'M' && arg[3] == '\0') ||
        ((c == 'E' || c == 'S' || c == 'c' || c == 'M') && arg[2] == '\0')) {
      return 0;
    }
  }
  return 1;
}

static void detect_nostdinc(char **argv,
                            /* Output bool arguments. */
                            char *has_nostdinc, char *has_nostdincxx) {
  char **argi, *arg;
  *has_nostdinc = *has_nostdincxx = 0;
  for (argi = argv + 1; (arg = *argi); ++argi) {
    if (0 == strcmp(arg, "-nostdinc")) {
      *has_nostdinc = 1;
    } else if (0 == strcmp(arg, "-nostdinc++")) {
      *has_nostdincxx = 1;
    }
  }
}

/** Return NULL if not found. Return cmd if it contains a slash. */
static char *find_on_path(const char *cmd) {
  const char *path;
  char *pathname;
  const char *p, *q;
  size_t scmd, s;
  struct stat st;
  if (strstr(cmd, "/")) {
    if (0 == stat(cmd, &st) && !S_ISDIR(st.st_mode)) {
      return strdupcat(cmd, "", "");
    }
  } else {
    path = getenv("PATH");
    if (!path || !path[0]) path = "/bin:/usr/bin";
    scmd = strlen(cmd);
    for (p = path; *p != '\0'; p = q + 1) {
      for (q = p; *q != '\0' && *q != ':'; ++q) {}
      s = q - p;
      pathname = malloc(s + 2 + scmd);
      memcpy(pathname, p, s);
      pathname[s] = '/';
      strcpy(pathname + s + 1, cmd);
      if (0 == stat(pathname, &st) && !S_ISDIR(st.st_mode)) return pathname;
      free(pathname);  /* TODO(pts): Realloc. */
      if (*q == '\0') break;
    }
  }
  return NULL;
}

typedef struct lang_t {
  char is_cxx;
  char is_clang;
  char is_compiling;
} lang_t;

static void detect_lang(const char *prog, char **argv, lang_t *lang) {
  char **argi;
  const char *arg, *basename, *ext;
  lang->is_compiling = 0;
  arg = prog;
  for (basename = arg + strlen(arg); basename != arg && basename[-1] != '/';
       --basename) {}
  lang->is_cxx = strstr(basename, "++") != NULL;
  lang->is_clang = strstr(basename, "clang") != NULL;
  for (argi = argv + 1; (arg = *argi); ++argi) {
    if (0 == strcmp(arg, "-xc++")) {
      lang->is_cxx = 1;
    } else if (0 == strcmp(arg, "-ccc-cxx")) {  /* Enable C++ for Clang. */
      lang->is_cxx = 1;
      /* -ccc-cxx is a Clang-specific flag, gcc doesn't have it.*/
      lang->is_clang = 1;
    } else if (0 == strcmp(arg, "-x") && argi[1]) {
      lang->is_cxx = 0 == strcmp(*++argi, "c++");
    } else if (arg[0] != '-') {
      for (ext = arg + strlen(arg);
           ext != arg && ext[-1] != '.' && ext[-1] != '/';
           --ext) {}
      if (ext == basename) ext = "";
      if (0 == strcmp(ext, "cc") ||
          0 == strcmp(ext, "cp") ||
          0 == strcmp(ext, "cxx") ||
          0 == strcmp(ext, "cpp") ||
          0 == strcmp(ext, "CPP") ||
          0 == strcmp(ext, "c++") ||
          0 == strcmp(ext, "C")) {
        lang->is_cxx = 1;
        lang->is_compiling = 1;
      } else if (0 == strcmp(ext, "c") ||
                 0 == strcmp(ext, "i") ||
                 0 == strcmp(ext, "ii") ||
                 0 == strcmp(ext, "m") ||
                 0 == strcmp(ext, "mi") ||
                 0 == strcmp(ext, "mm") ||
                 0 == strcmp(ext, "M") ||
                 0 == strcmp(ext, "mii") ||
                 0 == strcmp(ext, "h") ||
                 0 == strcmp(ext, "H") ||
                 0 == strcmp(ext, "hp") ||
                 0 == strcmp(ext, "hxx") ||
                 0 == strcmp(ext, "hpp") ||
                 0 == strcmp(ext, "HPP") ||
                 0 == strcmp(ext, "h++") ||
                 0 == strcmp(ext, "tcc") ||
                 0 == strcmp(ext, "s") ||
                 0 == strcmp(ext, "S") ||
                 0 == strcmp(ext, "sx")) {
        /* Extensions taken from `man gcc', Clang supports only a subset:
         * C, C++, Objective C and Objective C++. Fortran, ADA, Go, Java etc.
         * skipped.
         */
        lang->is_compiling = 1;
      }
    }
  }
}

typedef enum ldmode_t {
  LM_XCLANGLD = 0,  /* Use the ld and -lgcc shipped with clang. */
  LM_XSYSLD = 1,
  LM_XSTATIC = 2,
} ldmode_t;

int main(int argc, char **argv) {
  char *prog, *ldso0, *argv0;
  char *dir, *dirup;
  char *p;
  char **args, **argp;
  const char *febemsg = "frontend";  /* Or "backend". */
  char is_verbose = 0;
  ldmode_t ldmode;
  char **argi, *arg;
  char has_nostdinc, has_nostdincxx;

  dir = find_on_path(argv[0]);
  if (!dir) {
    fdprint(2, "xstatic: error: could not find myself on $PATH\n");
    return 122;
  }
  dir = readlink_alloc_all(dir);
  for (p = dir + strlen(dir); p != dir && p[-1] != '/'; --p) {}
  if (p == dir) {
    strcpy(dir, ".");
  } else {
    p[-1] = '\0';
  }
  dirup = strdupcat(dir, "", "");
  for (p = dirup + strlen(dirup); p != dirup && p[-1] != '/'; --p) {}
  if (p == dirup || (dirup[0] == '/' && dirup[1] == '\0')) {
    fdprint(2, strdupcat("xstatic: error: no parent dir for: ", dir, "\n"));
    return 122;
  }
  p[-1] = '\0';  /* Remove basename of dirup. */

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
    argp = args = malloc(sizeof(*args) * (argc + 5));
    *argp++ = argv0 = *argv;
    ldmode = LM_XSTATIC;
    for (argi = argv + 1; (arg = *argi); ++argi) {
      if (0 == strcmp(arg, "--do-xclangld")) {
        ldmode = LM_XCLANGLD;
      } else if (0 == strcmp(arg, "--do-clangldv")) {
        is_verbose = 1;
      }
    }
    if (ldmode == LM_XCLANGLD) {
      for (argi = argv + 1; (arg = *argi); ++argi) {
        if (0 != strcmp(arg, "--do-xclangld") &&
            0 != strcmp(arg, "--do-clangldv")) {
          *argp++ = *argi;
        }
      }
    } else {  /* ldmode == LM_XSTATIC. */
      *argp++ = "-nostdlib";  /* No system directories to find .a files. */
      /* We put xstaticfld with
       libgcc.a first, because clang puts
       * /usr/lib/gcc/i486-linux-gnu/4.4 with libgcc.a before /usr/lib with
       * libc.a .
       */
      *argp++ = strdupcat("-L", dirup, "/xstaticfld");
      *argp++ = strdupcat("-L", dirup, "/uclibcusr/lib");
      for (argi = argv + 1; (arg = *argi); ++argi) {
        if (0 == strcmp(arg, "-z") &&
            argi[1] && 0 == strcmp(argi[1], "relro")) {
          /* Would increase size. */
          ++argi;  /* Skip and drop both arguments: -z relro */
        } else if (
            0 == strcmp(arg, "-nostdlib") ||
            0 == strcmp(arg, "--do-clangcld") ||
            0 == strcmp(arg, "--do-clangldv") ||
            0 == strncmp(arg, "--hash-style=", 13) ||  /* Would increase size.*/
            0 == strcmp(arg, "--build-id") ||
            /* -L/usr/local/lib is not emitted by clang 3.3; we play safe. */
            is_dirprefix(arg, "-L/usr/local/lib") ||
            is_dirprefix(arg, "-L/usr/lib") ||
            is_dirprefix(arg, "-L/lib") ||
            0 == strncmp(arg, "--sysroot=", 10)) {
          /* Omit this argument. */
        } else if (0 == strcmp(arg, "-lstdc++")) {
          /* Fixup for linker errors: undefined reference to
           *`_Unwind_SjLj_Unregister'.
           */
          *argp++ = arg;
          *argp++ = "-lstdc++usjlj";  /* The order is important. */
        } else {
          *argp++ = *argi;
        }
      }
    }
    *argp = NULL;
    prog = strdupcat(argv0, ".bin", "");  /* "ld.bin". */
    args[0] = prog;
    if (is_verbose) {
      fdprint(2, escape_argv("info: running ld:\n", args, "\n"));
    }
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
  argp = args = malloc(sizeof(*args) * (argc + 17));
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
  if (!argv[1] || (!argv[2] && 0 == strcmp(argv[1], "-v"))) {
    /* Don't add any flags, because the user wants some version info, and with
     * `-Wl,... -v' gcc and clang won't display version info.
     */
    is_verbose = 0;
    if (argv[1]) {
      pid_t pid = fork();
      if (pid < 0) exit(124);
      if (pid != 0) {  /* Parent. */
        int status;
        pid_t pid2 = waitpid(pid, &status, 0);
        if (pid != pid2) exit(124);
        if (status != 0) {
          exit(WIFEXITED(status) ? WEXITSTATUS(status) : 124);
        }
        /* Print at the end of the output, after exec. */
        fdprint(1, "Additinal flags supported: -xstatic -xsysld\n");
        exit(0);
      }
    }
  } else if (argv[1] && 0 == strcmp(argv[1], "--help")) {
    fdprint(1, "Additinal flags supported: -xstatic -xsysld\n");
  } else if (ldmode == LM_XSTATIC) {
    struct stat st;
    char need_linker;
    lang_t lang;
    check_bflags(argv, 0);
    need_linker = detect_need_linker(argv);
    detect_nostdinc(argv, &has_nostdinc, &has_nostdincxx);
    detect_lang(argv[0], argv, &lang);

    /* When adding more arguments here, increase the args malloc count. */
    /* We don't need get_autodetect_archflag(argv), we always send "-m32". */
    *argp++ = "-m32";
    *argp++ = "-static";
    if (lang.is_compiling) {
      /*
       * Without this we get the following error compiling binutils 2.20.1:
       * chew.c:(.text+0x233f): undefined reference to `__stack_chk_fail'
       * We can't implement this in a compatible way, glibc gcc generates %gs:20,
       * uClibc-0.9.33 has symbol __stack_chk_guard.
       */
      *argp++ = "-fno-stack-protector";
      *argp++ = "-nostdinc";
      *argp++ = "-nostdinc++";
      if (lang.is_cxx && !has_nostdincxx) {
        /* It's important not to use the Clang flag -cxx-isystem here, because
         * that adds C++ includes after C includes, but we need the other way
         * round (because of conflicting files, e.g. complex.h, tgmath.h,
         * fenv.h).
         */
        *argp++ = "-isystem";
        *argp++ = strdupcat(dirup, "/uclibcusr/c++include", "");
      }
      if (!has_nostdinc) {
        p = strdupcat(dirup, "/uclibcusr/include", "");
        if (0 != stat(p, &st) || !S_ISDIR(st.st_mode)) goto dir_missing;
        *argp++ = "-isystem";
        *argp++ = p;

        /* It's important to keep clanginclude the last, for consistency.
         */
        p = strdupcat(dirup, "/clanginclude", "");
        if (0 != stat(p, &st) || !S_ISDIR(st.st_mode)) {
         dir_missing:
          fdprint(2, strdupcat(
              "error: directory missing for -xstatic, please install: ", p,
              "\n"));
          return 123;
        }
        *argp++ = "-isystem";
        *argp++ = p;
      }
    }
    p = strdupcat(dirup, "/xstaticfld/ld.bin", "");
    if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) {
     file_missing:
      fdprint(2, strdupcat(
          "error: file missing for -xstatic, please install: ", p, "\n"));
      return 123;
    }
    p = strdupcat(dirup, "/uclibcusr/lib/libc.a", "");
    if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) goto file_missing;
    p = strdupcat(dirup, "/uclibcusr/lib/crt1.o", "");
    if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) goto file_missing;
    p = strdupcat(dirup, "/xstaticfld/crtbeginT.o", "");
    if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) goto file_missing;
    /* The linker would be ../xstaticfld/ld, which is also a trampoline binary
     * of ours.
     */
    *argp++ = strdupcat("-B", dirup, "/xstaticfld");
    /* Run `clang -print-search-dirs' to confirm that it was properly
     * detected.
     */
    if (need_linker && is_verbose) {
      *argp++ = "-Wl,--do-clangldv";
    }
    for (++argv; (arg = *argv); ++argv) {
      if (0 == strcmp(arg, "-nostdinc") ||
          0 == strcmp(arg, "-nostdinc++")) {
      } else {
        *argp++ = arg;
      }
    }
    *argp++ = NULL;
    argc = 0;
  } else if (argv[1] && 0 == strcmp(argv[1], "-cc1")) {
    febemsg = "backend";
  } else {
    char need_linker;
    archbit_t archbit, archbit_override;
    lang_t lang;
    if (check_bflags(argv, ldmode == LM_XCLANGLD)) ldmode = LM_XSYSLD;
    need_linker = detect_need_linker(argv);
    detect_lang(argv[0], argv, &lang);
    detect_nostdinc(argv, &has_nostdinc, &has_nostdincxx);
    archbit = get_archbit_detected(argv);
    archbit_override = get_archbit_override(archbit);
    if (archbit_override == ARCHBIT_32) {
      *argp++ = "-m32";
      archbit = ARCHBIT_32;
    } else if (archbit_override == ARCHBIT_64) {
      *argp++ = "-m64";
      archbit = ARCHBIT_64;
    }

    if (ldmode == LM_XCLANGLD) {
      /* Use libgcc.a, srtbegin*.o etc. from our clangld directory. */
      if (archbit == ARCHBIT_64) {
        /* Unfortunately it's not easy to give a helpful error message if
         * /usr/lib/crt1.o is 32-bit and there is no 64-bit equivalent.
         * Currently the first error is: `clangld64/crtbegin.o:
         * incompatible target'.
         * TODO(pts): In linker mode, check the 64-bitness of crt1.o and
         * compatibility with `-m elf_x86_64'.
         */
        *argp++ = strdupcat("-B", dirup, "/clangld64");
      } else {
        *argp++ = strdupcat("-B", dirup, "/clangld32");
      }
      if (need_linker) {
        *argp++ = "-static-libgcc";
        *argp++ = "-Wl,--do-xclangld";
        if (is_verbose) *argp++ = "-Wl,--do-clangldv";
      }
    } else {  /* ldmode == LM_XSYSLD. */
      /* If !need_linker, avoid clang warning about unused linker input. */
      if (need_linker) {
        *argp++ = "-Wl,-nostdlib";  /* -L/usr and equivalents added by clang. */
      }
    }
    if (lang.is_compiling) {
      if (!has_nostdinc) {
        *argp++ = "-idirafter";
        *argp++ = strdupcat(dirup, "/clanginclude", "");
      }
      if (archbit == ARCHBIT_64 && !has_nostdinc) {
        /* Let the compiler find our replacement for gnu/stubs-64.h on 32-bit
         * systems which don't provide it.
         */
        *argp++ = "-idirafter";  /* Add with low priority. */
        *argp++ = strdupcat(dirup, "/clanginc64low", "");
      }
    }
  }
  memcpy(argp, argv + 1, argc * sizeof(*argp));
  ldso0 = strdupcat(dirup, "/binlib/ld0.so", "");
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
