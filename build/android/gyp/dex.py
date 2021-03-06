#!/usr/bin/env python
#
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import optparse
import os
import sys

from util import build_utils
from util import md5_check


def DoDex(options, paths):
  dx_binary = os.path.join(options.android_sdk_tools, 'dx')
  # See http://crbug.com/272064 for context on --force-jumbo.
  # --num-threads=10 made final dexing go from 10s -> 5s on a z620.
  dex_cmd = [dx_binary, '--num-threads=10', '--dex', '--force-jumbo',
             '--output', options.dex_path]
  if options.no_locals != '0':
    dex_cmd.append('--no-locals')

  dex_cmd += paths

  record_path = '%s.md5.stamp' % options.dex_path
  md5_check.CallAndRecordIfStale(
      lambda: build_utils.CheckOutput(dex_cmd, print_stderr=False),
      record_path=record_path,
      input_paths=paths,
      input_strings=dex_cmd,
      force=not os.path.exists(options.dex_path))
  build_utils.WriteJson(paths, options.dex_path + '.inputs')


def main():
  args = build_utils.ExpandFileArgs(sys.argv[1:])

  parser = optparse.OptionParser()
  build_utils.AddDepfileOption(parser)

  parser.add_option('--android-sdk-tools',
                    help='Android sdk build tools directory.')
  parser.add_option('--dex-path', help='Dex output path.')
  parser.add_option('--configuration-name',
                    help='The build CONFIGURATION_NAME.')
  parser.add_option('--proguard-enabled',
                    help='"true" if proguard is enabled.')
  parser.add_option('--proguard-enabled-input-path',
                    help=('Path to dex in Release mode when proguard '
                          'is enabled.'))
  parser.add_option('--no-locals',
                    help='Exclude locals list from the dex file.')
  parser.add_option('--inputs', help='A list of additional input paths.')
  parser.add_option('--excluded-paths',
                    help='A list of paths to exclude from the dex file.')

  options, paths = parser.parse_args(args)

  required_options = ('android_sdk_tools',)
  build_utils.CheckOptions(options, parser, required=required_options)

  if (options.proguard_enabled == 'true'
      and options.configuration_name == 'Release'):
    paths = [options.proguard_enabled_input_path]

  if options.inputs:
    paths += build_utils.ParseGypList(options.inputs)

  if options.excluded_paths:
    exclude_paths = build_utils.ParseGypList(options.excluded_paths)
    paths = [p for p in paths if not p in exclude_paths]

  DoDex(options, paths)

  if options.depfile:
    build_utils.WriteDepfile(
        options.depfile,
        paths + build_utils.GetPythonDependencies())



if __name__ == '__main__':
  sys.exit(main())
