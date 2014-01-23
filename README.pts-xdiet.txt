README for the pts-xdiet Linux i386 diet libc static compilation tool
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
pts-xdiet is a convenient wrapper tool for compiling and creating portable,
statically linked Linux i386 executables in C and C++. It works on Linux
i386 and Linux x86_64 host systems. It wraps an existing compiler (GCC or
Clang) of your choice, and it links against diet libc and the other base
libraries included in the pts-xdiet binary release.

See also pts-xstatic
(http://raw.github.com/pts/pts-clang-xstatic/master/README.pts-xstatic.txt)
which is similar to pts-xdiet, but it works with uClibc rather then diet
libc. The executables compiled by pts-xdiet tend to be smaller than those by
pts-xstatic. Stay tuned for more info about execution speed and
compatibility (e.g. C++ and libraries) differences.

C compilers supported: gcc-4.1 ... gcc-4.8, clang-3.0 ... clang-3.3. C++
compilers supported: g++ and clang++ corresponding to the supported C
compilers. Compatible uClibc C and C++ headers (.h) and precompiled static
libraries (e.g. libc.a, IN THE FUTURE libz.a, IN THE FUTURE libstdc++.a) are
also provided by pts-xdiet. To minimize system dependencies, pts-xdiet can
compile with pts-clang (for both C and C++), which is portable, and you can
install it as non-root.

Please note that pts-xdiet is under heavy development and testing. Stay
tuned for more info here later.

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
