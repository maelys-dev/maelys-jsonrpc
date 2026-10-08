#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
python3 - <<'PY'
from pathlib import Path
import re
version = Path('VERSION').read_text().strip()
if not re.fullmatch(r'\d+\.\d+\.\d+', version):
    raise SystemExit('invalid VERSION')
path = Path('include/maelys/jsonrpc/version.h')
text = path.read_text()
for name, value in zip(('MAJOR','MINOR','PATCH','STRING'), (*version.split('.'), '"'+version+'"')):
    text, count = re.subn(r'(#define MAELYS_JSONRPC_VERSION_'+name+r' )[^\n]+',
                         lambda match: match[1]+value, text)
    if count != 1:
        raise SystemExit('missing or duplicate version macro')
path.write_text(text)
PY
