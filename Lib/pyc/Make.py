#!/usr/bin/env python

import compileall
import sys

compileall.compile_dir ('.')

if len (sys.argv) > 1:
  print sys.argv [1]

