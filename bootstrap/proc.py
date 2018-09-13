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

# This module is intended to be a drop-in replacement for the standard
# subprocess module, with the difference that it logs commands before it runs
# them. Everything not overriden should pass through to the subprocess module
# via the import trick below.

# Imports subprocess in its own namespace so we can always refer directly to
# its attributes.
import subprocess
import os
import sys
# Imports all of subprocess into the current namespace, effectively
# re-exporting everything.
from subprocess import * # flake8: noqa


def Which(filename, cwd, require_executable=True):
  if os.path.isabs(filename):
    return filename

  to_search = [cwd] + os.environ.get('PATH', '').split(os.pathsep)
  exe_suffixes = ['']
  if sys.platform == 'win32':
    exe_suffixes = ['.exe', '.bat'] + exe_suffixes
  for path in to_search:
    abs_path = os.path.abspath(os.path.join(path, filename))
    for suffix in exe_suffixes:
      full_path = abs_path + suffix
      if (os.path.isfile(full_path) and
          (not require_executable or os.access(full_path, os.X_OK))):
        return full_path
  raise Exception('File "%s" not found. (cwd=`%s`, PATH=`%s`' %
                  (filename, cwd, os.environ['PATH']))


def SpecialCases(cmd, cwd):
  exe = cmd[0]
  if exe.endswith('.py'):
    script = Which(exe, cwd, require_executable=False)
    return [sys.executable, script] + cmd[1:]
  if exe == 'git' or exe == 'gclient':
    return [Which(exe, cwd)] + cmd[1:]
  return cmd


def LogCall(funcname, cmd, cwd):
  if isinstance(cmd, str):
    c = cmd
  else:
    c = ' '.join('"' + c + '"' if ' ' in c else c for c in cmd)
  print '%s(`%s`, cwd=`%s`)' % (funcname, c, cwd)


# Now we can override any parts of subprocess we want, while leaving the rest.
def check_call(cmd, **kwargs):
  cwd = kwargs.get('cwd', os.getcwd())
  cmd = SpecialCases(cmd, cwd)
  LogCall('subprocess.check_call', cmd, cwd)
  sys.stdout.flush()
  try:
    subprocess.check_call(cmd, **kwargs)
  finally:
    sys.stdout.flush()


def check_output(cmd, **kwargs):
  cwd = kwargs.get('cwd', os.getcwd())
  cmd = SpecialCases(cmd, cwd)
  LogCall('subprocess.check_output', cmd, cwd)
  sys.stdout.flush()
  return subprocess.check_output(cmd, **kwargs)
