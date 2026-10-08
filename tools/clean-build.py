#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
import pathlib
import shutil
import sys

root = pathlib.Path.cwd().resolve()
for suffix in ("", "-asan", "-ubsan", "-asan-ubsan", "-coverage"):
    path = pathlib.Path(sys.argv[1] + suffix).resolve()
    if not path.is_relative_to(root) or path == root or not path.name.startswith("build"):
        raise SystemExit("clean only removes build* directories inside this repository")
    if path.exists():
        shutil.rmtree(path)
