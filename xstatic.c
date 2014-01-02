#define ALONE \
    set -ex; ${CC:-gcc} -s -Os -fno-stack-protector \
    -W -Wall -o xstatic "$0"; exit 0
/*
 * xstatic.c: trampoline for uClibc static linking with gcc, g++ or clang(++)
 * by pts@fazekas.hu at Sun Dec 15 20:08:54 CET 2013
 *
 * This program examines its argv, modifies some args, and exec()s the specified
 * gcc, g++, clang or clang++ for static linking with uClibc. The directory
 * layout where the uClibc .h, .a and .o files are locaed is hardwired.
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

/* Returns a newly malloced string containing the directory 1 level above
 * the specified directory.
 */
typedef enum archbit_t {
  ARCHBIT_UNKNOWN = -1,
  ARCHBIT_UNSPECIFIED = -2,
  ARCHBIT_DETECTED = -3,
  ARCHBIT_32 = 32,
  ARCHBIT_64 = 64,
} archbit_t;

#if 0
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
#endif

#if 0
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
#endif

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

/* Return true iff we've received a linker command-line. */
static char detect_linker(char **argv) {
  char **argi, *arg;
  for (argi = argv + 1; (arg = *argi) &&
       /* We could also use `-z relro' or `-m elf_i386' or ther values. */
       0 != strcmp(arg, "--eh-frame-hdr") &&  /* By clang. */
       0 != strcmp(arg, "--build-id") &&  /* By clang and gcc. */
       0 != strncmp(arg, "--hash-style=", 13) &&
       0 != strcmp(arg, "--do-xstaticcld") &&
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

static void check_bflags(char **argv) {
  char **argi, *arg;
  for (argi = argv + 1; (arg = *argi); ++argi) {
    if ((arg[0] == '-' && arg[1] == 'B') ||
        is_argprefix(arg, "--sysroot") ||
        is_argprefix(arg, "--gcc-toolchain")) {
      fdprint(2, strdupcat(
          "error: flag not supported in this ldmode: ", arg, "\n"));
      exit(122);
    }
  }
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
        0 == strcmp(arg, "-sysld") ||
        0 == strcmp(arg, "--sysld") ||
        0 == strcmp(arg, "-p") ||  /* Needs Mcrt1.o (| gcrt1.o), clang crt1.o */
        0 == strcmp(arg, "-pg") ||  /* Needs gcrt1.o, clang uses crt1.o */
        0 == strcmp(arg, "-pie") ||
        0 == strcmp(arg, "-fpic") ||
        0 == strcmp(arg, "-fPIC") ||
        0 == strcmp(arg, "-fpie") ||
        0 == strcmp(arg, "-fPIE") ||  /* This would link Scrt1.o etc. */
        0 == strcmp(arg, "-shared") ||
        0 == strcmp(arg, "-shared-libgcc")) {
      fdprint(2, strdupcat(
          "xstatic: error: flag not supported: ", arg, "\n"));
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

/* Check that the linker is not instructed to link *crt*.o files from the
 * wrong directory. Most of the time this is a configuration issue, e.g. the
 * wrong value for the -B compiler flag was specified, and now the compiler
 * has instructed the linker to look for the *crt*.o files a system-default
 * (e.g. /usr/lib/...) directory.
 */
static void check_ld_crtoarg(char **argv) {
  /* If a relevant *crt*.o file is missing from xstaticcld, then
   * gcc generates e.g.
   * /usr/lib/gcc/x86_64-linux-gnu/4.6/../../../i386-linux-gnu/crtn.o , and
   * pts-static-clang generates crtn.o . We detect and fail on both.
   */
  char had_error = 0;
  char **argi, *supported_dir;
  const char *arg, *basename, *argend, *supbasename;
  size_t supdirlen;
  for (supbasename = argv[0] + strlen(argv[0]);
       supbasename != argv[0] && supbasename[-1] != '/'; --supbasename) {}
  supdirlen = supbasename - argv[0];
  for (argi = argv + 1; (arg = *argi); ++argi) {
    if (0 == strcmp(arg, "-o") && argi[1]) {
      ++argi;
      continue;
    }
    if (arg[0] == '-') continue;
    argend = arg + strlen(arg);
    for (basename = argend; basename != arg && basename[-1] != '/';
         --basename) {}
    if (basename[0] == '\0' || argend - arg < 2 ||
        argend[-1] != 'o' || argend[-2] != '.' ||
        ((0 != strncmp(basename, "crt", 3) ||
          (0 != strcmp(basename, "crt0.o") &&
           0 != strcmp(basename, "crt1.o") &&
           0 != strcmp(basename, "crt2.o") &&
           0 != strcmp(basename, "crti.o") &&
           0 != strcmp(basename, "crtn.o") &&
           0 != strcmp(basename, "crtbegin.o") &&
           0 != strcmp(basename, "crtbeginS.o") &&
           0 != strcmp(basename, "crtbeginT.o") &&
           0 != strcmp(basename, "crtend.o") &&
           0 != strcmp(basename, "crtendS.o") &&
           0 != strcmp(basename, "crtfastmath.o"))) &&
         (0 != strcmp(basename + 1, "crt1.o") ||
          (0 != strcmp(basename, "Scrt1.o") &&
           0 != strcmp(basename, "Mcrt1.o") &&
           0 != strcmp(basename, "gcrt1.o"))))) continue;
    if (0 == strncmp(arg, argv[0], supdirlen)) continue;
    if (supdirlen == 0) {
      supported_dir = ".";
    } else {
      supported_dir = malloc(supdirlen);
      memcpy(supported_dir, argv[0], supdirlen - 1);
      supported_dir[supdirlen - 1] = '\0';
    }
    fdprint(2, strdupcat(
        "xstatic-ld: error: *crt*.o file linked from unsupported location "
        "(supported is ", strdupcat(supported_dir, "): ", arg), "\n"));
    had_error = 1;
  }
  if (had_error) exit(122);
}

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
    fdprint(2, strdupcat("xstatic: error: no parent dir for: ", dir, "\n"));
    exit(122);
  } else {
    p[-1] = '\0';  /* Remove basename of dirup. */
  }
  return dirup;
}

typedef enum ldmode_t {
  LM_XCLANGLD = 0,  /* Use the ld and -lgcc shipped with clang. */
  LM_XSYSLD = 1,
  LM_XSTATIC = 2,
} ldmode_t;

int main(int argc, char **argv) {
  char *prog;
  char *dir, *dirup;
  char *p;
  char **args, **argp;
  char is_verbose = 0;
  char **argi, *arg;
  struct stat st;

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
    for (; p != dir && p[-1] == '/'; --p) {}
    p[p == dir] = '\0';  /* Remove basename of dir. */
  }
  dirup = get_up_dir_alloc(dir);
  if (detect_linker(argv)) {  /* clang trampoline runs as as ld. */
    char is_static = 0;
    char **argli;  /* Where to add the -L flags. */

    /* All we do is exec()ing ld.bin with the following dropped from argv:
     *
     * -L/usr/lib...
     * -L/lib...
     * --hash-style=both (because not supported by old ld)
     * --build-id (because not supported by old ld)
     * -z relro (because it increases the binary size and it's useless for static)
     */
    check_ld_crtoarg(argv);
    argp = args = malloc(sizeof(*args) * (argc + 5));
    *argp++ = *argv;
    for (argi = argv + 1; (arg = *argi); ++argi) {
      if (0 == strcmp(arg, "--do-xstaticldv")) {
        is_verbose = 1;
      } else if (0 == strcmp(arg, "-static")) {
        is_static = 1;
      }
    }
    if (!is_static) {
      fdprint(2, "xstatic: error: missing -static\n");
      exit(1);
    }

    *argp++ = "-nostdlib";  /* No system directories to find .a files. */
    /* Find argli: in front of the last -L (which will be removed), but no
     * later than just before the first -l.
     */
    argli = NULL;
    for (argi = argv + 1; (arg = *argi); ++argi) {
      if (arg[0] == '-') {
        if (arg[1] == 'L') {  /* "-L". */
          argli = argi;
        } else if (arg[1] == 'l') {  /* "-l". */
          if (!argli) argli = argi;
          break;
        }
      }
    }
    if (!argli) argli = argv + 1;

    for (argi = argv + 1; (arg = *argi); ++argi) {
      if (argi == argli) {
        char **argj;
        /* Add the user-specified link dirs first (just like non-xstatic). */
        for (argj = argv + 1; *argj; ++argj) {
          if (0 == strncmp(*argj, "-=L", 3)) {
            *argp++ = strdupcat("-L", "", *argj + 3);
          }
        }
        /* We put xstaticcld with libgcc.a first, because clang puts
         * /usr/lib/gcc/i486-linux-gnu/4.4 with libgcc.a before /usr/lib with
         * libc.a .
         *
         * We add these -L flags even if compiler -nostdlib was specified,
         * because non-xstatic compilers do the same.
         */
        *argp++ = strdupcat("-L", dirup, "/xstaticcld");
        *argp++ = strdupcat("-L", dirup, "/uclibcusr/lib");
        argli = NULL;  /* Don't insert the -L flags again. */
      }
      if (0 == strcmp(arg, "-z") &&
          argi[1] && 0 == strcmp(argi[1], "relro")) {
        /* Would increase size. */
        ++argi;  /* Omit both arguments: -z relro */
      } else if (arg[0] == '-' && arg[1] == 'L') {  /* "-L". */
        if (arg[2] == '\0' && argi[1]) ++argi;
        /* Omit -L... containing the system link dirs, the user-specified link
         * dir flags were passed as -=L...
         */
      } else if (0 == strncmp(arg, "-=L", 3)) {
        /* Omit -=L here, gets added at position argli. */
      } else if (
          0 == strcmp(arg, "-nostdlib") ||
          0 == strcmp(arg, "--do-xstaticcld") ||
          0 == strcmp(arg, "--do-xstaticldv") ||
          0 == strncmp(arg, "--hash-style=", 13) ||  /* Would increase size. */
          0 == strcmp(arg, "--build-id") ||
          0 == strncmp(arg, "--sysroot=", 10)) {
        /* Omit this argument. */
      } else {
        *argp++ = *argi;
      }
    }
    *argp = NULL;
    prog = strdupcat(argv[0], ".bin", "");  /* "ld.bin". */
    args[0] = prog;
    if (is_verbose) {
      fdprint(2, escape_argv("xstatic-ld: info: running ld:\n", args, "\n"));
    }
    execv(prog, args);
    fdprint(2, strdupcat("xstatic-ld: error: exec failed: ", prog, "\n"));
    return 120;
  }

  if (!argv[1] || 0 == strcmp(argv[1], "--help")) {
    fdprint(1, strdupcat(
        "Usage: ", argv[0], " <gcc|g++|clang|clang++> [<compiler-arg>...]\n"
        "Invokes the C/C++ compiler with -static against the xstatic uClibc.\n"
        ));
    return 1;
  }
  if (argv[1][0] == '-') {
    fdprint(2, "xstatic: error: please specify gcc|clang prog in 1st arg\n");
    return 1;
  }
  check_bflags(argv);
  check_xflags(argv);
  p = strdupcat(dirup, "/xstaticcld/ld.bin", "");
  if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) {
   file_missing:
    fdprint(2, strdupcat(
        "xstatic: error: file missing, please install: ", p, "\n"));
    return 123;
  }
  p = strdupcat(dirup, "/uclibcusr/lib/libc.a", "");
  if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) goto file_missing;
  p = strdupcat(dirup, "/uclibcusr/include/stdio.h", "");
  if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) goto file_missing;
  p = strdupcat(dirup, "/uclibcusr/lib/crt1.o", "");
  if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) goto file_missing;
  p = strdupcat(dirup, "/xstaticcld/crtbeginT.o", "");
  if (0 != stat(p, &st) || !S_ISREG(st.st_mode)) goto file_missing;
  prog = find_on_path(argv[1]);
  if (!prog) {
    fdprint(2, strdupcat(
        "xstatic: error: compiler not found: ", argv[1], "\n"));
    return 119;
  }
  argv[1] = argv[0];
  ++argv;
  --argc;
  argp = args = malloc(sizeof(*args) * (argc + 20));
  *argp++ = prog;  /* Set destination argv[0]. */
  /* No need to check for -m64, check_bflags has already done that. */
  if (!argv[1] ||
      (!argv[2] &&
       (0 == strcmp(argv[1], "-v") || 0 == strcmp(argv[1], "-m32"))) ||
      (argv[2] && !argv[3] &&
       0 == strcmp(argv[1], "-m32") &&
       0 == strcmp(argv[2], "-v"))) {
    /* This changes the `Target: ...' of Clang to i386, but of GCC. */
    if (!argv[1] || (!argv[2] && 0 != strcmp(argv[1], "-m32"))) {
      *argp++ = "-m32";
    }
    /* Don't add any flags, because the user wants some version info, and with
     * `-Wl,... -v' gcc and clang won't display version info.
     */
    memcpy(argp, argv + 1, argc * sizeof(*argp));
  } else {
    char need_linker;
    char has_nostdinc, has_nostdincxx;
    lang_t lang;
    need_linker = detect_need_linker(argv);
    detect_nostdinc(argv, &has_nostdinc, &has_nostdincxx);
    detect_lang(prog, argv, &lang);

    /* When adding more arguments here, increase the args malloc count. */
    /* We don't need get_autodetect_archflag(argv), we always send "-m32". */
    *argp++ = "-m32";
    *argp++ = "-static";
    /* The linker would be ../xstaticcld/ld, which is also a trampoline binary
     * of ours.
     */
    *argp++ = strdupcat("-B", dirup, "/xstaticcld");
    /* This puts uclibcusr/include to the top of the include path, and keeps
     * the gcc or clang headers below that. Specifying --nostdinc (when
     * lang.is_compiling) would remove these compiler-specific headers (e.g.
     * stdarg.h), which we don't want removed, because libc headers depend
     * on them.
     *
     * TODO(pts): Make /usr/local/bin/clang also work.
     */
    *argp++ = strdupcat("--sysroot=", dirup,
        lang.is_clang && is_dirprefix(prog, "/usr/bin") ?
        "/xstaticclangempty" : "/xstaticempty");
    if (lang.is_compiling) {
      /*
       * Without this we get the following error compiling binutils 2.20.1:
       * chew.c:(.text+0x233f): undefined reference to `__stack_chk_fail'
       * We can't implement this in a compatible way, glibc gcc generates %gs:20,
       * uClibc-0.9.33 has symbol __stack_chk_guard.
       */
      *argp++ = "-fno-stack-protector";
      if (lang.is_cxx) {
        /* Glitch: It's not possible to disable the gcc warning ``cc1: warning:
         * command line option "-nostdinc++" is valid for C++/ObjC++ but not
         * for C'', there is no such clang warning. Example invocation (with
         * both .c and .cc files): ``xstatic gcc -c -W -Wall -O2 co.c
         * hello.cc''.
         */
        *argp++ = "-nostdinc++";
        if (!has_nostdincxx) {
          /* Clang has -cxx-isystem, which is a no-op when compiling C code,
           * but it adds the directory after the regular -isystem. But we need
           * the C++ headers in front of the C headers (because of conflicting
           * files, e.g. complex.h, tgmath.h, fenv.h).
           *
           * So we don't use -cxx-isystem, but we use -isystem uniformly for
           * Clang and GCC.
           *
           * A small glitch: if there are both C and C++ source files
           * specified (because C source files mustn't have C++ headers on
           * their include path), and a non-C++ compiler is used (e.g.
           * ``xstatic gcc -c -W -Wall -O2 helloco.c hello.cc''), then
           * `#include <vector>' in the .c file would find the C++ vector
           * header, and fail with a confusing error message when parsing it.
           */
          *argp++ = "-isystem";
          *argp++ = strdupcat(dirup, "/uclibcusr/c++include", "");
          /* Regular g++ libstdc++ has non-backward and then backward. */
          *argp++ = "-isystem";
          *argp++ = strdupcat(dirup, "/uclibcusr/c++include/backward", "");
        }
      }
      if (!has_nostdinc) {
        *argp++ = "-isystem";
        *argp++ = strdupcat(dirup, "/uclibcusr/include", "");
      }
    }

    for (argi = argv + 1; (arg = *argi); ++argi) {
      if (0 == strcmp(arg, "-v")) {
        is_verbose = 1;
        *argp++ = arg;
      } else if (0 == strcmp(arg, "-static") ||
                 0 == strcmp(arg, "-xstatic") ||
                 0 == strcmp(arg, "--xstatic") ||
                 0 == strcmp(arg, "-nostdinc") ||
                 0 == strcmp(arg, "-nostdinc++") ||
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

    if (need_linker) {
      *argp++ = "-Wl,--do-xstaticcld";
      if (is_verbose) *argp++ = "-Wl,--do-xstaticldv";
    }
    *argp++ = NULL;
  }

  if (is_verbose) {
    fdprint(2, escape_argv("xstatic: info: running compiler:\n", args, "\n"));
  }
  execv(prog, args);
  fdprint(2, strdupcat("xstatic: error: exec failed: ", prog, "\n"));
  return 120;
}
