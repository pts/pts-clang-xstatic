#define ALONE \
    set -ex; ${CC:-gcc} -s -Os -fno-stack-protector \
    -W -Wall -o clang_trampoline "$0"; exit 0
/*
 * clang_trampline.c: clang trampoline for pts-clang
 * by pts@fazekas.hu at Fri Dec 13 22:17:42 CET 2013
 *
 * This program examines its argv, modifies some args, and exec()s "clang.bin".
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

#ifdef USE_MINIINC
#include <miniinc.h>
#else
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

static char *readlink_slash_alloc(char *path) {
  char *p, *q, c;
  for (p = path + strlen(path); p != path && p[-1] == '/'; --p) {}
  if (p == path) return NULL;
  for (q = p; q != path && q[-1] == '/'; --q) {}
  for (; q != path && q[-1] != '/'; --q) {}
  /* Don't follow symlink if ends with /.. or /. */
  if (q[0] == '.' && (q[1] == '/' || q[1] == '\0' ||
      (q[1] == '.' && (q[2] == '/' || q[2] == '\0')))) return NULL;
  c = *p;
  *p = '\0';
  q = readlink_alloc(path);
  *p = c;
  return q;
}

/* Combines two pathnames to a newly allocated string. The . and .. components
 * in the beginning of path2 are processed specially (and correctly).
 *
 * base must end with a '/'.
 */
static char *combine_paths_dotdot_alloc(const char *base, const char *path2) {
  char *path1;
  if (path2[0] == '/') return strdupcat(path2, "", "");
  path1 = malloc(strlen(base) + strlen(path2) + 4);
  strcpy(path1, base);
  while (path2[0] == '.') {
    if (path2[1] == '/') {
      path2 += 2;
      for (; path2[0] == '/'; ++path2) {}
      continue;
    } else if (path2[1] == '.' && path2[2] == '/' && path1[0] != '\0') {
      /* TODO(pts): Also special-case path2 == "..". */
      char *p, *path3;
      struct stat sta, stb;
      char c;
      size_t spath1;
      path2 += 3;
      for (; path2[0] == '/'; ++path2) {}
     do_path1_again:
      spath1 = strlen(path1);
      /* TODO(pts): Add loop limit for infinite symlinks. */
      while ((path3 = readlink_slash_alloc(path1))) {
        char *path4;
        size_t spath3;
        /* TODO(pts): Handle ../ in the beginning of path3. */
        spath3 = strlen(path3);
        if (path3[0] == '/') {
          spath1 = 0;  /* Truncate on absolute symlink. */
        } else {
          for (p = path1 + spath1; p != path1 && p[-1] == '/'; --p) {}
          for (; p != path1 && p[-1] != '/'; --p) {}
          spath1 = p - path1;
          *p = '\0';
        }
        path4 = malloc(spath1 + spath3 + strlen(path2) + 5);
        memcpy(path4, path1, spath1);
        free(path1);
        path1 = path4;
        memcpy(p = path4 + spath1, path3, spath3);
        free(path3);
        p += spath3;
        if (spath3 != 0 && p[-1] != '/') *p++ = '/';
        *p = '\0';
        spath1 = p - path1;
      }
      strcpy(path1 + spath1, "../");
      if (0 != lstat(path1, &sta)) { dotdot_dumb:
        strcpy(path1 + spath1 + 3, path2);
        return path1;
      }
      p = path1 + spath1;
      for (; p != path1 && p[-1] == '/'; --p) {}
      if (p == path1) goto dotdot_dumb;  /* Absolute /. */
      for (; p != path1 && p[-1] != '/'; --p) {}
      if (p[0] == '.' && p[1] == '/') {
        if (p == path1) goto dotdot_dumb;
        *p = '\0';
        goto do_path1_again;
      } else if (p[0] == '.' && p[1] == '.' && p[2] == '/') {
        goto dotdot_dumb;
      }
      if (p == path1) {
        if (0 != lstat(".", &stb) || sta.st_dev != stb.st_dev ||
            sta.st_ino != stb.st_ino) goto dotdot_dumb;
      } else {
        c = p[0];
        p[0] = '\0';  // path1 now ends with '/', it's OK.
        if (0 != lstat(path1, &stb) ||
            sta.st_dev != stb.st_dev || sta.st_ino != stb.st_ino) {
          p[0] = c;
          goto dotdot_dumb;
        }
      }
    } else {
      break;
    }
  }
  strcat(path1, path2);
  return path1;
}

/* Resolve symlinks until a non-symlink is found. */
static char *readlink_alloc_all(const char *path) {
  char *path2, *path1 = strdup(path);
  while ((path2 = readlink_alloc(path1))) {
    if (path2[0] != '/') {
      char *p;
      for (p = path1 + strlen(path1); p != path1 && p[-1] != '/'; --p) {}
      if (p != path1) {
        *p = '\0';  /* Remove basename from path1. */
        p = combine_paths_dotdot_alloc(path1, path2);
        free(path2);
        path2 = p;
      }
    }
    free(path1);
    path1 = path2;
  }
  return path1;
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

/* Return a newly malloc()ed string containing prefix + escaped(argv) +
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

static void fdprint(int fd, const char *msg) {
  const size_t smsg = strlen(msg);
  if (smsg != 0U + write(fd, msg, smsg)) exit(121);
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

static void check_xflags(char **argv) {
  char **argi, *arg;
  for (argi = argv + 1; (arg = *argi); ++argi) {
    /* TODO(pts): Copy some of these flags to clang_trampoline.cc */
    if ((arg[0] == '-' && arg[1] == 'B') ||
        is_argprefix(arg, "--sysroot") ||
        is_argprefix(arg, "--gcc-toolchain") ||
        /* TODO(pts): Accept a few targets (and don't pass the flag). */
        0 == strcmp(arg, "-target") ||  /* Clang-specific. */
        0 == strcmp(arg, "-m64") ||
        0 == strcmp(arg, "-xsysld") ||
        0 == strcmp(arg, "--xsysld") ||
        0 == strcmp(arg, "-p") ||  /* Needs Mcrt1.o (| gcrt1.o), clang crt1.o */
        0 == strcmp(arg, "-pg") ||  /* Needs gcrt1.o, clang uses crt1.o */
        0 == strcmp(arg, "-pie") ||
        0 == strcmp(arg, "-fpic") ||
        0 == strcmp(arg, "-fPIC") ||
        0 == strcmp(arg, "-fpie") ||
        0 == strcmp(arg, "-fPIE") ||
        0 == strcmp(arg, "-shared") ||
        0 == strcmp(arg, "-shared-libgcc") ||
        0 == strcmp(arg, "-xermine") ||
        0 == strncmp(arg, "-xermine,", 9)) {
      fdprint(2, strdupcat(
          "error: flag not supported in ldmode -xstatic: ", arg, "\n"));
      exit(122);
    }
  }
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

/* Return NULL if not found. Return cmd if it contains a slash. */
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
  char is_cxx_prog;
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
  lang->is_cxx = lang->is_cxx_prog = strstr(basename, "++") != NULL;
  lang->is_clang = strstr(basename, "clang") != NULL;
  for (argi = argv + 1; (arg = *argi); ++argi) {
    if (0 == strcmp(arg, "-xc++")) {
      lang->is_cxx = 1;
    } else if (0 == strcmp(arg, "--driver-mode=g++") ||
               0 == strcmp(arg, "-ccc-cxx")) {  /* Enable C++ for Clang. */
      lang->is_cxx = 1;
      /* -ccc-cxx (between clang 3.0 and 3.3) and --driver-mode=g++ (Clang 3.4)
       * are Clang-specific flags, gcc doesn't have them.
       */
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

static char is_m32_or_m64(const char *arg) {
  return 0 == strcmp(arg, "-m32") || 0 == strcmp(arg, "-m64");
}

/* Returns a newly malloced string containing the directory 1 level above
 * the specified directory.
 */
static char *get_up_dir_alloc(const char *dir) {
  char *dirup = NULL, *p;
  if (0 == strcmp(dir, "..") || (dirup = readlink_alloc(dir))) {
    free(dirup);
    return strdupcat(dir, "/..", "");
  }
  dirup = strdupcat(dir, ".", "");
  for (p = dirup + strlen(dirup) - 1; p != dirup && p[-1] != '/'; --p) {}
  if (p == dirup) {
    *p++ = '.';
    if (0 == strcmp(dir, ".")) *p++ = '.';
    *p = '\0';
  } else if (dirup[0] == '/' && dirup[1] == '\0') {
    fdprint(2, strdupcat("error: no parent dir for: ", dir, "\n"));
    exit(126);
  } else {
    p[-1] = '\0';  /* Remove basename of dirup. */
  }
  return dirup;
}

static char *get_cxx_flag(const char *version_suffix) {
  /* TODO(pts): Make it work for -3.5 etc. in the future. */
  return 0 == strcmp(version_suffix, "-3.4") ? "--driver-mode=g++" : "-ccc-cxx";
}

typedef enum ldmode_t {
  LM_XCLANGLD = 0,  /* Use the ld and -lgcc shipped with clang. */
  LM_XSYSLD = 1,
  LM_XSTATIC = 2,
} ldmode_t;

int main(int argc, char **argv) {
  char *argv0;
  char *argv0_base;  /* The basename of argv[0], after following symlinks. */
  char *version_suffix;
  char *dir, *dirup;
  char *p;
  char **args, **argp;
  char is_verbose = 0;
  ldmode_t ldmode;
  char **argi, *arg;
  char has_nostdinc, has_nostdincxx;

  dir = find_on_path(argv[0]);
  if (!dir) {
    fdprint(2, "error: could not find myself on $PATH\n");
    return 126;
  }
  dir = readlink_alloc_all(dir);
  for (p = dir + strlen(dir); p != dir && p[-1] != '/'; --p) {}
  if (p == dir) {
    dir = strdupcat("./", dir, "");
    dir[1] = '\0';  /* Replace slash. */
    argv0_base = dir + 2;
  } else {
    argv0_base = p;
    for (; p != dir && p[-1] == '/'; --p) {}
    p[p == dir] = '\0';  /* Remove basename of dir. */
  }
  dirup = get_up_dir_alloc(dir);

  /* clang was doing:
   * readlink("/proc/self/exe", ".../clang/binlib/ld-linux.so.2", ...)
   * ... and believed it's ld-linux.so.2. I edited the binary to /proc/self/exE
   * to fix it.
   */
  argv0 = argv[0];
  argp = args = malloc(sizeof(*args) * (argc + 20));
  /* Now argv0_base looks like "clang" or "clang-3.3". We make version_suffix
   * point to the suffix: "" or "-3.3".
   */
  for (version_suffix = argv0_base + strlen(argv0_base);
       version_suffix != argv0_base && *version_suffix != '-';
       --version_suffix) {}
  if (*version_suffix != '-') version_suffix = "";
  *argp++ = strdupcat(dir, strdupcat("/clang", version_suffix, ".bin"), "");
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
  if (!argv[1] ||
      (!argv[2] && 0 == strcmp(argv[1], "-v")) ||
      (!argv[2] && is_m32_or_m64(argv[1])) ||
      (is_m32_or_m64(argv[1]) && argv[2] && 0 == strcmp(argv[2], "-v") &&
       !argv[3])) {
    char is_v = !argv[1] || 0 == strcmp(argv[1], "-v") ||
        (argv[2] && 0 == strcmp(argv[2], "-v"));
    archbit_t archbit, archbit_override;
    archbit = get_archbit_detected(argv);
    archbit_override = get_archbit_override(archbit);
    if (archbit_override == ARCHBIT_32) {
      *argp++ = "-m32";
    } else if (archbit_override == ARCHBIT_64) {
      *argp++ = "-m64";
    }
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
        fdprint(is_v ? 2 : 1,
            "Additinal flags supported: -xstatic -xsysld -xermine\n");
        exit(0);
      }
    }
    memcpy(argp, argv + 1, argc * sizeof(*argp));
  } else if (argv[1] && 0 == strcmp(argv[1], "--help")) {
    fdprint(1, "Additinal flags supported: -xstatic -xsysld -xermine\n");
    memcpy(argp, argv + 1, argc * sizeof(*argp));
  } else if (ldmode == LM_XSTATIC) {
    struct stat st;
    char need_linker;
    lang_t lang;
    check_bflags(argv, 0);
    check_xflags(argv);
    need_linker = detect_need_linker(argv);
    detect_nostdinc(argv, &has_nostdinc, &has_nostdincxx);
    detect_lang(argv0, argv, &lang);
    if (lang.is_cxx_prog) *argp++ = get_cxx_flag(version_suffix);
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
      if (lang.is_cxx) *argp++ = "-nostdinc++";
      if (lang.is_cxx && !has_nostdincxx) {
        /* It's important not to use the Clang flag -cxx-isystem here, because
         * that adds C++ includes after C includes, but we need the other way
         * round (because of conflicting files, e.g. complex.h, tgmath.h,
         * fenv.h).
         */
        *argp++ = "-isystem";
        *argp++ = strdupcat(dirup, "/xstaticusr/c++include", "");
        /* Regular g++ libstdc++ has non-backward and then backward. */
        *argp++ = "-isystem";
        *argp++ = strdupcat(dirup, "/xstaticusr/c++include/backward", "");
      }
      if (!has_nostdinc) {
        p = strdupcat(dirup, "/xstaticusr/include", "");
        if (0 != stat(p, &st) || !S_ISDIR(st.st_mode)) goto dir_missing;
        *argp++ = "-isystem";
        *argp++ = p;

        /* It's important to keep clanginclude the last, for consistency.
         */
        p = strdupcat(dirup, "/clanginclude", version_suffix);
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
    p = strdupcat(dirup, "/clangldxs/ld.bin", "");
    if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) {
     file_missing:
      fdprint(2, strdupcat(
          "error: file missing for -xstatic, please install: ", p, "\n"));
      return 123;
    }
    p = strdupcat(dirup, "/xstaticusr/lib/libc.a", "");
    if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) goto file_missing;
    p = strdupcat(dirup, "/xstaticusr/lib/crt1.o", "");
    if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) goto file_missing;
    p = strdupcat(dirup, "/clangldxs/crtbeginT.o", "");
    if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) goto file_missing;
    /* The linker would be ../clangldxs/ld, which is also a trampoline binary
     * of ours.
     */
    *argp++ = strdupcat("-B", dirup, "/clangldxs");
    /* Run `clang -print-search-dirs' to confirm that it was properly
     * detected.
     */
    if (need_linker && is_verbose) {
      *argp++ = "-Wl,--do-clangldv";
    }
    for (++argv; (arg = *argv); ++argv) {
      if (0 == strcmp(arg, "-nostdinc") ||
          0 == strcmp(arg, "-nostdinc++") ||
          0 == strcmp(arg, "-static") ||
          0 == strcmp(arg, "-xstatic") ||
          0 == strcmp(arg, "--xstatic") ||
          0 == strcmp(arg, "-m32")) {
      } else if (arg[0] == '-' && arg[1] == 'L') {  /* "-L". */
        /* Convert -L to -=L in the linker command-line on which our ld wrapper
         * code can trigger.
         */
        if (arg[2] == '\0' && argi[1]) {
          *argp++ = strdupcat("-Wl,-=L", "", *++argi);
        } else {
          *argp++ = strdupcat("-Wl,-=L", "", arg + 2);
        }
      } else {
        *argp++ = arg;
      }
    }
    *argp++ = NULL;
  } else if (argv[1] && 0 == strcmp(argv[1], "-cc1")) {
    /* The Ermine binary wouldn't work anyway, because it opens its argv[0]. */
    fdprint(2, "error: clang: unexpected backend (-cc1) invocation\n");
    return 125;
  } else {
    char need_linker;
    archbit_t archbit, archbit_override;
    lang_t lang;
    if (check_bflags(argv, ldmode == LM_XCLANGLD)) ldmode = LM_XSYSLD;
    need_linker = detect_need_linker(argv);
    detect_lang(argv0, argv, &lang);
    detect_nostdinc(argv, &has_nostdinc, &has_nostdincxx);
    archbit = get_archbit_detected(argv);
    archbit_override = get_archbit_override(archbit);
    if (lang.is_cxx_prog) *argp++ = get_cxx_flag(version_suffix);
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
        *argp++ = strdupcat(dirup, "/clanginclude", version_suffix);
      }
      if (archbit == ARCHBIT_64 && !has_nostdinc) {
        /* Let the compiler find our replacement for gnu/stubs-64.h on 32-bit
         * systems which don't provide it.
         */
        *argp++ = "-idirafter";  /* Add with low priority. */
        *argp++ = strdupcat(dirup, "/clanginc64low", "");
      }
    }
    for (++argv; (arg = *argv); ++argv) {
      if (0 == strcmp(arg, "-xsysld") || 0 == strcmp(arg, "--xsysld") ||
          0 == strcmp(arg, "-xstatic") || 0 == strcmp(arg, "--xstatic")) {
        fdprint(2, strdupcat("error: flag specified too late: ", arg, "\n"));
        exit(122);
      } else if (0 == strcmp(arg, "-xermine") ||
                 0 == strncmp(arg, "-xermine,", 9)) {
        if (ldmode != LM_XCLANGLD) {
          /* With -xsysld our it's not our linker which is running, so we can't
           * pass special flag -xermine... to it.
           *
           * `gcc -print-prog-name=ld' would be needed for -xsysld, but it is
           * not reliable since `i686-gcc -print-prog-name=ld' prints ld.
           */
          fdprint(2, strdupcat(
              "error: flag incompatible with -xsysld: ", arg, "\n"));
          exit(122);
        }
        if (need_linker) {
          *argp = strdupcat("-Wl,-=", "", arg + 1);  /* -=xermine,foo */
          /* When passing -Wl,-=foo,bar, two args `-=foo bar' will be passed
           * to the linker, so wi pass an = instead of a , here.
           */
         if (argp[0][13] == ',') argp[0][13] = '=';
          ++argp;
        }
      } else {
        *argp++ = arg;
      }
    }
    *argp++ = NULL;
  }
  if (is_verbose) {
    fdprint(2, escape_argv("info: running clang frontend:\n", args, "\n"));
  }
  execv(args[0], args);
  fdprint(2, strdupcat("error: clang: exec failed: ", args[0], "\n"));
  return 120;
}
