#! /usr/bin/env python
# -*- coding: utf-8 -*-

#   Copyright 2015 WebAssembly Community Group participants
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.

import argparse
import glob
import json
import multiprocessing
import os
import shutil
import sys
import textwrap
import time
import traceback

import buildbot
import compile_torture_tests
import execute_files
from file_util import Chdir, CopyTree, Mkdir, Remove
import link_assembly_files
import proc


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
WAVIX_SRC_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, '..'))
BUILD_DIR = os.path.abspath(os.getcwd())

LLVM_SRC_DIR = os.path.join(WAVIX_SRC_DIR, 'llvm')
CLANG_SRC_DIR = os.path.join(WAVIX_SRC_DIR, 'clang')
LLD_SRC_DIR = os.path.join(WAVIX_SRC_DIR, 'lld')
LIBCXX_SRC_DIR = os.path.join(WAVIX_SRC_DIR, 'libcxx')
LIBCXXABI_SRC_DIR = os.path.join(WAVIX_SRC_DIR, 'libcxxabi')
COMPILER_RT_SRC_DIR = os.path.join(WAVIX_SRC_DIR, 'compiler-rt')
LLVM_TEST_SUITE_SRC_DIR = os.path.join(WAVIX_SRC_DIR, 'llvm-test-suite')
GCC_TEST_DIR = os.path.join(WAVIX_SRC_DIR,'gcc-test-suite')

MUSL_SRC_DIR = os.path.join(WAVIX_SRC_DIR, 'musl')
BASH_SRC_DIR = os.path.join(WAVIX_SRC_DIR, 'bash')
COREUTILS_SRC_DIR = os.path.join(WAVIX_SRC_DIR, 'coreutils')
WAVM_SRC_DIR = os.path.join(WAVIX_SRC_DIR, 'WAVM')
WAVIX_HOST_SRC_DIR = os.path.join(WAVIX_SRC_DIR, 'Wavix')

HOST_LLVM_OUT_DIR = os.path.join(BUILD_DIR, 'host-llvm')
HOST_WAVM_OUT_DIR = os.path.join(BUILD_DIR, 'host-WAVM')
WAVIX_OUT_DIR = os.path.join(BUILD_DIR, 'wavix')
WAVM_OUT_DIR = os.path.join(BUILD_DIR, 'WAVM')
MUSL_OUT_DIR = os.path.join(BUILD_DIR, 'musl')
BASH_OUT_DIR = os.path.join(BUILD_DIR, 'bash')
COREUTILS_OUT_DIR = os.path.join(BUILD_DIR, 'coreutils')
COMPILER_RT_OUT_DIR = os.path.join(BUILD_DIR, 'compiler-rt')
LIBCXXABI_OUT_DIR = os.path.join(BUILD_DIR, 'libcxxabi')
LIBCXX_OUT_DIR = os.path.join(BUILD_DIR, 'libcxx')
TORTURE_O_OUT_DIR = os.path.join(BUILD_DIR, 'torture-o')

HELLOWORLD_SRC_DIR = os.path.join(WAVIX_SRC_DIR, 'helloworld')
HELLOWORLD_OUT_DIR = os.path.join(BUILD_DIR, 'helloworld')

HOST_DIR = os.path.join(BUILD_DIR, 'host')
HOST_BIN = os.path.join(HOST_DIR, 'bin')

SYSROOT_DIR = os.path.join(BUILD_DIR, 'sys')
SYSROOT_LIB = os.path.join(SYSROOT_DIR, 'lib')

CMAKE_BIN = 'cmake'
NINJA_BIN = 'ninja'
CMAKE_GENERATOR = 'Ninja'

def AllBuilds():
  return [
      # Host tools
      Build('host-llvm', HostLLVM),
      Build('host-WAVM', HostWAVM),
      Build('wavix', Wavix),
      Build('Toolchain', Toolchain),
      # Target libs
      Build('musl', Musl),
      Build('compiler-rt', CompilerRT),
      Build('libcxxabi', LibCXXABI),
      Build('libcxx', LibCXX),
      Build('bash', Bash),
      Build('coreutils', CoreUtils),
      Build('HelloWorld', HelloWorld),
      Build('WAVM', WAVM),
  ]

def AllTests():
  return [
    Test('bare', TestBare),
  ]


def IsWindows():
  return sys.platform == 'win32'


def IsLinux():
  return sys.platform == 'linux2'


def IsMac():
  return sys.platform == 'darwin'


def Executable(name, extension='.exe'):
  return name + extension if IsWindows() else name


def WindowsFSEscape(path):
  #return os.path.normpath(path).replace('\\', '/')
  return path

def CMakePlatformName():
  return {'linux2': 'Linux',
          'darwin': 'Darwin',
          'win32': 'win32'}[sys.platform]

# Known failures.
IT_IS_KNOWN = 'known_gcc_test_failures.txt'
LLVM_KNOWN_TORTURE_FAILURES = [os.path.join(LLVM_SRC_DIR, 'lib', 'Target',
                                            'WebAssembly', IT_IS_KNOWN)]

RUN_KNOWN_TORTURE_FAILURES = [os.path.join(SCRIPT_DIR, 'test',
                                           'run_' + IT_IS_KNOWN)]
LLD_KNOWN_TORTURE_FAILURES = [os.path.join(SCRIPT_DIR, 'test',
                              'lld_' + IT_IS_KNOWN)]

# Optimization levels
TEST_OPT_FLAGS = ['O0', 'O2']


NPROC = multiprocessing.cpu_count()

def CopyLibraryToSysroot(sysroot_dir,library):
  """All libraries are archived in the same tar file."""
  sysroot_lib = os.path.join(sysroot_dir, 'lib')
  print 'Copying library %s to archive %s' % (library, sysroot_lib)
  Mkdir(sysroot_lib)
  shutil.copy2(library, sysroot_lib)

def Clobber():
  buildbot.Step('Clobbering work dir')
  #if os.path.isdir(BUILD_DIR):
  #  Remove(BUILD_DIR)
  Remove(HOST_DIR)
  Remove(SYSROOT_DIR)
  

# Build rules

def HostLLVM():
  buildbot.Step('host-llvm')
  Mkdir(HOST_LLVM_OUT_DIR)
  build_dylib = 'ON' if not IsWindows() else 'OFF'
  command = [CMAKE_BIN, '-G', CMAKE_GENERATOR, LLVM_SRC_DIR,
             '-DCMAKE_EXPORT_COMPILE_COMMANDS=YES',
             '-DLLVM_EXTERNAL_CLANG_SOURCE_DIR=' + CLANG_SRC_DIR,
             '-DLLVM_EXTERNAL_LLD_SOURCE_DIR=' + LLD_SRC_DIR,
             '-DLLVM_TOOL_CLANG_BUILD=ON',
             '-DLLVM_TOOL_LLD_BUILD=ON',
             '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
             '-DLLVM_BUILD_TESTS=OFF',
             '-DCMAKE_BUILD_TYPE=Release', #'-DCMAKE_BUILD_TYPE=RelWithDebInfo',
             '-DCMAKE_INSTALL_PREFIX=' + HOST_DIR,
             '-DCLANG_INCLUDE_TESTS=OFF',
             '-DCLANG_INCLUDE_DOCS=OFF',
             '-DLLVM_INCLUDE_EXAMPLES=OFF',
             '-DLLVM_INCLUDE_DOCS=OFF',
             '-DLLVM_INCLUDE_GO_TESTS=OFF',
             '-DLLVM_INCLUDE_TESTS=OFF',
             '-DLLVM_BUILD_LLVM_DYLIB=%s' % build_dylib,
             '-DLLVM_LINK_LLVM_DYLIB=%s' % build_dylib,
             # Our mac bot's toolchain's ld64 is too old for trunk libLTO.
             '-DLLVM_TOOL_LTO_BUILD=OFF',
             '-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=WebAssembly',
             '-DLLVM_TARGETS_TO_BUILD=X86']

  proc.check_call(command, cwd=HOST_LLVM_OUT_DIR)
  proc.check_call([NINJA_BIN, 'install'], cwd=HOST_LLVM_OUT_DIR)

def HostWAVM():
  buildbot.Step('HostWAVM')
  Mkdir(HOST_WAVM_OUT_DIR)
  command = [CMAKE_BIN, '-G', CMAKE_GENERATOR, WAVM_SRC_DIR, 
             '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
             '-DCMAKE_EXPORT_COMPILE_COMMANDS=YES',
             '-DCMAKE_INSTALL_PREFIX=' + HOST_DIR,
             '-DLLVM_DIR=' + os.path.join(HOST_DIR, 'lib/cmake/llvm') ]

  proc.check_call(command, cwd=HOST_WAVM_OUT_DIR)
  proc.check_call([NINJA_BIN, 'install'], cwd=HOST_WAVM_OUT_DIR)

def Wavix():
  buildbot.Step('Wavix')
  Mkdir(WAVIX_OUT_DIR)
  command = [CMAKE_BIN, '-G', CMAKE_GENERATOR, WAVIX_HOST_SRC_DIR, 
             '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
             '-DCMAKE_EXPORT_COMPILE_COMMANDS=YES',
             '-DCMAKE_INSTALL_PREFIX=' + HOST_DIR,
             '-DWAVM_DIR=' + os.path.join(HOST_DIR, 'lib/cmake/WAVM') ]

  proc.check_call(command, cwd=WAVIX_OUT_DIR)
  proc.check_call([NINJA_BIN, 'install'], cwd=WAVIX_OUT_DIR)

def Toolchain():
  buildbot.Step('Toolchain')
  Mkdir(HOST_DIR)
  Mkdir(os.path.join(HOST_DIR, 'cmake', 'Modules', 'Platform'))
  shutil.copy2(os.path.join(SCRIPT_DIR, 'cmake', 'Modules', 'Platform', 'Wavix.cmake'),
               os.path.join(HOST_DIR,   'cmake', 'Modules', 'Platform'))
  shutil.copy2(os.path.join(SCRIPT_DIR, 'wavix_toolchain.cmake'),
               HOST_DIR)

def WAVM():
  buildbot.Step('WAVM')
  Mkdir(WAVM_OUT_DIR)
  command = [CMAKE_BIN, '-G', CMAKE_GENERATOR, WAVM_SRC_DIR, 
             '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
             '-DCMAKE_EXPORT_COMPILE_COMMANDS=YES',
             '-DCMAKE_INSTALL_PREFIX=' + SYSROOT_DIR,
             '-DCMAKE_TOOLCHAIN_FILE=' +
             WindowsFSEscape(os.path.join(HOST_DIR, 'wavix_toolchain.cmake')),
             '-DWAVM_ENABLE_RUNTIME=OFF',
             '-DWAVM_ENABLE_STATIC_LINKING=ON',
             '-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON' ]

  proc.check_call(command, cwd=WAVM_OUT_DIR)
  proc.check_call([NINJA_BIN, 'install'], cwd=WAVM_OUT_DIR)

def CompilerRT():
  # TODO(sbc): Figure out how to do this step as part of the llvm build.
  # I suspect that this can be done using the llvm/runtimes directory but
  # have yet to make it actually work this way.
  buildbot.Step('compiler-rt')

  # TODO(sbc): Remove this.
  # The compiler-rt doesn't currently rebuild libraries when a new -DCMAKE_AR
  # value is specified.
  if os.path.isdir(COMPILER_RT_OUT_DIR):
    Remove(COMPILER_RT_OUT_DIR)

  Mkdir(COMPILER_RT_OUT_DIR)
  command = [CMAKE_BIN, '-G', CMAKE_GENERATOR,
             WindowsFSEscape(os.path.join(COMPILER_RT_SRC_DIR, 'lib', 'builtins')),
             # wasm-clang can't compile compiler-rt in release yet:
             # Assertion failed: VT.isFloatingPoint(), file C:\Dropbox\Development\wavix\llvm\include\llvm/CodeGen/TargetLowering.h, line 2159
             # looks like a problem with how WASM lowers long doubles?
             #'-DCMAKE_BUILD_TYPE=RelWithDebInfo',
             '-DCMAKE_BUILD_TYPE=Debug',
             '-DCMAKE_TOOLCHAIN_FILE=' +
             WindowsFSEscape(os.path.join(HOST_DIR, 'wavix_toolchain.cmake')),
             '-DCMAKE_EXPORT_COMPILE_COMMANDS=YES',
             '-DCMAKE_C_COMPILER_WORKS=ON',
             '-DCOMPILER_RT_BAREMETAL_BUILD=On',
             '-DCOMPILER_RT_BUILD_XRAY=OFF',
             '-DCOMPILER_RT_INCLUDE_TESTS=OFF',
             '-DCOMPILER_RT_ENABLE_IOS=OFF',
             '-DCOMPILER_RT_DEFAULT_TARGET_ONLY=On',
             '-DLLVM_CONFIG_PATH=' +
             WindowsFSEscape(os.path.join(HOST_DIR, 'bin', 'llvm-config')),
             '-DCOMPILER_RT_OS_DIR=wavix',
             '-DCMAKE_INSTALL_PREFIX=' +
             WindowsFSEscape(os.path.join(HOST_DIR, 'lib', 'clang', '8.0.0'))]

  proc.check_call(command, cwd=COMPILER_RT_OUT_DIR)
  proc.check_call([NINJA_BIN, 'install'], cwd=COMPILER_RT_OUT_DIR)

def HelloWorld():
  buildbot.Step('helloworld')

  # TODO(sbc): Remove this.
  # The compiler-rt doesn't currently rebuild libraries when a new -DCMAKE_AR
  # value is specified.
  if os.path.isdir(HELLOWORLD_OUT_DIR):
    Remove(HELLOWORLD_OUT_DIR)

  Mkdir(HELLOWORLD_OUT_DIR)
  command = [CMAKE_BIN, '-G', CMAKE_GENERATOR,
             HELLOWORLD_SRC_DIR,
             '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
             '-DCMAKE_TOOLCHAIN_FILE=' +
             WindowsFSEscape(os.path.join(HOST_DIR, 'wavix_toolchain.cmake')),
             '-DCMAKE_EXPORT_COMPILE_COMMANDS=YES',
             '-DCMAKE_INSTALL_PREFIX=' + WindowsFSEscape(SYSROOT_DIR)]

  proc.check_call(command, cwd=HELLOWORLD_OUT_DIR)
  proc.check_call([NINJA_BIN, 'install'], cwd=HELLOWORLD_OUT_DIR)

def LibCXXABI():
  buildbot.Step('libcxxabi')

  # TODO(sbc): Remove this.
  # The compiler-rt doesn't currently rebuild libraries when a new -DCMAKE_AR
  # value is specified.
  if os.path.isdir(LIBCXXABI_OUT_DIR):
    Remove(LIBCXXABI_OUT_DIR)

  Mkdir(LIBCXXABI_OUT_DIR)
  command = [CMAKE_BIN, '-G', CMAKE_GENERATOR,
             LIBCXXABI_SRC_DIR,
             '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
             '-DCMAKE_TOOLCHAIN_FILE=' +
             WindowsFSEscape(os.path.join(HOST_DIR, 'wavix_toolchain.cmake')),
             '-DCMAKE_EXPORT_COMPILE_COMMANDS=YES',
             '-DLIBCXXABI_LIBCXX_PATH=' + LIBCXX_SRC_DIR,
             '-DLIBCXXABI_LIBCXX_INCLUDES=' + os.path.join(LIBCXX_SRC_DIR, 'include'),
             '-DLIBCXXABI_ENABLE_STATIC=ON',
             '-DLIBCXXABI_ENABLE_SHARED=OFF',
             '-DLIBCXXABI_ENABLE_THREADS=ON',
             '-DLIBCXXABI_SYSROOT=' + SYSROOT_DIR,
             '-DLIBCXXABI_USE_COMPILER_RT=ON',
             '-DCMAKE_INSTALL_PREFIX=' + WindowsFSEscape(SYSROOT_DIR)]

  proc.check_call(command, cwd=LIBCXXABI_OUT_DIR)
  proc.check_call([NINJA_BIN, 'install'], cwd=LIBCXXABI_OUT_DIR)

  CopyLibraryToSysroot(SYSROOT_DIR,os.path.join(LIBCXXABI_SRC_DIR, 'libc++abi.imports'))

def LibCXX():
  buildbot.Step('libcxx')

  # TODO(sbc): Remove this.
  # The compiler-rt doesn't currently rebuild libraries when a new -DCMAKE_AR
  # value is specified.
  if os.path.isdir(LIBCXX_OUT_DIR):
    Remove(LIBCXX_OUT_DIR)

  Mkdir(LIBCXX_OUT_DIR)
  command = [CMAKE_BIN, '-G', CMAKE_GENERATOR,
             LIBCXX_SRC_DIR,
             '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
             '-DCMAKE_TOOLCHAIN_FILE=' +
             WindowsFSEscape(os.path.join(HOST_DIR, 'wavix_toolchain.cmake')),
             '-DCMAKE_EXPORT_COMPILE_COMMANDS=YES',
             # Make HandleLLVMOptions.cmake (it can't check for c++11 support
             # because no C++ programs can be linked until libc++abi is
             # installed, so chicken and egg.
             '-DCXX_SUPPORTS_CXX11=ON',
             # HandleLLVMOptions.cmake include CheckCompilerVersion.cmake.
             # This checks for working <atomic> header, which in turn errors
             # out on systems with threads disabled
             '-DLLVM_COMPILER_CHECKED=ON',
             '-DLIBCXX_CXX_ABI=libcxxabi',
             '-DLIBCXX_CXX_ABI_INCLUDE_PATHS=' + WindowsFSEscape(os.path.join(LIBCXXABI_SRC_DIR, 'include')),
             '-DLIBCXX_CXX_ABI_LIBRARY_PATH=' + WindowsFSEscape(os.path.join(SYSROOT_DIR, 'lib')),
             '-DLLVM_PATH=' + LLVM_SRC_DIR,
             '-DLIBCXX_ENABLE_STATIC=ON',
             '-DLIBCXX_ENABLE_SHARED=OFF',
             '-DLIBCXX_HAS_MUSL_LIBC=ON',
             '-DLIBCXX_ENABLE_THREADS=ON',
             '-DLIBCXX_INCLUDE_BENCHMARKS=OFF',
             '-DCMAKE_INSTALL_PREFIX=' + WindowsFSEscape(SYSROOT_DIR)]

  proc.check_call(command, cwd=LIBCXX_OUT_DIR)
  proc.check_call([NINJA_BIN, 'install'], cwd=LIBCXX_OUT_DIR)

def Musl():
  buildbot.Step('musl')
  Mkdir(MUSL_OUT_DIR)
  path = os.environ['PATH']
  try:
    # Build musl directly to wasm object files in an ar library
    proc.check_call([
        os.path.join(MUSL_SRC_DIR, 'libc.py'),
        '--clang_dir', HOST_BIN,
        '--out', os.path.join(MUSL_OUT_DIR, 'libc.a'),
        '--musl', MUSL_SRC_DIR])
    CopyLibraryToSysroot(SYSROOT_DIR,os.path.join(MUSL_OUT_DIR, 'libc.a'))
    CopyLibraryToSysroot(SYSROOT_DIR,os.path.join(MUSL_OUT_DIR, 'crt1.o'))
    CopyLibraryToSysroot(SYSROOT_DIR,os.path.join(MUSL_SRC_DIR, 'arch', 'wasm32',
                                      'libc.imports'))

    CopyTree(os.path.join(MUSL_SRC_DIR, 'include'),
             os.path.join(SYSROOT_DIR, 'include'))
    CopyTree(os.path.join(MUSL_SRC_DIR, 'arch', 'generic'),
             os.path.join(SYSROOT_DIR, 'include'))
    CopyTree(os.path.join(MUSL_SRC_DIR, 'arch', 'wasm32'),
             os.path.join(SYSROOT_DIR, 'include'))
    CopyTree(os.path.join(MUSL_OUT_DIR, 'obj', 'include'),
             os.path.join(SYSROOT_DIR, 'include'))

  except proc.CalledProcessError:
    # Note the failure but allow the build to continue.
    buildbot.Fail()
  finally:
    os.environ['PATH'] = path

def Bash():
  buildbot.Step('bash')
  Mkdir(BASH_OUT_DIR)

  # Build bash 
  proc.check_call([
      'bash',
      WAVIX_SRC_DIR.replace('\\', '/').replace('C:','/mnt/c') + '/bash/build-wavix-bash.sh',
      WAVIX_SRC_DIR.replace('\\', '/').replace('C:','/mnt/c'),
      BUILD_DIR.replace('\\', '/'),
      BUILD_DIR.replace('\\', '/').replace('C:','/mnt/c'),
      BUILD_DIR.replace('\\', '/').replace('C:','/mnt/c') + Executable('/host/bin/clang'),
      BUILD_DIR.replace('\\', '/').replace('C:','/mnt/c') + Executable('/host/bin/llvm-ar'),
      BUILD_DIR.replace('\\', '/').replace('C:','/mnt/c') + Executable('/host/bin/llvm-ranlib'),
      BUILD_DIR.replace('\\', '/').replace('C:','/mnt/c') + Executable('/host/bin/clang-cpp')
      ], cwd=BASH_OUT_DIR)

def CoreUtils():
  buildbot.Step('coreutils')
  Mkdir(COREUTILS_OUT_DIR)

  # Build bash 
  proc.check_call([
      'bash',
      WAVIX_SRC_DIR.replace('\\', '/').replace('C:','/mnt/c') + '/coreutils/build-wavix-coreutils.sh',
      WAVIX_SRC_DIR.replace('\\', '/').replace('C:','/mnt/c'),
      BUILD_DIR.replace('\\', '/'),
      BUILD_DIR.replace('\\', '/').replace('C:','/mnt/c'),
      BUILD_DIR.replace('\\', '/').replace('C:','/mnt/c') + Executable('/host/bin/clang'),
      BUILD_DIR.replace('\\', '/').replace('C:','/mnt/c') + Executable('/host/bin/llvm-ar'),
      BUILD_DIR.replace('\\', '/').replace('C:','/mnt/c') + Executable('/host/bin/llvm-ranlib'),
      BUILD_DIR.replace('\\', '/').replace('C:','/mnt/c') + Executable('/host/bin/clang-cpp')
      ], cwd=COREUTILS_OUT_DIR)


def CompileLLVMTorture(outdir, opt):
  name = 'Compile LLVM Torture (%s)' % (opt)
  buildbot.Step(name)
  c = Executable(os.path.join(HOST_BIN, 'clang'))
  cxx = Executable(os.path.join(HOST_BIN, 'clang++'))
  Remove(outdir)
  Mkdir(outdir)
  unexpected_result_count = compile_torture_tests.run(
      c=c, cxx=cxx, testsuite=GCC_TEST_DIR,
      sysroot_dir=SYSROOT_DIR,
      fails=LLVM_KNOWN_TORTURE_FAILURES,
      out=outdir,
      config='wasm-o',
      opt=opt)
  if 0 != unexpected_result_count:
    buildbot.Fail()


def LinkLLVMTorture(name, linker, fails, indir, outdir, extension,
                    opt, args=None):
  buildbot.Step('Link LLVM Torture (%s, %s)' % (name, opt))
  assert os.path.isfile(linker), 'Cannot find linker at %s' % linker
  Remove(outdir)
  Mkdir(outdir)
  input_pattern = os.path.join(indir, '*.' + extension)
  unexpected_result_count = link_assembly_files.run(
      linker=linker, files=input_pattern, fails=fails, attributes=[opt],
      out=outdir, args=args)
  if 0 != unexpected_result_count:
    buildbot.Fail()

def ExecuteLLVMTorture(name, runner, indir, fails, attributes, extension, opt,
                       outdir='', wasmjs='', extra_files=None,
                       warn_only=False):
  extra_files = [] if extra_files is None else extra_files

  buildbot.Step('Execute LLVM Torture (%s, %s)' % (name, opt))
  if not indir:
    print 'Step skipped: no input'
    buildbot.Warn()
    return None
  assert os.path.isfile(runner), 'Cannot find runner at %s' % runner
  files = os.path.join(indir, '*.%s' % extension)
  unexpected_result_count = execute_files.run(
      runner=runner,
      files=files,
      fails=fails,
      attributes=attributes + [opt],
      out=outdir,
      extra_args=['--sysroot', SYSROOT_DIR],
      exclude_files=[os.path.join(indir, '930529-1.c.o.wasm')],
      wasmjs=wasmjs,
      extra_files=extra_files)
  if 0 != unexpected_result_count:
    buildbot.FailUnless(lambda: warn_only)


class Build(object):
  def __init__(self, name_, runnable_,
               no_windows=False, no_linux=False,
               *args, **kwargs):
    self.name = name_
    self.runnable = runnable_
    self.args = args
    self.kwargs = kwargs
    # Almost all of these steps depend directly or indirectly on CMake.
    # Temporarily disable them.
    self.no_windows = no_windows
    self.no_linux = no_linux

  def Run(self):
    if IsWindows() and self.no_windows:
      print "Skipping %s: Doesn't work on windows" % self.runnable.__name__
      return
    if IsLinux() and self.no_linux:
      print "Skipping %s: Doesn't work on Linux" % self.runnable.__name__
      return
    self.runnable(*self.args, **self.kwargs)


def Summary(repos):
  buildbot.Step('Summary')
  info = {'repositories': repos}
  info_file = os.path.join(HOST_DIR, 'buildinfo.json')

  print 'Failed steps: %s.' % buildbot.Failed()
  for step in buildbot.FailedList():
    print '    %s' % step
  print 'Warned steps: %s.' % buildbot.Warned()
  for step in buildbot.WarnedList():
    print '    %s' % step

  if buildbot.Failed():
    buildbot.Fail()

def BuildRepos():
  for rule in AllBuilds():
    rule.Run()


class Test(object):
  def __init__(self, name_, runnable_, no_windows=False):
    self.name = name_
    self.runnable = runnable_
    self.no_windows = no_windows

  def Test(self):
    if IsWindows() and self.no_windows:
      print "Skipping %s: Doesn't work on windows" % self.runnable.__name__
      return
    self.runnable()


def GetTortureDir(name, opt):
  dirs = {
      'o': os.path.join(TORTURE_O_OUT_DIR, opt),
  }
  if name in dirs:
    return dirs[name]
  return os.path.join(BUILD_DIR, 'torture-' + name, opt)


def TestBare():
  # Compile
  for opt in TEST_OPT_FLAGS:
    CompileLLVMTorture(GetTortureDir('o', opt), opt)

  # Link/Assemble
  for opt in TEST_OPT_FLAGS:
    LinkLLVMTorture(
        name='lld',
        linker=Executable(os.path.join(HOST_BIN, 'clang')),
        fails=LLD_KNOWN_TORTURE_FAILURES,
        indir=GetTortureDir('o', opt),
        outdir=GetTortureDir('lld', opt),
        extension='o',
        opt=opt)

  # Execute
  for opt in TEST_OPT_FLAGS:
    ExecuteLLVMTorture(
        name='WAVM',
        runner=Executable(os.path.join(HOST_BIN, 'wavix')),
        indir=GetTortureDir('lld', opt),
        fails=RUN_KNOWN_TORTURE_FAILURES,
        attributes=['bare', 'lld'],
        extension='wasm',
        opt=opt)


def run():
  #Clobber()

  try:
    BuildRepos()
  except Exception:
    # If any exception reaches here, do not attempt to run the tests; just
    # log the error for buildbot and exit
    print "Exception thrown in build step."
    traceback.print_exc()
    buildbot.Fail()
    Summary({})
    return 1

  for t in AllTests():
    t.Test()

  # Keep the summary step last: it'll be marked as red if the return code is
  # non-zero. Individual steps are marked as red with buildbot.Fail().
  Summary({})
  return buildbot.Failed()


def main():
  start = time.time()

  try:
    ret = run()
    print 'Completed in {}s'.format(time.time() - start)
    return ret
  except:
    traceback.print_exc()
    # If an except is raised during one of the steps we still need to
    # print the @@@STEP_FAILURE@@@ annotation otherwise the annotator
    # makes the failed stap as green:
    # TODO(sbc): Remove this if the annotator is fixed: http://crbug.com/647357
    if buildbot.current_step:
      buildbot.Fail()
    return 1


if __name__ == '__main__':
  sys.exit(main())
