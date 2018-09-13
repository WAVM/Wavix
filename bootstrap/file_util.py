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

# Shell utilities

import errno
import os
import shutil
import sys

import proc


def Chdir(path):
  print 'Change directory to: %s' % path
  os.chdir(path)


def Mkdir(path):
  """Create a directory at a specified path.

  Creates all intermediate directories along the way.
  e.g.: Mkdir('a/b/c') when 'a/' is an empty directory will
        cause the creation of directories 'a/b/' and 'a/b/c/'.

  If the path already exists (and is already a directory), this does nothing.
  """
  try:
    os.makedirs(path)
  except OSError as e:
    if not os.path.isdir(path):
      raise Exception('Path %s is not a directory!' % path)
    if not e.errno == errno.EEXIST:
      raise e


def Remove(path):
  """Remove file or directory if it exists, do nothing otherwise."""
  if not os.path.exists(path):
    return
  print 'Removing %s' % path
  if not os.path.isdir(path):
    os.remove(path)
    return
  if sys.platform == 'win32':
    # shutil.rmtree() may not work in Windows if a directory contains read-only
    # files.
    proc.check_call('rmdir /S /Q "%s"' % path, shell=True)
  else:
    shutil.rmtree(path)


def CopyTree(src, dst):
  """Recursively copy the items in the src directory to the dst directory.

  Unlike shutil.copytree, the destination directory and any subdirectories and
  files may exist. Existing directories are left untouched, and existing files
  are removed and copied from the source using shutil.copy2. It is also not
  symlink-aware.

  Args:
    src: Source. Must be an existing directory.
    dst: Destination directory. If it exists, must be a directory. Otherwise it
         will be created, along with parent directories.
  """
  print 'Copying directory %s to %s' % (src, dst)
  if not os.path.isdir(dst):
    os.makedirs(dst)
  for root, dirs, files in os.walk(src):
    relroot = os.path.relpath(root, src)
    dstroot = os.path.join(dst, relroot)
    for d in dirs:
      dstdir = os.path.join(dstroot, d)
      if not os.path.isdir(dstdir):
        os.mkdir(dstdir)
    for f in files:
      dstfile = os.path.join(dstroot, f)
      if os.path.isfile(dstfile):
        os.remove(dstfile)
      shutil.copy2(os.path.join(root, f), dstfile)
