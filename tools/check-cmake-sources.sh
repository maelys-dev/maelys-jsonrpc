#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
python3 - "$@" <<'PY'
from pathlib import Path
import re, sys
sources = re.findall(r'^\s+(src/\w+\.c)\)?$', Path('CMakeLists.txt').read_text(), re.M)
if sorted(sources) != sorted(sys.argv[1:]):
    raise SystemExit('Make/CMake library source mismatch')
print('check-cmake-sources: OK')
PY
