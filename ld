#!/usr/bin/perl -w
# by pts@fazekas.hu at Thu Dec 12 22:31:57 CET 2013
#
# TODO(pts): collect2 calls it -- do we have to change its flags?

use integer;
use strict;

die "$0: error: missing --sysroot=...\n" if
    !@ARGV or $ARGV[0] !~ /\A--sysroot=(.*+)/;
my $sysroot = $1;
#my $lgccld = "-L$sysroot/gccld";
my $lgccld = "-L$0";
die "$0: error: missing / in command name" if $lgccld !~ s@/[^/]++\Z(?!\n)@@;

my $is_static;
my $i;
my $gccbdir;
for ($i = 1; $i < @ARGV; ++$i) {
  my $arg = $ARGV[$i];
  if ($arg eq '-static') {
    $is_static = 1;
  # /usr/lib/gcc/i486-linux-gnu/4.4.3/crtbeginT.o
  } elsif (!defined $gccbdir and $arg =~ m@(/crtbegin[A-Z]?[.]o)\Z(?!\n)@) {
    $gccbdir = substr($arg, 0, -length($1));
    last if $is_static;  # Shortcut.
  }
}
die "$0: error: missing -static\n" if !$is_static;
if (!defined $gccbdir) {  # Probably `gcc -nostdlib'.
  for ($i = 1; $i < @ARGV; ++$i) {
    my $arg = $ARGV[$i];
    if ($arg =~ /\A-L/ and $arg ne $lgccld) {
      # Pick the first -L which is not uClibc's ($lgccld).
      $gccbdir = substr($arg, 2);
      last;
    }
  }
  die "$0: error: cannot detect gcc built-in -B directory\n" if
      !defined($gccbdir);
}

# `ld -nostdlib' is different from `gcc -nostdlib'.
my @a = ('ld', '-nostdlib');
for ($i = 1; $i < @ARGV; ++$i) {
  my $arg = $ARGV[$i];
  if ($arg eq '--build-id' or
      $arg eq '--hash-style=both' or
      $arg eq '-L/usr/lib' or
      $arg eq '-L/lib' or
      $arg eq '-L/usr/local/lib' or
      $arg eq $lgccld or
      # -L/usr/lib/gcc/i486-linux-gnu/4.4.3/../../../../lib  # /usr/lib
      $arg =~ m@\A-L@ and $arg =~ m@/\.\./\.\./\.\./\.\./lib\Z(?!\n)@ or
      $arg =~ m@\A-L@ and $arg =~ m@/\.\./\.\./\.\.\Z(?!\n)@ or
     0) {
  } elsif ($arg eq '-z' and $i + 1 < @ARGV and $ARGV[$i + 1] eq 'relro') {
    ++$i;
  } else {
    push @a, $arg;
  }
}

if (-f "$gccbdir/ld") {
  # If not found here we could try "$gccbdir/../../ld", i.e. /usr/lib/gcc/ld,
  # because `gcc -B/usr/lib/gcc' also tries it.
  $a[0] = "$gccbdir/ld";  # Override 'ld' (on $PATH).
}

#print STDERR "running linker: @a\n";
die "$0: error: exec $a[0]: $!\n" if !exec @a;
