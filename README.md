# maelys-jsonrpc

Bounded JSON-RPC 2.0 documents and request correlation in C11, built on
maelys-json. Implements stages 1 and 2 of the JSON-RPC extraction.

`frame.h` accepts byte chunks and yields immutable documents one at a time.
`message.h` classifies envelopes and writes requests, notifications, responses
and errors through the maelys-json writer. `calls.h` pairs integer response IDs
with pending requests using deadlines supplied by the caller.

The caller owns transport, scheduling and clocks. No callbacks or mutable DOM.
Duplicate keys, invalid UTF-8, U+0000 and BOM are rejected. A rejected line is
consumed and counted; the next line remains available. Defaults are 1 MiB per
line, depth 64, 65,536 tokens, 256 method bytes and 64 pending calls. Feed copies
undrained lines: pull `reader_next` between feeds to control retained memory.

```sh
make check MAELYS_JSON_DIR=/path/to/maelys-json
make asan-ubsan MAELYS_JSON_DIR=/path/to/maelys-json
make fuzz-smoke FUZZ_TIME=30 MAELYS_JSON_DIR=/path/to/maelys-json
```

The dependency pin is maelys-json v0.3.0 (ABI 3). jansson is required only for the
legacy-reader differential tests. CMake exports `maelys::jsonrpc`;
pkg-config reports `maelys-jsonrpc` with `Requires: maelys-json >= 0.3.0`.

Readers transfer documents to the caller, who releases them with
`maelys_json_document_release`. Message strings and handles borrow that document.
`begin_request`, `begin_notification` and `begin_response` leave the root open;
write params/result and call `maelys_jsonrpc_end`. `write_error` closes a complete
object; finish it with `maelys_json_writer_finish`. Create the writer with
`MAELYS_JSON_WRITER_FINAL_NEWLINE` for JSON Lines. C0 characters are escaped.

Calls return an owned inline method copy and unchanged borrowed userdata.
Drain with `calls_cancel` before release; release reports the number lost.

Raw corpus manifests record source, tool, date, exact descriptor bytes and SHA256.
Admission rejects personal paths and credential patterns before writing.
Gemini executable fixtures are labelled; Codex turn/declined approval evidence
is recorded in `docs/audits/corpus.json`. `make test` verifies every admitted
stream in two chunkings and records named differential exceptions. Scenario 8
requires exact non-integer lexeme relay and a byte fixed point after rereading
and writing twice. Keys and escaping are normalized, while `is_canonical`
remains false for fractions/exponents. Copying a received document is bounded
by its byte limit, including number lexemes longer than 64 bytes; the public
`writer_number_text` call retains its separate 64-byte bound. Integers outside
[-2^63, 2^64-1] still return RANGE. No floating-point conversion is used.
