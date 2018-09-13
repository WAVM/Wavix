#! /usr/bin/env python

#   Copyright 2016 WebAssembly Community Group participants
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
import itertools
import multiprocessing
import os
import shutil
import subprocess
import sys
import tempfile


verbose = False

DIR_BLACKLIST = ['ldso']
BLACKLIST = [
#    '__init_tls.c',
]
CFLAGS = ['-std=c99',
          '-D_XOPEN_SOURCE=700',
          '-Werror',
          '-Wno-empty-body',
          '-Wno-incompatible-library-redeclaration',
          '-Wno-shift-op-parentheses',
          '-Wno-tautological-unsigned-zero-compare',
          '-Wno-tautological-constant-out-of-range-compare',
          '-Wno-tautological-unsigned-enum-zero-compare',
          '-Wno-ignored-attributes',
          '-Wno-format',
          '-Wno-bitwise-op-parentheses',
          '-Wno-logical-op-parentheses',
          '-Wno-string-plus-int',
          '-Wno-pointer-sign',
          '-Wno-dangling-else',
          '-Wno-absolute-value',
          '-Wno-parentheses',
          '-Wno-unknown-pragmas']


def check_output(cmd, **kwargs):
  cwd = kwargs.get('cwd', os.getcwd())
  if verbose:
    c = ' '.join('"' + c + '"' if ' ' in c else c for c in cmd)
    print '  `%s`, cwd=`%s`' % (c, cwd)
  return subprocess.check_output(cmd, cwd=cwd)


def change_extension(path, new_extension):
  return path[:path.rfind('.')] + new_extension


def create_version(musl, outdir):
  """musl's Makefile creates version.h"""
  script = os.path.join('tools', 'version.sh').replace('\\', '/')
  version = check_output(['bash', script], cwd=musl).strip()
  outroot = os.path.join(outdir, 'src', 'internal')
  if not os.path.exists(outroot):
    os.makedirs(outroot)
  with open(os.path.join(outroot, 'version.h'), 'w') as v:
    v.write('#define VERSION "%s"\n' % version)


def build_headers(musl, arch, outdir):
  """Emulate musl's Makefile build of alltypes.h and syscall.h"""
  outroot = os.path.join(outdir, 'include', 'bits')
  if not os.path.exists(outroot):
    os.makedirs(outroot)
  mkalltypes = os.path.join('tools', 'mkalltypes.sed').replace('\\', '/')
  inbits = os.path.join('arch', arch, 'bits', 'alltypes.h.in').replace('\\', '/')
  intypes = os.path.join('include', 'alltypes.h.in').replace('\\', '/')
  out = check_output(['bash', '-c', 'sed -f %s %s %s' % (mkalltypes, inbits, intypes)], cwd=musl)
  with open(os.path.join(outroot, 'alltypes.h'), 'w') as o:
    o.write(out)

  insyscall = os.path.join('arch', arch, 'bits', 'syscall.h.in')
  out = check_output(['bash', '-c', 'sed -n -e s/__NR_/SYS_/p %s' % insyscall.replace('\\', '/')], cwd=musl)
  with open(os.path.join(outroot, 'syscall.h'), 'w') as o:
    o.write(open(os.path.join(musl,insyscall)).read())
    o.write(out)


def musl_sources(musl_root):
  """musl sources to be built."""
  sources = []
  for d in os.listdir(os.path.join(musl_root, 'src')):
    if d in DIR_BLACKLIST:
      continue
    base = os.path.join(musl_root, 'src', d)
    pattern = os.path.join(base, '*.c')
    for f in glob.glob(pattern):
      if os.path.basename(f) in BLACKLIST:
        continue
      sources.append(os.path.join(d, os.path.basename(f)))
  return sorted(sources)


def includes(musl, arch, outdir):
  """Include path."""
  includes = [
              os.path.join(musl, 'arch', arch),
              os.path.join(musl, 'arch', 'generic'),
              os.path.join(outdir, 'src', 'internal'),
              os.path.join(musl, 'src', 'internal'),
              os.path.join(outdir, 'include'),
              os.path.join(musl, 'include')]
  return list(itertools.chain(*zip(['-I'] * len(includes), includes)))


class Compiler(object):
  """Compile source files."""
  def __init__(self, out, clang_dir, musl, arch, tmpdir):
    self.out = out
    self.outbase = os.path.basename(self.out)
    self.clang_dir = clang_dir
    self.musl = musl
    self.arch = arch
    self.tmpdir = tmpdir
    self.compiled = []

  def compile(self, sources):
    for source in sources:
      obj_dir = os.path.join(self.tmpdir, os.path.dirname(source))
      if not os.path.isdir(obj_dir):
        os.mkdir(obj_dir)

    if verbose:
      self.compiled = sorted([self(source) for source in sources])
    else:
      pool = multiprocessing.Pool()
      self.compiled = sorted(pool.map(self, sources))
      pool.close()
      pool.join()


class ObjCompiler(Compiler):
  def __init__(self, out, clang_dir, musl, arch, tmpdir):
    super(ObjCompiler, self).__init__(out, clang_dir, musl, arch, tmpdir)

  def __call__(self, src):
    target = 'wasm32-unknown-wavix'
    compile_cmd = [os.path.join(self.clang_dir, 'clang'), '-target', target,
                   '-Os', '-c', '-nostdinc']
    compile_cmd += includes(self.musl, self.arch, self.tmpdir)
    compile_cmd += CFLAGS
    obj = src[:-1] + 'o' # .c -> .o
    check_output(compile_cmd + [os.path.join(self.musl, 'src', src), '-o', obj], cwd=self.tmpdir)
    return obj

  def binary(self):
    if os.path.exists(self.out):
      os.remove(self.out)
    check_output([os.path.join(self.clang_dir, 'llvm-ar'), 'rcs', self.out] + self.compiled,
                  cwd=self.tmpdir)


def run(clang_dir, musl, arch, out):
  objdir = os.path.join(os.path.dirname(out), 'obj')
  if os.path.isdir(objdir):
    shutil.rmtree(objdir)
  os.mkdir(objdir)

  create_version(musl, objdir)
  build_headers(musl, arch, objdir)
  sources = musl_sources(musl)
  compiler = ObjCompiler(out, clang_dir, musl, arch, objdir)
  compiler.compile(sources)
  compiler.binary()
  compiler.compile([os.path.join(musl, 'crt', 'crt1.c')])
  shutil.copy(os.path.join(objdir, compiler.compiled[0]),
              os.path.dirname(out))


def getargs():
  parser = argparse.ArgumentParser(description='Build a hacky wasm libc.')
  parser.add_argument('--clang_dir', type=str, required=True,
                      help='Clang binary directory')
  parser.add_argument('--musl', type=str, required=True,
                      help='musl libc root directory')
  parser.add_argument('--arch', type=str, default='wasm32',
                      help='architecture to target')
  parser.add_argument('--out', '-o', type=str,
                      default=os.path.join(os.getcwd(), 'musl.wast'),
                      help='Output file')
  parser.add_argument('--verbose', default=False, action='store_true',
                      help='Verbose')
  return parser.parse_args()


def main():
  global verbose
  args = getargs()
  if args.verbose:
    verbose = True
  return run(args.clang_dir, args.musl, args.arch, args.out)


if __name__ == '__main__':
  sys.exit(main())
