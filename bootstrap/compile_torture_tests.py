#! /usr/bin/env python

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
import os
import os.path
import sys

import testing


def c_compile(infile, outfile, extras):
  """Create the command-line for a C compiler invocation."""
  return [extras['c'], infile, '-o', outfile] + extras['cflags']


def create_outname(outdir, infile, extras):
  basename = os.path.basename(infile)
  outname = basename + extras['suffix']
  return os.path.join(outdir, outname)


def run(c, cxx, testsuite, sysroot_dir, fails, out, config, opt):
  """Compile all torture tests."""
  cflags_common = ['--std=gnu89', '-DSTACK_SIZE=524288',
                   '-w', '-Wno-implicit-function-declaration', '-' + opt]
  cflags_extra = ['--target=wasm32-unknown-wavix', '-c',
                 '--sysroot=%s' % sysroot_dir]
  suffix = '.o'

  assert os.path.isfile(c), 'Cannot find C compiler at %s' % c
  assert os.path.isfile(cxx), 'Cannot find C++ compiler at %s' % cxx
  assert os.path.isdir(testsuite), 'Cannot find testsuite at %s' % testsuite
  # TODO(jfb) Also compile other C tests, as well as C++ tests under g++.dg.
  c_torture = os.path.join(testsuite, 'gcc.c-torture', 'execute')
  assert os.path.isdir(c_torture), ('Cannot find C torture tests at %s' %
                                    c_torture)
  assert os.path.isdir(out), 'Cannot find outdir %s' % out
  c_test_files = glob.glob(os.path.join(c_torture, '*c'))
  cflags = cflags_common + cflags_extra

  result = testing.execute(
      tester=testing.Tester(
          command_ctor=c_compile,
          outname_ctor=create_outname,
          outdir=out,
          extras={'c': c, 'cflags': cflags, 'suffix': suffix}),
      inputs=c_test_files,
      fails=fails,
      attributes=[config, opt])

  return result


def main():
  parser = argparse.ArgumentParser(description='Compile GCC torture tests.')
  parser.add_argument('--c', type=str, required=True,
                      help='C compiler path')
  parser.add_argument('--cxx', type=str, required=True,
                      help='C++ compiler path')
  parser.add_argument('--testsuite', type=str, required=True,
                      help='GCC testsuite tests path')
  parser.add_argument('--sysroot', type=str, required=True,
                      help='Sysroot directory')
  parser.add_argument('--fails', type=str, required=True,
                      help='Expected failures')
  parser.add_argument('--out', type=str, required=True,
                      help='Output directory')
  parser.add_argument('--config', type=str, required=True,
                      help='configuration to use')
  args = parser.parse_args()
  return run(c=args.c,
             cxx=args.cxx,
             testsuite=args.testsuite,
             sysroot_dir=args.sysroot,
             fails=args.fails,
             out=args.out,
             config=args.config)


if __name__ == '__main__':
  sys.exit(main())
