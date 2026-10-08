#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Check the production dependency graph and prohibited transport primitives."""
import pathlib
import re

for root in ("src", "include"):
    for path in pathlib.Path(root).rglob("*"):
        if path.suffix not in (".c", ".h"):
            continue
        text = path.read_text()
        text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
        if re.search(r"\b(jansson|json_t|pthread\w*|clock_gettime|fork|socket|select|poll|read|write|sleep)\s*(?:\(|\.h|\*)", text):
            raise SystemExit(f"transport/system/DOM dependency: {path}")
        if re.search(r"maelys/(http|system|mcp)", text):
            raise SystemExit(f"unexpected Maelys dependency: {path}")
        if re.search(r"Content-Length|thread/start|turn/start|tools/call|session/prompt", text):
            raise SystemExit(f"known method or excluded framing: {path}")
print("check-boundaries: OK")
