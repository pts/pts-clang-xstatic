README for the pts-clang binary release
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
pts-clang is a portable Linux i386 version of the clang tool of the LLVM
Clang compiler (http://clang.llvm.org/), version 3.3. C and C++ compilation
is supported, other frontends (such as Objective C) were not tested. The
tool runs on Linix i386 and Linux amd64 systems. It's libc-independent, all
code is statically linked to the binary. It also contains statically
linked linker (ld), so installing binutils is not necessary.

Get the newest version of this document from here:

  http://raw.github.com/pts/pts-clang-xstatic/master/README.pts-clang.txt

Send donations to the author of pts-clang:
https://flattr.com/submit/auto?user_id=pts&url=https://github.com/pts/pts-clang-xstatic

About Clang
~~~~~~~~~~~
The clang command-line tool of Clang contains a C compiler, a C++ compiler,
an Objective-C compiler and an Objective-C++ compiler. (pts-clang supports C
and C++ only, Objective-{C,C++} were not tried.) The command-line flags and
the ABI (e.g. C++ name mangling, built-in macros, built-in functions, standard
library symbols), are compatible with GCC.

The commonly cited advantages of Clang over GCC:

* Clang compiles faster (usually of a factor between 2 and 3, depending on
  the -O... optimization flag used), while the speed of the compiled code is
  about the same.
* Clang gives more specific, more detailed, better formatted and
  easier-to-understand error and warning messages. Based on these messages,
  a human can identify and fix the bug fasterthan with GCC.
* Clang moves at a faster pace, it gets new language features (e.g. C11,
  C++11, C++14) implemented faster.
* Clang is more modular, it's easier to write plugins and to add and refine
  experimental language and optimization features.

One important disadvantage of Clang is that the newest version is
complicated and tiresome to install, because Linux distributions don't have
it. pts-clang helps this by providing portable, statically linked binary
which runs on any Linux i386 and Linux amd64 host system.

Installation
~~~~~~~~~~~~
Install the dependencies first (see the details in the ``Dependencies''
section below). The easiest way to do it on Ubuntu systems:

  $ sudo apt-get install libc6-dev:amd64 libc6-dev:i386

To use pts-clang on a Linux i386 or Linux amd64 system, download it and
extract it to any directory. Here is how to do it:

  $ rm -f pts-clang-latest.sfx.7z
  $ wget http://pts.50.hu/files/pts-clang/pts-clang-latest.sfx.7z
  $ chmod +x pts-clang-latest.sfx.7z
  $ ./pts-clang-latest.sfx.7z -y  # Creates the pts-clang directory.

If you don't want to run the self-extracting archive, you can extract it
using 7z instead (`sudo apt-get install p7zip-full' first):

  $ 7z x -y pts-clang-latest.sfx.7z

Now you can already run pts-clang/bin/clang and pts-clang/bin/clang++ . If
you don't like typing that much, you can either create symlinks to
pts-clang/bin/clang and pts-clang/bin/clang++ in a directory in your $PATH,
or add the matching pts-clang/clangpathbin-* to your $PATH (add it to the
beginnning to make it override other clang and clang++ commands). An example
temporary solution (which affects the current terminal window only):

  $ export PATH="$PWD/pts-clang/pathbin:$PATH"
  $ clang -v

(This step is optional with -xstatic.) If /usr/include/i386-linux-gnu is a
directory, but /usr/include/i486-linux-gnu doesn't exist (e.g. on Debian
Jessie), then create a symlink:

  $ sudo ln -snf i386-linux-gnu /usr/include/i486-linux-gnu

Example usage
~~~~~~~~~~~~~
C compilation:

$ cat >>hellow.c <<'END'
#include <stdio.h>
int main(void) {
  return !printf("Hello, %s!\n", "World");
}
END
$ pts-clang/bin/clang -s -O2 -W -Wall hellow.c && ./a.out
Hello, World!

C++ compilation:

$ cat >>hellow.cc <<'END'
#include <iostream>
int main(void) {
  std::cout << "Hello, " << "World++!" << std::endl;
}
END
$ pts-clang/bin/clang++ -s -O2 -W -Wall hellow.cc && ./a.out
Hello, World++!

C compilation with -xermine (if you have an Ermine license and have Ermine
installed):

$ pts-clang/bin/clang -xermine -s -xermine,-v -O2 -W -Wall hellow.c && ./a.out
Hello, World!

C++11 and C++0x compatibility
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Please note that even though Clang 3.3 supports C++11, much of that is
implemented in the C++ standard library (GCC's libstdc++ or Clang's libc++)
header files, and no attempt is made in pts-clang to provide the
most up-to-date C++ standard library. With -xstatic, an old libstdc++ (the
one from gcc-4.4.3) is provided, and without -xstatic the system's default
libstdc++ will be used, which can be older than C++11.

Does pts-clang create portable executables?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
By default (without the -xstatic or -xermine flags), the executables created by
pts-clang are just as portable as those generated by `gcc' or `clang'. They
are dynamically linked (unless -static is specified), thus they depend on
the system libraries (e.g. /lib/libc.so.6).

If the -static flag is specified, then the executable becomes statically
linked, but this doesn't provide real portability,
because for calls such as getpwent(3) (getting info of Unix system users)
and gethostbyname(3) (DNS resolution), glibc loads files such as
libnss_compat.so, libnss_dns.so. On the target system those libraries may
be incompatible with your binary, so you may get a segfault or unintended
behavior. pts-xstatic solves this (see below), because it uses uClibc.

If the -xstatic flag is specified, pts-xstatic is used to create a portable
statically linked executable, linked against uClibc. See the ``Additional
features'' section for details.

If the -xermine flag is specified, Ermine is used to pack library and other
dependencies to a single, portable executable. This can be even more
portable than -xstatic, because Ermine can pack locale files, gconv
libraries etc. See the ``Additional features'' section for details.

Additional features
~~~~~~~~~~~~~~~~~~~
New features in addition to the original, base
http://llvm.org/releases/3.3/clang+llvm-3.3-i386-debian6.tar.bz2
(2013-06-10) release:

* pts-clang also supports a non-standard command-line flag -xstatic (at the
  beginning of the command-line), which makes it create statically linked
  Linux i386 binaries, using
  the uClibc etc. libraries. To use it, download pts-xstatic and extract it
  to the same directory (so that clangldxs and xstaticusr are neighbors).
  See more info here:
  http://raw.github.com/pts/pts-clang-xstatic/master/README.pts-xstatic.txt

* pts-clang also supports a non-standard command-line flag -xsysld (at the
  beginning of the command-line), which
  instructs it to use the system linker (whatever clang finds) rather than the
  supplied GNU gold linker.

* pts-clang also supports a non-standard command-line flag -xermine (and the
  -xermine,... variant to pass the ... argument to Ermine), which instructs it
  to process the output binary using Ermine. Ermine is a Linux ELF portable
  executable creator: it takes a dynamically linked ELF executable, discovers
  its dependencies (e.g. dynamic libraries, NSS libaries), and builds a
  protable, statically linked ELF executable containing all the dependencies.
  See the features, licensing information and get Ermine from
  http://www.magicermine.com/products.html .
  The result can be even more portable than -xstatic, because Ermine can pack
  locale files, gconv libraries etc. Not all the packing is automatic: use
  -xermine,... to specify packing flags to Ermine.
  To use the -xermine feature, create the symlink pts-clang/bin/ermine pointing to
  your Ermine binary, e.g. ErmineLight.i386.

* pts-clang autodetects and uses -m32 or -m64 based on system defaults (i.e.
  whether /bin/sh is a 64-bit executable). The original Clang release has an
  implicit -m32.

* To compile 64-bit executables, you need: 64-bit libgcc headers (such as
  stddef.h, included as clanginclude/stddef.h), 64-bit libgcc.a (included as
  pts-clang/clangld64/libgcc.a), libc headers with 64-bit support (usually
  it works with a recent glibc or eglibc, part of the Ubuntu package
  libc6-dev:amd64), and 64-bit libc.a or libc.so (these are part of the
  Ubuntu package libc6-dev:amd64). Installing GCC is not necessary.

* pts-clang (with `clang -m64 -c') can compile .c source code in 64-bit mode to
  Linux ELF amd64 .o relocatable files, even on older, non-multilib, 32-bit
  systems (such as Ubuntu Lucid) without libc6-dev:amd64. `clang -nostdlib
  -m64' also works. This is has
  limited usefulness, because many libc headers include
  architecture-specific C headers, and
  the latter is not available. hellow.c barely works, but e.g.
  `#include <sys/select.h>' doesn't work, because it tries to include
  bits/select.h, which is architecture-specific. Please note that this doesn't
  produce an amd64 executable, because libc6:amd64 or libc6-dev:amd64 would
  be needed for that.

Portability improvements
~~~~~~~~~~~~~~~~~~~~~~~~
* The release is portable (libc-independent): all shipped binaries are
  either statically linked. (The clang binary is packed with Ermine, the
  file is a statically linked executable, which contains a dynamically
  linked executables and its dependencies (e.g. libc etc.) with itself.) The
  system libraries are not used for running the compiler (but they are used
  for linking the output file).

* A statically linked linker (ld, GNU gold) binary is provided, so GNU
  binutils is not a requirement for compilation on the host system.

* Some other optional, statically linked binutils tools (ar, ranlib and
  strip) are also provided for convenience in the pts-static-binu binary
  release, see more info here:
  http://raw.github.com/pts/pts-clang-xstatic/master/README.pts-static-binu.txt
  . These tools can be used for auxiliary tasks such as building static
  libraries.

Dependencies
~~~~~~~~~~~~
The dependencies of pts-clang are the following:

* A Linux i386 or Linux amd64 host system.

* A mounted /proc filesystem.

* root access is not needed for execution, and it's usually not needed for
  installation.

* Without -xstatic or -sysld, pts-clang needs the library header (.h) and
  library code (.so and .a) files on the system. It doesn't need GCC or a
  system linker.

  On Ubuntu Precise, install this for C compilation:

    $ sudo apt-get install libc6-dev:amd64 libc6-dev:i386

  On Ubuntu Precise, install this additionally for C++ compilation:

    $ sudo apt-get install g++-4.6-multilib libstdc++6-4.6-dev

* With -sysld, the system linker (usually /usr/bin/ld, part of the binutils
  package), libgcc.a (part of some gcc package), crtbegin.o (etc., part of
  the same gcc package) are also needed in addition to the dependencies
  above.

  On Ubuntu Precise, install

    # sudo apt-get install binutils gcc-4.6-multilib gcc-4.6

* With -xstatic, the pts-xstatic package needs to be downloaded and
  extracted. (None of the Ubuntu packages above are needed.) Extract
  pts-xstatic to the same directory (so that clangldxs and xstaticusr are
  neighbors). See more info here:
  http://raw.github.com/pts/pts-clang-xstatic/master/README.pts-xstatic.txt

* For -xermine, install Ermine first, and create the symlink
  pts-clang/bin/ermine pointing to your Ermine binary, e.g. ErmineLight.i386.
  Example command to create the symlink:

    $ ln -snf ~/Downloads/ErmineLight.i386 pts-clang/bin/ermine

The included clang and ld tools don't use any system libraries (.so files)
to run, all dependencies are included. With -xstatic, the whole compilation
and linking end-to-end doesn't use any other files outside the extracted
pts-clang directory (except for the temporary files it creates in /tmp), so
it can be run in a chroot environment.

Troubleshooting
~~~~~~~~~~~~~~~
* `error: could not find Ermine: .../pts-clang/bin/ermine'

  This happens when you use the -xermine flag, but pts-clang can't find
  Ermine.

  Get an Ermine license, download Ermine, and create the symlink
  pts-clang/bin/ermine pointing to your Ermine binary, e.g. ErmineLight.i386
  or ErmineLightTrial.i386.

* `directory missing for -xstatic, please install:
  .../pts-clang/xstaticusr/include'

  This happens when you use the -xstatic flag, but pts-clang can't find
  pts-xstatic. Download and extract pts-xstatic to the same directory (so
  that clangldxs and xstaticusr are neighbors). See more info here:
  http://raw.github.com/pts/pts-clang-xstatic/master/README.pts-xstatic.txt

  Please note that it's possible to use pts-clang and pts-xstatic together,
  without extracting them to the same directory, by using the xstatic
  command (provided by pts-xstatic) instead of the -xstatic flag. See the
  ``Minimum installation'' section in the pts-xstatic documentation
  http://raw.github.com/pts/pts-clang-xstatic/master/README.pts-xstatic.txt
  for details.

Author, copyright and recompilation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
pts-clang was created by Péter Szabó <pts@fazekas.hu>, using
existing LLVM Clang, uClibc and Ermine, and writing some custom trampoline
code. See the details in
http://github.com/pts/pts-clang-xstatic/blob/master/info.txt

All software mentioned in this document is free software and open source,
except for Ermine.

Thanks to Ermine
~~~~~~~~~~~~~~~~
The author of pts-clang is grateful and says thank you to the author of
Ermine, who has provided a free-of-charge Ermine license, using which the
portable clang.bin binary was created from the official Clang binary release
(which is libc-dependent).

If you want to create portable Linux executables (and you don't care too
much about file size), give Ermine a try! It's the most comfortable,
easy-to-use, and comprehensive tool available.

__EOF__
