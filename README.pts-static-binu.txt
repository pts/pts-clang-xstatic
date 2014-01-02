README for the pts-static-binu binary release
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
pts-static-binu is collection of a few tools from GNU binutils,
statically linked for Linux i386, for portable usage on Linux i386 and Linux
amd64 systems. It contains the tools ar, ranlib and strip.

Use pts-static-binu as an optional companion of pts-xstatic
(http://raw.github.com/pts/pts-clang-xstatic/master/README.pts-xstatic.txt)
and pts-clang
(http://raw.github.com/pts/pts-clang-xstatic/master/README.pts-clang.txt).

pts-static-binu was compiled from GNU binutils-2.22 with the 6ubuntu1.1
(Ubuntu Precise) patches.

The tools in pts-static-binu support support targets Linux ELF i386 and Linux
ELF amd64. (They may also support some non-Linux ELF targets.)

Installation
~~~~~~~~~~~~
To use pts-static-binu on a Linux i386 or Linux amd64 system, download it and
extract it to any directory. Here is how to do it:

  $ rm -f pts-static-binu-latest.sfx.7z
  $ wget http://pts.50.hu/files/pts-static-binu/pts-static-binu-latest.sfx.7z
  $ chmod +x pts-static-binu-latest.sfx.7z
  $ ./pts-static-binu-latest.sfx.7z -y  # Creates the pts-static-binu directory.

If you don't want to run the self-extracting archive, you can extract it
using 7z instead (`sudo apt-get install p7zip-full' first):

  $ 7z x -y pts-static-binu-latest.sfx.7z

Now you can already run pts-static-binu/bin/ar etc. If you don't like typing
that much, you can either create symlinks to pts-static-binu/bin/ar etc. in
a directory in your $PATH, or add pts-static-binu/bin to your $PATH. An
example temporary solution (which affects the current terminal window only):

  $ export PATH="$PWD/pts-static-binu/bin:$PATH"
  $ ar --version

__EOF__
