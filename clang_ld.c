#define ALONE \
    set -ex; ${CC:-gcc} -s -Os -fno-stack-protector \
    -W -Wall -o clang_trampoline "$0"; exit 0
/*
 * clang_ld.c: ld trampoline for pts-clang
 * by pts@fazekas.hu at Thu Jan 16 00:32:36 CET 2014
 *
 * This program examines its argv, modifies some args, and exec()s "ld.bin".
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

/* Check that the linker is not instructed to link *crt*.o files from the
 * wrong directory. Most of the time this is a configuration issue, e.g. the
 * wrong value for the -B compiler flag was specified, and now the compiler
 * has instructed the linker to look for the *crt*.o files a system-default
 * (e.g. /usr/lib/...) directory.
 */
static void check_ld_crtoarg(char **argv, char is_xstatic) {
  /* If a relevant *crt*.o file is missing in xstaticcld or clangldxs, then
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
          ((!is_xstatic || 0 != strcmp(basename, "crt0.o")) &&
           (!is_xstatic || 0 != strcmp(basename, "crt1.o")) &&
           (!is_xstatic || 0 != strcmp(basename, "crt2.o")) &&
           (!is_xstatic || 0 != strcmp(basename, "crti.o")) &&
           (!is_xstatic || 0 != strcmp(basename, "crtn.o")) &&
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
        "error: *crt*.o file linked from unsupported location "
        "(supported is ", strdupcat(supported_dir, "): ", arg), "\n"));
    had_error = 1;
  }
  if (had_error) exit(122);
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

typedef enum ldmode_t {
  LM_XCLANGLD = 0,  /* Use the ld and -lgcc shipped with clang. */
  LM_XSYSLD = 1,
  LM_XSTATIC = 2,
} ldmode_t;

int main(int argc, char **argv) {
  char *argv0;
  char *argv0_base;  /* The basename of argv[0], after following symlinks. */
  char *dir, *dirup;
  char *p;
  char **args, **argp;
  char is_verbose = 0;
  ldmode_t ldmode;
  char **argi, *arg;

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
  {
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
    char **ermine_args = NULL;
    char *output_file = 0;
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
    check_ld_crtoarg(argv, ldmode == LM_XSTATIC);
    if (ldmode == LM_XCLANGLD) {
      char is_ermine = 0;
      int ermine_flag_count = 0;
      char **eargp = NULL;
      output_file = "a.out";
      for (argi = argv + 1; (arg = *argi); ++argi) {
        if (0 == strcmp(arg, "-=xermine")) {
          is_ermine = 1;
        } else if (0 == strncmp(arg, "-=xermine=", 10)) {
          is_ermine = 1;
          ++ermine_flag_count;
        } else if (arg[0] == '-' && arg[1] == 'o') {
          output_file = (arg[2] == '\0' && argi[1] != NULL) ? *++argi : arg + 2;
        }
      }
      if (is_ermine) {
        char *eprog;
        eargp = ermine_args = malloc(
            sizeof(*ermine_args) * (ermine_flag_count + 4));
        *eargp++ = strdupcat(dir, "/ermine", "");
        eprog = find_on_path(eargp[-1]);
        if (!eprog) {
          fdprint(2, strdupcat(
              "error: could not find Ermine: ", eargp[-1], "\n"));
          return 126;
        }
        free(eprog);
        *eargp++ = "-o";
        *eargp++ = output_file;
        output_file = strdupcat(output_file, ".eE0", "");
      }
      for (argi = argv + 1; (arg = *argi); ++argi) {
        if (0 == strcmp(arg, "--do-xclangld") ||
            0 == strcmp(arg, "--do-clangldv") ||
            0 == strcmp(arg, "-=xermine")) {
        } else if (0 == strncmp(arg, "-=xermine=", 10)) {
          *eargp++ = arg + 10;
        } else if (is_ermine && arg[0] == '-' && arg[1] == 'o') {
          /* Skip the original output_file argument. */
          if (arg[2] == '\0' && argi[1] != NULL) ++argi;
        } else {
          *argp++ = *argi;
        }
      }
      if (is_ermine) {
        *argp++ = "-o";
        *argp++ = output_file;
        *eargp++ = output_file;
        *eargp = NULL;
      }
    } else {  /* ldmode == LM_XSTATIC. */
      char **argli;  /* Where to add the -L flags. */

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
          *argp++ = strdupcat("-L", dirup, "/xstaticusr/lib");
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
            0 == strcmp(arg, "--do-clangcld") ||
            0 == strcmp(arg, "--do-clangldv") ||
            0 == strncmp(arg, "--hash-style=", 13) ||  /* Would increase size.*/
            0 == strcmp(arg, "--build-id") ||
            0 == strncmp(arg, "--sysroot=", 10)) {
          /* Omit this argument. */
        } else {
          *argp++ = *argi;
        }
      }
    }
    *argp = NULL;
    args[0] = strdupcat(argv0, ".bin", "");  /* "ld.bin". */
    if (is_verbose) {
      fdprint(2, escape_argv("info: running ld:\n", args, "\n"));
    }
    if (ermine_args) {
      pid_t pid = fork();
      if (pid < 0) exit(124);
      if (pid != 0) {  /* Parent. */
        int status;
        pid_t pid2 = waitpid(pid, &status, 0);
        if (pid != pid2) exit(124);
        if (status != 0) {
          unlink(output_file);  /* The .eE0 file. */
          exit(WIFEXITED(status) ? WEXITSTATUS(status) : 124);
        }
        /* Done with ld, run ermine. */
        if (is_verbose) {
          fdprint(2, escape_argv("info: running ermine:\n", ermine_args, "\n"));
        }
        pid = fork();
        if (pid < 0) exit(124);
        if (pid == 0) {  /* Child. */
          execv(ermine_args[0], ermine_args);
          fdprint(2, strdupcat(
              "error: clang-ermine: exec failed: ", ermine_args[0], "\n"));
          exit(120);
        }
        pid2 = waitpid(pid, &status, 0);
        unlink(output_file);  /* The .eE0 file. */
        if (pid != pid2) exit(124);
        exit(WIFEXITED(status) ? WEXITSTATUS(status) : 124);
      }
    }
    execv(args[0], args);
    fdprint(2, strdupcat("error: clang-ld: exec failed: ", args[0], "\n"));
    return 120;
  }
}
