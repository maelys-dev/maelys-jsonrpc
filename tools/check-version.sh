#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
python3 - <<'PY'
from pathlib import Path
import re
version = Path('VERSION').read_text().strip()
header = Path('include/maelys/jsonrpc/version.h').read_text()
for name, value in zip(('MAJOR','MINOR','PATCH','STRING'), (*version.split('.'), '"'+version+'"')):
    if not re.search(r'^#define MAELYS_JSONRPC_VERSION_'+name+' '+re.escape(value)+r'$', header, re.M):
        raise SystemExit('version mismatch: '+name)
print('check-version: OK')
PY
