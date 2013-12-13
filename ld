#!/usr/bin/perl -w
# by pts@fazekas.hu at Thu Dec 12 22:31:57 CET 2013

use integer;
use strict;

# When gcc runs ld (as collect2), the -Wl,... flags are not passed at the
# front, so we have to look for --xstatic-ld in each arg.
my $i;
my $xstatic_prog;
my $verbose_arg;
for ($i = 1; $i < @ARGV; ++$i) {
  my $arg = $ARGV[$i];
  if ($arg eq '--xstatic-ld' and $i + 2 < @ARGV) {  # Just a marker with an arg.
    $verbose_arg = $ARGV[$i + 1];
    $xstatic_prog = $ARGV[$i + 2];
    splice @ARGV, $i, 3;
    last;
  }
}
die "$0: error: --xstatic-ld flag not found\n" if !defined($xstatic_prog);
my @a = ($xstatic_prog, '--do-xstatic-ld', $verbose_arg, @ARGV);
die "$0: error: exec $a[0]: $!\n" if !exec @a;
