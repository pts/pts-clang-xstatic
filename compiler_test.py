#! /bin/sh
# by pts@fazekas.hu at Sun Dec 22 18:43:24 CET 2013

""":" #xstatic_test.py: Unit tests for an installed xstatic.

type python2.7 >/dev/null 2>&1 && exec python2.7 -- "$0" ${1+"$@"}
type python2.6 >/dev/null 2>&1 && exec python2.6 -- "$0" ${1+"$@"}
type python2.5 >/dev/null 2>&1 && exec python2.5 -- "$0" ${1+"$@"}
type python2.4 >/dev/null 2>&1 && exec python2.4 -- "$0" ${1+"$@"}
exec python -- ${1+"$@"}; exit 1

Requirements: Python (2.4, 2.5, 2.6 or 2.7; doesn't work with 3.x) and an
installed xstatic command on $PATH.
"""

import cStringIO
import difflib
import os
import os.path
import pipes
import platform
import signal
import struct
import subprocess
import sys
import unittest

TMPPROG = 'xstatic_test.tmp.%d.prog' % os.getpid()
SRCDIR = 'testsrc'



def FindOnPath(prog):
  if os.sep in prog:
    if os.path.isfile(prog):
      return prog
  else:
    for dirname in os.getenv('PATH', '/bin::/usr/bin').split(os.pathsep):
      pathname = os.path.join(dirname, prog)
      if os.path.isfile(pathname):
        return pathname
  return None


XSTATIC = FindOnPath('xstatic')
# Find the clang binary next to the xstatic binary (after following symlinks).
#
# Also converts clang to absolute path.
LOCAL_CLANG = os.path.join(os.path.dirname(os.path.realpath(XSTATIC)), 'clang')
HAS_64BIT_HEADERS = os.path.isfile(os.path.dirname(
    FindOnPath('gcc') or '/usr') +
    '/../include/x86_64-linux-gnu/gnu/stubs-64.h')


def MaybeRemove(filename):
  try:
    os.remove(filename)
  except OSError:
    pass
  assert not os.path.exists(filename)


def WriteAndRemove(filename):
  MaybeRemove(filename)
  open(filename, 'wb').close()
  MaybeRemove(filename)


def GetProg(compiler):
  return compiler[compiler[0] == XSTATIC]


def FindCompilerOnPath(compiler):
  if compiler is None:
    return None
  elif compiler is ():
    return True
  else:
    return FindOnPath(GetProg(compiler))


def DelUnusedTestClasses():
  """Delete test classes whose compiler is not available."""
  g = globals()
  g.pop('TestBaseCases')
  base = g.pop('TestBase')
  skip_count = 0
  names_to_delete = set()
  for name, value in sorted(g.iteritems()):
    if isinstance(value, type) and issubclass(value, base):
      if ((value.EXPECT_BITS == 64 and not HAS_64BIT_HEADERS) or
          not FindCompilerOnPath(value.C_COMPILER) or
          not FindCompilerOnPath(value.CC_COMPILER)):
        print >>sys.stderr, 'info: skipping: %s' % (value.__name__)
        skip_count += 1
        names_to_delete.add(name)
  for name in sorted(names_to_delete):
    del g[name]
  return skip_count


def SetupTimeout(sec):
  if not isinstance(sec, int):
    raise TypeError

  def TimeoutFn():
    # TODO(pts): Make this compatible with Windows.
    signal.alarm(sec)

  return TimeoutFn


PT_DYNAMIC = 2
PT_INTERP = 3


def IsElfLsb(filename, expect_static, expect_bits, expect_type):
  if expect_bits == 32:
    bit_str = '\001'
  elif expect_bits == 64:
    bit_str = '\002'
  else:
    raise ValueError('Unsupported expect_bits=%r' % (expect_bits,))
  if expect_type == 'relocatable':
    e_type_str = '\001'
  elif expect_type == 'executable':
    e_type_str = '\002'
  else:
    raise ValueError('Unsupported expect_type=%r' % (expect_type,))
  f = open(filename)
  try:
    head = f.read(4096)
  finally:
    f.close()
  # Check ELF, 32-bit, LSB, executable.
  if head[:6] != '\177ELF%s\001' % bit_str or head[16] != e_type_str:
    return False
  if expect_static is None or expect_type == 'relocatable':
    return True
  is_static = True
  # Now we check for static linkage. For that we check that there is no
  # PT_DYNAMIC or PT_INTERP field in the program header.
  if expect_bits == 64:
    e_phoff, = struct.unpack('<Q', head[32 : 40])
    e_phentsize, e_phnum = struct.unpack('<HH', head[54 : 58])
    assert e_phentsize == 56, 'bad ELF program header size: %d' % e_phentsize
  else:
    e_phoff, = struct.unpack('<L', head[28 : 32])
    e_phentsize, e_phnum = struct.unpack('<HH', head[42 : 46])
    assert e_phentsize == 32, 'bad ELF program header size: %d' % e_phentsize
  assert e_phoff < len(head), 'ELF program header starts too late'
  phend = e_phoff + e_phentsize * e_phnum
  assert phend <= len(head), 'ELF program header ends too late'
  for i in xrange(e_phoff, phend, e_phentsize):
    p_type, = struct.unpack('<L', head[i : i + 4])
    if p_type in (PT_DYNAMIC, PT_INTERP):
      is_static = False  # Found a dynamically linked executable.
      break
  return bool(expect_static) == is_static


STDOUT_LINE_PREFIX = '//: '


def IsOldCompiler(compiler, is_clang, version):
  """Return True iff compiler is clang older than version."""
  is_clang = bool(is_clang)
  if is_clang != ('clang' in os.path.basename(GetProg(compiler))):
    return False
  cmd = tuple(compiler) + ('-v',)
  p = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  cmd_str = ' '.join(map(pipes.quote, cmd))
  try:
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         preexec_fn=SetupTimeout(10))
  except OSError, e:
    # Most of the time: `No such file or directory'.
    assert 0, 'could not run compiler (%s): %s' % (e, cmd_str)
  try:
    stdout, stderr = p.communicate('')
  finally:
    exit_code = p.wait()
  assert not exit_code, 'compiler failed: %s' % cmd_str
  assert not stdout, 'unexpected compiler stdout: %s\n%s' % (cmd_str, stdout)
  if is_clang:
    s = 'clang version '
  else:
    s = 'gcc version '
  i = stderr.find(s)
  assert i >= 0, 'missing version in compiler output: %s\n%s' % (
      cmd_str, stdout)
  j = stderr.find(' (', i + len(s))
  assert j >= 0, 'missing version end in compiler output: %s\n%s' % (
      cmd_str, stdout)
  return stderr[i + len(s) : j] < version


def AssertCompileRun(src_filename, cmd, expect_stdout,
                     expect_link_error=False,
                     expect_compile_error=False,
                     expect_compile_warning=False,
                     expect_compile_stdout=False,
                     expect_compile_fail=None,
                     expect_runtime_error=False,
                     expect_static=True,
                     expect_bits=32):
  if expect_stdout is True:
    if expect_link_error or expect_compile_error or expect_compile_stdout:
      expect_stdout = ''
    else:
      if src_filename is None:
        raise ValueError('Please specify src_filename= or expect_stdout=')
      f = open(src_filename)
      try:
        data = []
        for line in f:
          if not line.startswith(STDOUT_LINE_PREFIX):
            break
          data.append(line[len(STDOUT_LINE_PREFIX):])
      finally:
        f.close()
      expect_stdout = ''.join(data)
      del data
  MaybeRemove(TMPPROG)
  cmd_str = ' '.join(map(pipes.quote, cmd))
  try:
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         preexec_fn=SetupTimeout(10))
  except OSError, e:
    # Most of the time: `No such file or directory'.
    assert 0, 'could not run compiler (%s): %s' % (e, cmd_str)
  try:
    stdout, stderr = p.communicate('')
  finally:
    exit_code = p.wait()
  stderr_left = []
  src_filename_colon = (src_filename or 'NONE') + ':'
  src_filename_infunc = (src_filename or 'NONE') + ': In function '
  if stderr:
    for line in cStringIO.StringIO(stderr):
      if expect_link_error and '.o:' in line:
        pass
      elif expect_link_error and '): undefined reference to ' in line:
        pass
      elif expect_link_error and '): error: undefined reference to ' in line:
        pass
      elif expect_link_error and '.o: In function ' in line:
        pass
      elif expect_link_error and ' has invalid symbol index ' in line:
        pass
      elif (expect_link_error and
            'error: linker command failed with exit code 1' in line):
        pass
      elif (expect_link_error and
            ': ld returned 1 exit status\n' in line):
        pass
      elif expect_compile_error and line.startswith(src_filename_colon):
        pass
      elif (expect_compile_warning and line.startswith(src_filename_colon) and
            ': warning:' in line):
        pass
      elif (expect_compile_warning and '.h:' in line and ': warning:' in line):
        pass
      elif (expect_compile_error or expect_compile_warning) and (
          line.startswith(' ')):
        pass  # GCC 4.8 without -fno-disagnostics-show-caret.
      elif ((expect_compile_error or expect_compile_warning) and
            (line.startswith('In file included from ') or
             line.startswith('                 from ') or
             line.startswith(src_filename_infunc))):
         pass
      elif "warning: treating 'c' input as 'c++' when in C++ mode" in line:
        pass  # Harmless warning from Clang.
      else:
        stderr_left.append(line)
  stderr_left = ''.join(stderr_left)

  # This also checks that there were no warnings.
  if isinstance(expect_compile_error, str):
    assert expect_compile_error in stderr, (
        'compiler error without %r, exit_code=%d: %s\n%s' %
        (expect_compile_error, exit_code, cmd_str, stderr))
  else:
    assert not stderr_left, 'compiler error message, exit_code=%d: %s\n%s' % (
        exit_code, cmd_str, stderr)
  if isinstance(expect_compile_stdout, str):
    assert expect_compile_stdout in stdout, (
        'compiler stdout without %r, exit_code=%d: %s\n%s' %
        (expect_compile_stdout, exit_code, cmd_str, stdout))
  elif expect_compile_stdout is False:
    assert not stdout, 'compiler wrote to stdout, exit_code=%d: %s\n%s' % (
        exit_code, cmd_str, stdout)
  else:
    raise ValueError('Unknown expect_compile_stdout value: %r' %
                     expect_compile_stdout)
  if isinstance(expect_compile_warning, str):
    assert expect_compile_warning in stderr, (
        'compiler error without warning %r, exit_code=%d: %s\n%s' %
        (expect_compile_warning, exit_code, cmd_str, stderr))

  if expect_compile_fail is None:
    cf = bool(expect_link_error or expect_compile_error)
  else:
    cf = expect_compile_fail
  if cf:
    assert exit_code, 'compiler succeeded unexpectedly: %s' % cmd_str
  else:
    assert not exit_code, 'compiler failed with exit_code=%d: %s' % (
        exit_code, cmd_str)
  if expect_link_error or expect_compile_error or expect_compile_stdout:
    assert not os.path.isfile(TMPPROG), (
        'compiler generated binary %s: %s' % (TMPPROG, cmd_str))
    return
  assert os.path.isfile(TMPPROG), (
      'compiler did not generate binary %s: %s' % (TMPPROG, cmd_str))
  if '-c' in cmd:
    expect_type = 'relocatable'
  else:
    expect_type = 'executable'
  assert IsElfLsb(TMPPROG, expect_static, expect_bits, expect_type), (
      'compiler did not generate an ELF %d-bit LSB, static=%r '
      '%s %s: %s' % (expect_bits, expect_static, expect_type, TMPPROG, cmd_str))
  if expect_type == 'relocatable':
    return

  if os.sep in TMPPROG:
    tmpprog_cmd = (TMPPROG,)
  else:
    tmpprog_cmd = (os.path.join('.', TMPPROG),)
  tmpprog_cmd_str = ' '.join(map(pipes.quote, tmpprog_cmd))
  p = subprocess.Popen(tmpprog_cmd, stdin=subprocess.PIPE,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       preexec_fn=SetupTimeout(3))
  try:
    stdout, stderr = p.communicate('')
  finally:
    exit_code = p.wait()
  assert not stderr, 'prog run error, exit_code=%d: %s\n%s' % (
      exit_code, tmpprog_cmd_str, stderr)
  if expect_runtime_error:
    assert exit_code, 'prog succeeded unexpectedly: %s' % tmpprog_cmd_str
  else:
    assert not exit_code, 'prog failed with exit_code=%d: %s' % (
        exit_code, tmpprog_cmd_str)
  if expect_stdout is not False and stdout != expect_stdout:
    assert 0, (
        'unexpected prog stdout, exit_code=%d: %s\n%s' %
        (exit_code, tmpprog_cmd_str, ''.join(difflib.unified_diff(
             cStringIO.StringIO(expect_stdout).readlines(),
             cStringIO.StringIO(stdout).readlines(),
             fromfile='expected', tofile='from ' + src_filename, n=5))))


MSG_NO_INPUT_FILES = ': no input files'
MSG_THREAD_MODEL = '\nThread model: '
# A common substring of GCC and Clang --help messages.
MSG_HELP =' -Wl,'
MSG_XSTATIC_HELP = (
    'Invokes the C/C++ compiler with -static against the xstatic uClibc.\n')
MSG_XSTATIC_ARGV1DASH = (
    'xstatic: error: please specify gcc|clang prog in 1st arg\n')
MSG_FLAG_M64_NOT_SUPPORTED = 'xstatic: error: flag not supported: -m64'

DEFAULT_FLAGS = ('-s', '-O2', '-W', '-Wall')


class TestBase(unittest.TestCase):
  C_COMPILER  = None  # Must be overridden in subclasses.
  CC_COMPILER = None  # Must be overridden in subclasses.
  EXPECT_STATIC = True
  EXPECT_BITS = 32

  def tearDown(self):
    MaybeRemove(TMPPROG)

  def GetCompiler(self, filename):
    ext = os.path.splitext(filename)[1]
    if ext == '.cc':
      return self.CC_COMPILER
    elif ext == '.c':
      return self.C_COMPILER
    else:
      assert 0, 'unknown source ext for: %s' % filename

  def AssertProg(self, src_filename, flags=DEFAULT_FLAGS,
                 is_cc=None, expect_compile_fail=None,
                 expect_stdout=True, expect_link_error=False,
                 expect_compile_warning=False, expect_runtime_error=False,
                 expect_compile_error=False, expect_compile_stdout=False,
                 expect_bits=None):
    if is_cc is None:
      if src_filename is None:
        raise ValueError('Please specify src_filename= or is_cc=')
      compiler = self.GetCompiler(src_filename)
    elif is_cc is True:
      compiler = self.CC_COMPILER
    elif is_cc is False:
      compiler = self.C_COMPILER
    elif isinstance(is_cc, tuple):
      compiler = is_cc
    else:
      raise ValueError('Unknown is_cc value: %r' % is_cc)
    if src_filename is not None and os.sep not in src_filename:
      src_filename = os.path.join(SRCDIR, src_filename)
    cmd = list(compiler)
    if ('-m32' in flags or '-m64' in flags) and cmd[-1] in ('-m32', '-m64'):
      cmd.pop()
    if (expect_compile_error is True or expect_compile_warning) and (
        'clang' in GetProg(compiler)):
      # Clang Caret diagnostics are printed at the beginning of the line,
      # with no leading space. That can confuse our error message parser, so
      # we just disable them. GCC caret diagnostics have a leading space.
      cmd.append('-fno-caret-diagnostics')
    cmd.extend(flags)
    if src_filename is not None:
      cmd.extend(('-o', TMPPROG, src_filename))
    cmd = tuple(cmd)
    if expect_bits is None:
      expect_bits = self.EXPECT_BITS
    AssertCompileRun(src_filename=src_filename, cmd=cmd,
                     expect_stdout=expect_stdout,
                     expect_compile_fail=expect_compile_fail,
                     expect_link_error=expect_link_error,
                     expect_compile_error=expect_compile_error,
                     expect_compile_warning=expect_compile_warning,
                     expect_compile_stdout=expect_compile_stdout,
                     expect_runtime_error=expect_runtime_error,
                     expect_static=self.EXPECT_STATIC,
                     expect_bits=expect_bits)


class TestBaseCases(TestBase):
  def testNoInputFiles(self):
    self.AssertProg(None, is_cc=False, flags=(),
                    expect_compile_error=MSG_NO_INPUT_FILES)

  def testNoInputFilesCc(self):
    self.AssertProg(None, is_cc=True, flags=(),
                    expect_compile_error=MSG_NO_INPUT_FILES)

  def testNoInputFilesM32(self):
    self.AssertProg(None, is_cc=False, flags=('-m32',),
                    expect_compile_error=MSG_NO_INPUT_FILES)

  def testNoInputFilesM32Cc(self):
    self.AssertProg(None, is_cc=True, flags=('-m32',),
                    expect_compile_error=MSG_NO_INPUT_FILES)

  def testNoInputFilesM64(self):
    if self.C_COMPILER and self.C_COMPILER[0] == XSTATIC:
      self.AssertProg(None, is_cc=False, flags=('-m64',),
                      expect_compile_error=MSG_FLAG_M64_NOT_SUPPORTED)
    else:
      self.AssertProg(None, is_cc=False, flags=('-m64',),
                      expect_compile_error=MSG_NO_INPUT_FILES)

  def testNoInputFilesM64Cc(self):
    if self.CC_COMPILER and self.CC_COMPILER[0] == XSTATIC:
      self.AssertProg(None, is_cc=True, flags=('-m64',),
                      expect_compile_error=MSG_FLAG_M64_NOT_SUPPORTED)
    else:
      self.AssertProg(None, is_cc=True, flags=('-m64',),
                      expect_compile_error=MSG_NO_INPUT_FILES)

  def testHelpOnly(self):
    self.AssertProg(None, is_cc=False, flags=('--help',),
                    expect_compile_stdout=MSG_HELP)

  def testHelpOnlyCc(self):
    self.AssertProg(None, is_cc=True, flags=('--help',),
                    expect_compile_stdout=MSG_HELP)

  def testDashVOnly(self):
    self.AssertProg(None, is_cc=False, flags=('-v',),
                    expect_compile_fail=False,
                    expect_compile_error=MSG_THREAD_MODEL)

  def testDashVOnlyCc(self):
    self.AssertProg(None, is_cc=True, flags=('-v',),
                    expect_compile_fail=False,
                    expect_compile_error=MSG_THREAD_MODEL)

  def testM32DashVOnly(self):
    self.AssertProg(None, is_cc=False, flags=('-m32', '-v',),
                    expect_compile_fail=False,
                    expect_compile_error=MSG_THREAD_MODEL)

  def testM32DashVOnlyCc(self):
    self.AssertProg(None, is_cc=True, flags=('-m32','-v',),
                    expect_compile_fail=False,
                    expect_compile_error=MSG_THREAD_MODEL)

  def testM64DashVOnly(self):
    if self.C_COMPILER and self.C_COMPILER[0] == XSTATIC:
      self.AssertProg(None, is_cc=False, flags=('-m64', '-v'),
                      expect_compile_error=MSG_FLAG_M64_NOT_SUPPORTED)
    else:
      self.AssertProg(None, is_cc=False, flags=('-m64', '-v',),
                      expect_compile_fail=False,
                      expect_compile_error=MSG_THREAD_MODEL)

  def testM64DashVOnlyCc(self):
    if self.CC_COMPILER and self.CC_COMPILER[0] == XSTATIC:
      self.AssertProg(None, is_cc=True, flags=('-m64', '-v'),
                      expect_compile_error=MSG_FLAG_M64_NOT_SUPPORTED)
    else:
      self.AssertProg(None, is_cc=True, flags=('-m64','-v',),
                      expect_compile_fail=False,
                      expect_compile_error=MSG_THREAD_MODEL)

  def testHellowC(self):
    self.AssertProg('hellow.c')

  def testHellowCObj(self):
    self.AssertProg('hellow.c', flags=('-c',))

  def testLinkerrC(self):
    self.AssertProg('linkerr.c', expect_link_error=True)

  def testCompileerrC(self):
    self.AssertProg('compileerr.c', expect_compile_error=True)

  def testWarningC(self):
    self.AssertProg('warning.c', expect_compile_warning=True)

  def testHellowCCc(self):
    self.AssertProg('hellow.c', is_cc=True)

  def testLinkerrCCc(self):
    self.AssertProg('linkerr.c', is_cc=True, expect_link_error=True)

  def testCompileerrCCc(self):
    self.AssertProg('compileerr.c', is_cc=True, expect_compile_error=True)

  def testWarningCCc(self):
    self.AssertProg('warning.c', is_cc=True, expect_compile_warning=True)

  def testHelloCc(self):
    self.AssertProg('hello.cc')

  def testHelloCcObj(self):
    self.AssertProg('hello.cc', flags=('-c',))

  def testHelloCcNoExcNoRtti(self):
    self.AssertProg('hello.cc',
                    flags=DEFAULT_FLAGS + ('-fno-exceptions', '-fno-rtti'))

  def testHellostrCc(self):
    self.AssertProg('hellostr.cc')

  def testIncludeExceptionCc(self):
    self.AssertProg('include_exception.cc', flags=('-std=c++0x',))

  def testIncludeManyCc(self):
    self.AssertProg('include_many.cc')

  def testIncludeManyNoExcNoRtti(self):
    self.AssertProg('include_many.cc',
                    flags=DEFAULT_FLAGS + ('-fno-exceptions', '-fno-rtti'))

  def testIncludeManyCcDeprecated(self):
    self.AssertProg('include_many.cc',
                    flags=DEFAULT_FLAGS + ('-DUSE_DEPR',),
                    expect_compile_warning='deprecated')

  def testIncludeManyCcWNoDeprecated(self):
    self.AssertProg('include_many.cc',
                    flags=DEFAULT_FLAGS + ('-DUSE_DEPR', '-Wno-deprecated'))

  def testExcCcNoRtti(self):
    self.AssertProg('exc.cc',
                    flags=DEFAULT_FLAGS + ('-fno-rtti',))

  def testExcCcNoRttiEarlyWarning(self):
    if 'clang' in os.path.basename(GetProg(self.CC_COMPILER)):
      expect_compile_warning = False
    else:
      expect_compile_warning = 'exception'
    self.AssertProg('exc.cc',
                    flags=DEFAULT_FLAGS + ('-fno-rtti', '-DUSE_EARLY'),
                    expect_compile_warning=expect_compile_warning)

  def testExcCcNoExcNoRtti(self):
    self.AssertProg('exc.cc',
                    flags=DEFAULT_FLAGS + ('-fno-exceptions', '-fno-rtti'),
                    expect_compile_error=True)

  def testHelloCcNoExcNoRtti(self):
    self.AssertProg('virtual.cc',
                    flags=DEFAULT_FLAGS + ('-fno-exceptions', '-fno-rtti'))

  def testLinkerrCcNoExcNoRtti(self):
    self.AssertProg('linkerrc.cc',
                    flags=DEFAULT_FLAGS + ('-fno-exceptions', '-fno-rtti'))

  def testLinkerrCcCNoExcNoRtti(self):
    # With -O1 and -O2 clang is too smart to optimize the dynamic memory
    # allocation away.
    flags = filter(lambda flag: flag != '-O2', DEFAULT_FLAGS)
    self.AssertProg('linkerrc.cc', is_cc=False,
                    flags=flags + ('-O0', '-fno-exceptions', '-fno-rtti'),
                    expect_link_error=True)

  def testDyncastCcNoExc(self):
    self.AssertProg('dyncast.cc',
                    flags=DEFAULT_FLAGS + ('-fno-exceptions',))

  def testDyncastCcNoExcNoRtti(self):
    if ('clang' in os.path.basename(GetProg(self.CC_COMPILER)) or
        IsOldCompiler(self.CC_COMPILER, False, '4.1~')):
      kwargs = {'expect_runtime_error': True, 'expect_stdout': False}
    else:
      kwargs = {'expect_compile_error': True}
    self.AssertProg('dyncast.cc',
                    flags=DEFAULT_FLAGS + ('-fno-exceptions', '-fno-rtti'),
                    **kwargs)

  def testRttiCcNoExc(self):
    self.AssertProg('rtti.cc',
                    flags=DEFAULT_FLAGS + ('-fno-exceptions',))

  def testRttiCcNoExcNoRtti(self):
    if IsOldCompiler(self.CC_COMPILER, True, '3.0~'):
      # Known to work like this in clang++-3.0.
      kwargs = {'expect_runtime_error': True, 'expect_stdout': False}
    else:
      # Known to work like this in clang++-3.2 and clang++-3.3.
      kwargs = {'expect_compile_error': True}
    self.AssertProg('rtti.cc',
                    flags=DEFAULT_FLAGS + ('-fno-exceptions', '-fno-rtti'),
                    **kwargs)


class XstaticNoCompilerFlagsTest(TestBase):
  C_COMPILER = CC_COMPILER = ()

  def testNoArg(self):
    self.AssertProg(None, is_cc=(XSTATIC,), flags=(), expect_compile_fail=True,
                    expect_compile_stdout=MSG_XSTATIC_HELP)

  def testHelpArg(self):
    self.AssertProg(None, is_cc=(XSTATIC,), flags=('--help',),
                    expect_compile_fail=True,
                    expect_compile_stdout=MSG_XSTATIC_HELP)

  def testDashVArg(self):
    self.AssertProg(None, is_cc=(XSTATIC,), flags=('-v',),
                    expect_compile_error=MSG_XSTATIC_ARGV1DASH)


class SysGccTest(TestBaseCases):
  EXPECT_STATIC = False
  C_COMPILER  = ('gcc', '-m32')
  CC_COMPILER = ('g++', '-m32')


class SysGcc64Test(TestBaseCases):
  EXPECT_STATIC = False
  EXPECT_BITS = 64
  C_COMPILER  = ('gcc', '-m64')
  CC_COMPILER = ('g++', '-m64')


class SysClangTest(TestBaseCases):
  EXPECT_STATIC = False
  C_COMPILER  = ('clang',   '-m32')
  CC_COMPILER = ('clang++', '-m32')


class SysClang64Test(TestBaseCases):
  EXPECT_STATIC = False
  EXPECT_BITS = 64
  C_COMPILER  = ('clang',   '-m64')
  CC_COMPILER = ('clang++', '-m64')


class LocalClangTest(TestBaseCases):
  EXPECT_STATIC = False
  C_COMPILER  = (LOCAL_CLANG,        '-m32')
  CC_COMPILER = (LOCAL_CLANG + '++', '-m32')

  # This works even on a 32-bit system without 64-bit libs. Please note that
  # we'd need to copy lots of files to clanginc64low/bits/ to make everything
  # work, e.g. `#include <sys/select.h>' would need bits/select.h.
  def testHellowCObj64(self):
    self.AssertProg('hellow.c', flags=('-m64', '-c'), expect_bits=64)

  # This doesn't work on a 32-bit system without 64-bit libs, it would need
  # the 64-bit /usr/include/c++/4.6/x86_64-linux-gnu/bits/c++config.h
  #def testHelloCcObj64(self):
  #  self.AssertProg('hello.cc', flags=('-m64', '-c'))


class LocalClang64Test(TestBaseCases):
  EXPECT_STATIC = False
  EXPECT_BITS = 64
  C_COMPILER  = (LOCAL_CLANG,        '-m64')
  CC_COMPILER = (LOCAL_CLANG + '++', '-m64')


class LocalClangXsysldFlagTest(TestBaseCases):
  EXPECT_STATIC = False
  C_COMPILER  = (LOCAL_CLANG,        '-xsysld', '-m32')
  CC_COMPILER = (LOCAL_CLANG + '++', '-xsysld', '-m32')


class LocalClang64XsysldFlagTest(TestBaseCases):
  EXPECT_STATIC = False
  EXPECT_BITS = 64
  C_COMPILER  = (LOCAL_CLANG,        '-xsysld', '-m64')
  CC_COMPILER = (LOCAL_CLANG + '++', '-xsysld', '-m64')


class LocalClangXstaticFlagTest(TestBaseCases):
  C_COMPILER  = (LOCAL_CLANG,        '-xstatic')
  CC_COMPILER = (LOCAL_CLANG + '++', '-xstatic')


class XstaticGccTest(TestBaseCases):
  C_COMPILER  = (XSTATIC, 'gcc')
  CC_COMPILER = (XSTATIC, 'g++')


class XstaticGcc41Test(TestBaseCases):
  C_COMPILER  = (XSTATIC, 'gcc-4.1')
  CC_COMPILER = (XSTATIC, 'g++-4.1')


class XstaticGcc42Test(TestBaseCases):
  C_COMPILER  = (XSTATIC, 'gcc-4.2')
  CC_COMPILER = (XSTATIC, 'g++-4.2')


class XstaticGcc43Test(TestBaseCases):
  C_COMPILER  = (XSTATIC, 'gcc-4.3')
  CC_COMPILER = (XSTATIC, 'g++-4.3')


class XstaticGcc44Test(TestBaseCases):
  C_COMPILER  = (XSTATIC, 'gcc-4.4')
  CC_COMPILER = (XSTATIC, 'g++-4.4')


class XstaticGcc45Test(TestBaseCases):
  C_COMPILER  = (XSTATIC, 'gcc-4.5')
  CC_COMPILER = (XSTATIC, 'g++-4.5')


class XstaticGcc46Test(TestBaseCases):
  C_COMPILER  = (XSTATIC, 'gcc-4.6')
  CC_COMPILER = (XSTATIC, 'g++-4.6')


class XstaticGcc47Test(TestBaseCases):
  C_COMPILER  = (XSTATIC, 'gcc-4.7')
  CC_COMPILER = (XSTATIC, 'g++-4.7')


class XstaticGcc48Test(TestBaseCases):
  C_COMPILER  = (XSTATIC, 'gcc-4.8')
  CC_COMPILER = (XSTATIC, 'g++-4.8')


class XstaticGcc49Test(TestBaseCases):
  C_COMPILER  = (XSTATIC, 'gcc-4.9')
  CC_COMPILER = (XSTATIC, 'g++-4.9')


class XstaticClangTest(TestBaseCases):
  C_COMPILER  = (XSTATIC, 'clang')
  CC_COMPILER = (XSTATIC, 'clang++')


class XstaticLocalClangTest(TestBaseCases):
  C_COMPILER  = (XSTATIC, LOCAL_CLANG)
  CC_COMPILER = (XSTATIC, LOCAL_CLANG + '++')


if __name__ == '__main__':
  os.chdir(os.path.dirname(__file__))
  hc = os.path.join(SRCDIR, 'hellow.c')
  assert os.path.isfile(hc), 'not found: %s' % hc
  del hc
  assert XSTATIC, 'not found on path: xstatic'
  WriteAndRemove(TMPPROG)
  skip_count = DelUnusedTestClasses()
  sys.argv[1 : 1] = ('-v',)  # Make verbose output in unittest.py.
  try:
    unittest.main()
  finally:
    print >>sys.stderr, 'info: skipped %d test class%s, see above' % (
        skip_count, 'es' * (skip_count != 1))
