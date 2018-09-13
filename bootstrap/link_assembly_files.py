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


def create_outname(outdir, infile, extras):
  """Create the output file's name."""
  basename = os.path.basename(infile)
  linker = os.path.splitext(os.path.basename(extras['linker']))[0]
  if linker == 'clang':
    outname = basename + '.wasm'
  else:
    outname = basename + '.wast'
  return os.path.join(outdir, outname)


def link(infile, outfile, extras):
  """Create the command-line for a linker invocation."""
  linker = extras['linker']
  build_root = os.path.dirname(os.path.dirname(os.path.dirname(linker)))
  sysroot_dir = os.path.join(build_root, 'sys')
  command = [linker, '--target=wasm32-unknown-wavix',
                '--sysroot=%s' % sysroot_dir, '-Wl,-zstack-size=1048576',
                '-o', outfile, infile]
  return command + extras['args']


def run(linker, files, fails, attributes, out, args):
  """Link all files."""
  assert os.path.isfile(linker), 'Cannot find linker at %s' % linker
  assert os.path.isdir(out), 'Cannot find outdir %s' % out
  input_files = glob.glob(files)
  assert len(input_files), 'No files found by %s' % files
  if not args:
    args = []
  return testing.execute(
      tester=testing.Tester(
          command_ctor=link,
          outname_ctor=create_outname,
          outdir=out,
          extras={'linker': linker, 'args': args}),
      inputs=input_files,
      fails=fails,
      attributes=attributes)


def main():
  parser = argparse.ArgumentParser(
      description='Link .s/.o files into .wast/.wasm.')
  parser.add_argument('--linker', type=str, required=True,
                      help='Linker path')
  parser.add_argument('--files', type=str, required=True,
                      help='Glob pattern for .s files')
  parser.add_argument('--fails', type=str, required=True,
                      help='Expected failures')
  parser.add_argument('--out', type=str, required=True,
                      help='Output directory')
  args = parser.parse_args()
  return run(args.linker, args.files, args.fails, args.out)


if __name__ == '__main__':
  sys.exit(main())
