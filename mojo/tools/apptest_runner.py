#!/usr/bin/env python
# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A test runner for gtest application tests."""

import argparse
import logging
import sys

import devtools
devtools.add_lib_to_path()
from devtoolslib.android_shell import AndroidShell
from devtoolslib.linux_shell import LinuxShell
from devtoolslib.apptest_dart import run_dart_apptest
from devtoolslib.apptest_gtest import run_gtest_apptest

from mopy import gtest
from mopy.config import Config
from mopy.gn import ConfigForGNArgs, ParseGNConfig
from mopy.log import InitLogging
from mopy.paths import Paths


_logger = logging.getLogger()


def main():
  parser = argparse.ArgumentParser(description="A test runner for application "
                                               "tests.")

  parser.add_argument("--verbose", help="be verbose (multiple times for more)",
                      default=0, dest="verbose_count", action="count")
  parser.add_argument("test_list_file", type=file,
                      help="a file listing apptests to run")
  parser.add_argument("build_dir", type=str,
                      help="the build output directory")
  args = parser.parse_args()

  InitLogging(args.verbose_count)
  config = ConfigForGNArgs(ParseGNConfig(args.build_dir))

  _logger.debug("Test list file: %s", args.test_list_file)
  execution_globals = {"config": config}
  exec args.test_list_file in execution_globals
  test_list = execution_globals["tests"]
  _logger.debug("Test list: %s" % test_list)

  paths = Paths(config)
  extra_args = []
  if config.target_os == Config.OS_ANDROID:
    shell = AndroidShell(paths.adb_path)
    shell.InstallApk(paths.target_mojo_shell_path)
    extra_args.append(shell.SetUpLocalOrigin(paths.build_dir, fixed_port=False))
  else:
    shell = LinuxShell(paths.mojo_shell_path)

  gtest.set_color()

  exit_code = 0
  for test_dict in test_list:
    test = test_dict["test"]
    test_name = test_dict.get("name", test)
    test_type = test_dict.get("type", "gtest")
    test_args = test_dict.get("test-args", [])
    shell_args = test_dict.get("shell-args", []) + extra_args

    _logger.info("Will start: %s" % test_name)
    print "Running %s...." % test_name,
    sys.stdout.flush()

    if test_type == "dart":
      apptest_result = run_dart_apptest(shell, shell_args, test, test_args)
    elif test_type == "gtest":
      apptest_result = run_gtest_apptest(shell, shell_args, test, test_args,
                                         False)
    elif test_type == "gtest_isolated":
      apptest_result = run_gtest_apptest(shell, shell_args, test, test_args,
                                         True)
    else:
      apptest_result = False
      print "Unrecognized test type in %r" % test_dict

    if not apptest_result:
      exit_code = 1
    print "Succeeded" if apptest_result else "Failed"
    _logger.info("Completed: %s" % test_name)

  return exit_code


if __name__ == '__main__':
  sys.exit(main())
