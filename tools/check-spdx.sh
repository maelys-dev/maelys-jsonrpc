#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
python3 - <<'PY'
from pathlib import Path
for root in ('src','include','tests','fuzz','tools'):
    for path in Path(root).rglob('*'):
        if path.suffix in ('.c','.h','.cpp','.py','.sh') and path.is_file():
            if 'SPDX-License-Identifier: MPL-2.0' not in path.read_text()[:300]:
                raise SystemExit('SPDX notice missing: '+str(path))
print('check-spdx: OK')
PY
