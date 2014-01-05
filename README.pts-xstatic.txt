README for the pts-xstatic Linux i386 uClibc static compilation tool
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
pts-xstatic is a convenient wrapper tool for compiling and creating
portable, statically linked Linux i386 binaries. It works on Linux i386 and
Linux x86_64 host systems. It wraps an existing
compiler (GCC or Clang) of your choice, and it links against uClibc and the
other base libraries included in the pts-xstatic binary release.

C compilers supported: gcc-4.1 ... gcc-4.8, clang-3.0 ... clang-3.3. C++
compilers supported: g++ and clang++ corresponding to the supported C
compilers. Compatible uClibc C and C++ headers (.h) and precompiled static
libraries (e.g. libc.a, libz.a, libstdc++.a) are also provided by
pts-xstatic. To minimize system dependencies, pts-xstatic can compile
with pts-clang (for both C and C++), which is portable, and you can install
it as non-root.

As an alternative of pts-xstatic, if you want a tiny, self-contained
(single-file) for Linux i386, please take a look at pts-tcc at
http://ptspts.blogspot.hu/2009/11/tiny-self-contained-c-compiler-using.html
. With pts-xstatic, you can create faster and smaller statically linked
executables, with the compiler of your choice.

Motivation
~~~~~~~~~~
0. Available uClibc GCC toolchain binary releases are very old:
   http://www.uclibc.org/downloads/binaries/0.9.30.1/cross-compiler-i686.tar.bz2
   contains gcc-4.1.2 compiled on 2009-04-11.

1. With uClibc Buildroot (http://buildroot.uclibc.org/), the uClibc version
   is tied to a specific GCC version. It's not possible to compile with your
   favorite preinstalled C or C++ compiler version, and link against your
   favorite uClibc version. pts-xstatic makes this possible.

2. libstdc++ is not easily available for uClibc, and it's a bit cumbersome
   to compile. pts-xstatic contains a precompiled version.

Minimum installation
~~~~~~~~~~~~~~~~~~~~
If you want to install try pts-xstatic quickly, without root access, without
installing any dependencies, and without changing any settings, this is the
easiest way:

$ cd /tmp
$ rm -f pts-xstatic-latest.sfx.7z
$ wget http://pts.50.hu/files/pts-xstatic/pts-xstatic-latest.sfx.7z
$ chmod +x pts-xstatic-latest.sfx.7z
$ ./pts-xstatic-latest.sfx.7z -y  # Creates the pts-xstatic directory.
$ rm -f pts-clang-latest.sfx.7z
$ wget http://pts.50.hu/files/pts-clang/pts-clang-latest.sfx.7z
$ chmod +x pts-clang-latest.sfx.7z
$ ./pts-clang-latest.sfx.7z -y  # Creates the pts-clang directory.
$ cat >>hw.c <<'END'
#include <stdio.h>
int main(void) {
  return !printf("Hello, %s!\n", "World");
}
END
$ pts-xstatic/bin/xstatic pts-clang/bin/clang -s -O2 -W -Wall hw.c && ./a.out
Hello, World!
$ strace -e open ./a.out
Hello, World!
$ file a.out
a.out: ELF 32-bit LSB executable, Intel 80386, version 1 (SYSV), statically linked, stripped
$ ls -l a.out
-rwxr-xr-x 1 pts pts 16888 Jan  2 23:17 a.out

Compare the file size with statically linking against regular (e)glibc:

$ gcc -static -m32 -o a.big -s -O2 -W -Wall hw.c && ./a.big
Hello, World!
$ strace -e open ./a.big
Hello, World!
$ file a.big
a.big: ELF 32-bit LSB executable, Intel 80386, version 1 (GNU/Linux), statically linked, for GNU/Linux 2.6.24, BuildID[sha1]=0x37284f286ffeecdb7ac5d77bfa83ade4310df098, stripped
$ ls -l a.big
-rwxr-xr-x 1 pts eng 684748 Jan  2 23:20 a.big

Dependencies
~~~~~~~~~~~~
The dependencies of pts-xstatic are the following:

* A Linux i386 or Linux amd64 host system.

* A mounted /proc filesystem.

* root access is not needed for execution, and it's not strictly needed for
  installation, but installing the C or C++ compiler is most convenient
  using system package manager (which needs root access). Use pts-clang
  (see below) if you don't have root access.

* To use pts-xstatic, you need a working C and/or C++ compiler. pts-xstatic
  supports gcc-4.1 ... gcc-4.8, clang-3.0 ... clang-3.3 and the
  corresponding C++ compilers (g++ and clang++). Newer versions of gcc and
  clang are probably also work out-of-the-box, but they were not tried.

* Here is how to install GCC as a C and C++ compiler on Ubuntu:

    $ sudo apt-get install gcc g++

  If you want to compile to both Linux i386 and Linux amd64 as a target,
  install these packages as well on Ubuntu Precise:

    $ sudo apt-get install libc6-dev:amd64 libc6-dev:i386
    $ sudo apt-get install g++-4.6-multilib libstdc++6-4.6-dev

* As an alternative of GCC, it's also possible to install Clang. (If unsure,
  use GCC, because it's easier to install from package.)

* As an alternative of the system GCC and Clang, use pts-clang
  (http://raw.github.com/pts/pts-clang-xstatic/master/README.pts-clang.txt)
  if you don't have root access and you want a quick and easy installation.

Installation
~~~~~~~~~~~~
If you want to try pts-xstatic quickly before installing it properly, please
follow the instructions in the section ``Minimum installation''.

Install the dependencies first (see the details in the ``Dependencies''
section below). The easiest way to do it on Ubuntu Precise:

  $ sudo apt-get install gcc g++

To use pts-xstatic on a Linux i386 or Linux amd64 system, download it and
extract it to any directory. Here is how to do it:

  $ rm -f pts-xstatic-latest.sfx.7z
  $ wget http://pts.50.hu/files/pts-xstatic/pts-xstatic-latest.sfx.7z
  $ chmod +x pts-xstatic-latest.sfx.7z
  $ ./pts-xstatic-latest.sfx.7z -y  # Creates the pts-xstatic directory.

If you don't want to run the self-extracting archive, you can extract it
using 7z instead (`sudo apt-get install p7zip-full' first):

  $ 7z x -y pts-xstatic-latest.sfx.7z

Now you can already run pts-xstatic/bin/xstatic . If you don't like typing
that much, you can either create a symlink to pts-xstatic/bin/xstatic in a
directory in your $PATH, or add pts-xstatic/pathbin to your $PATH. This is
strongly recommended. As a result, you can just run `xstatic'. An example
temporary solution (which affects the current terminal window only):

  $ export PATH="$PWD/pts-xstatic/pathbin:$PATH"
  $ xstatic -v

Example usage
~~~~~~~~~~~~~
C compilation:

$ cat >>hellow.c <<'END'
#include <stdio.h>
int main(void) {
  return !printf("Hello, %s!\n", "World");
}
END
$ xstatic gcc -s -O2 -W -Wall hellow.c && ./a.out
Hello, World!
$ file a.out
a.out: ELF 32-bit LSB executable, Intel 80386, version 1 (SYSV), statically linked, stripped
$ ls -l a.out
-rwxr-xr-x 1 pts pts 16996 Jan  2 21:42 a.out

C++ compilation:

$ cat >>hellow.cc <<'END'
#include <iostream>
int main(void) {
  std::cout << "Hello, " << "World++!" << std::endl;
}
END
$ xstatic g++ -s -O2 -W -Wall hellow.cc && ./a.out
Hello, World++!
$ file a.out
a.out: ELF 32-bit LSB executable, Intel 80386, version 1 (SYSV), statically linked, stripped
$ ls -l a.out
-rwxr-xr-x 1 pts pts 549384 Jan  2 21:43 a.out

How to use to build 3rd-party software
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
After installation you can use `xstatic gcc' as a drop-in replacement for
`gcc' for creating statically linked binaries, and similarly for `g++',
`clang' and `clang++'.

Example indirect usages:

  $ ./configure CC='xstatic gcc' CXX='xstatic g++' --build=i686-pc-linux-gnux
  $ make CC='xstatic gcc' CXX='xstatic g++'

Sometimes a `make' without arguments also works, because configure has
already initialized CC= and CXX= properly in the Makefile.

Please note that config.guess usually returns the wrong architecture
(64-bit instead of 32-bit) on 64-bit systems, you may want to override that
manually with the --build=... flag above.

How to generate smaller executables
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Follow the instructions in this blog post:

  http://ptspts.blogspot.hu/2013/12/how-to-make-smaller-c-and-c-binaries.html

In the C++ example above you can get the biggest saving is by using the C
stdio library instead of the C++ iostream.

Compatibility and feature notes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
* There is no 64-bit (amd64, x86_64 -m64) compilation or linking support in
  pts-xstatic.

* There is no support for Clang's libc++.

* Non-xstatic system Clang has libc C++ headers (e.g.
  /usr/include/c++/4.6), then libc C headers (e.g.
  /usr/inclde), then Clang compiler C headers (e.g.
  /usr/include/clang/3.0/include/), then
  GCC compiler C headers (e.g. usr/lib/gcc/i686-linux-gnu/4.6/include/)
  on the include path.

  pts-clang Clang has libc C++ headers, then libc C headers, then
  Clang compiler C headers.

  Non-xstatic GCC has libc C++ headers, then GCC compiler C headers, then
  libc C headers.

  For simplicity, pts-xstatic GCC and Clang have libc C++ headers, then libc
  C headers, then compiler (Clang or GCC) C headers. Special #include_next
  directives are provided in C mode to load the C header only if a C header
  and a C++ header exists with the same name.

* The shipped libstdc++ corresponds to g++-4.4, it works out-of-the-box with
  g++-4.4 and newer, and clang++-3.0 and newer. It has been backported
  manually, so it also works with anything between g++-4.1 and g++4.8 and
  between clang++-3.0 clang++-3.3. It probably works with newer g++ and
  clang++ out-of-the-box. Please note that some C++11 (formerly known as
  C++0x) features that were not supported in g++-4.4 may not be supported in
  pts-xstatic, because many C++11 features needs libary header support, and
  the libstdc++ in g++-4.4 is not the newest.
  See the features in http://gcc.gnu.org/projects/cxx0x.html

* Profiling (prof with -p, and gprof with -pg) is not supported, because the
  corresponding Mcrt1.o and gcrt1.o files were missing from the uClibc
  cross-compiler toolchain.

* Some other optional, statically linked binutils tools (ar, ranlib and
  strip) are also provided for convenience in the pts-static-binu binary
  release, see more info here:
  http://raw.github.com/pts/pts-clang-xstatic/master/README.pts-static-binu.txt
  . These tools can be used for auxiliary tasks such as building static
  libraries.

* C++ compiler built-ins __is_pod, __is_empty and __has_trivial_destructor
  were introduced in g++-4.3. For older compilers pts-xstatic emulated them
  using macros invoking template metaprogramming, in
  xstaticusr/c++include/bits/builtin_type_traits.h . If you write C++ code
  intended to be portable across g++ and clang++, please don't
  `#include <bits/builtin_type_traits.h>' directly, because this header is
  only available in xstatic. Please use `#include <bits/stl_construct.h>'
  instead, which is always available, and as a side effect it makes
  __is_pod, __is_empty and __has_trivial_destructor available.

Does pts-xstatic create portable executables?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
pts-xstatic creates portable, statically linked, Linux ELF i386 executables,
linked against uClibc. By default, these executables don't need any external
file (not even the file specified by argv[0], not even the /proc filesystem)
to run. NSS libraries (the code needed for e.g. getpwent(3) (getting info of
Unix system users) and gethostbyname(3) (DNS resolution)) are also included.
The executables also work on FreeBSD in Linux mode if the operating system
field in the ELF header frm SYSV to Linux.

As an alternative to pts-xstatic: `gcc -static' (or `clang -static') doesn't
provide real portability, because for calls such as getpwent(3) (getting
info of Unix system users) and gethostbyname(3) (DNS resolution), glibc
loads files such as libnss_compat.so, libnss_dns.so. On the target system
those libraries may be incompatible with your binary, so you may get a
segfault or unintended behavior. pts-xstatic solves this, because it uses
uClibc.

It can be useful to embed locale files, gconv libraries, arbitrary data and
configuration files needed by the program, Neither `gcc -static',
pts-xstatic or statifier (http://statifier.sf.net/) can do it, but Ermine
can. Ermine is not free software, but you can get a free-of-charge
time-limited trial, and you can ask for a discount for noncommercial use.
See all details at http://www.magicermine.com/products.html , and give it a
try!

Author, copyright and recompilation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
pts-xstatic here was created by Péter Szabó <pts@fazekas.hu>, a custom
trampoline code was written in C to start the real compiler with the correct
flags, see all the details in
http://github.com/pts/pts-clang-xstatic/blob/master/info.txt

Precompiled uClibc libraries (e.g. libc.a, crt1.o) and the corresponding
headers in xstatic are from 
http://www.uclibc.org/downloads/binaries/0.9.30.1/cross-compiler-i686.tar.bz2
, which was compiled on 2009-04-11, and contains uClibc-0.9.30.1.

The precompiled libstdc++.a and the corresponding C++ headers were created
by compiling parts of gcc-4.4.3 (with the 4ubuntu5.1 patches in Ubuntu
Lucide) using `xstatic g++', and applying a custom compatibility patch
http://raw.github.com/pts/pts-clang-xstatic/master/pts-xstatic-libstdc++-header-gcc-4.4.3.patch
to make it work with other versions of g++ and also with many version of
clang++.

The other precompiled libraries (see them in README.lib*.txt) which are
included were compiled with `xstatic gcc' with some minor modifications in
the configure scripts and Makefiles, and the resulting .a and .h files were
copied manually to the xstaticusr directory.

All software mentioned in this document is free software and open source.

__EOF__
