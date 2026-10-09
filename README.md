# maelys-jsonrpc

Bounded JSON-RPC 2.0 documents and request correlation in C11, built on
maelys-json. Implements stages 1 and 2 of the JSON-RPC extraction.

`frame.h` accepts byte chunks and yields immutable documents one at a time.
`message.h` classifies envelopes and writes requests, notifications, responses
and errors through the maelys-json writer. `calls.h` pairs integer response IDs
with pending requests using deadlines supplied by the caller.

Classification defaults to STRICT: `jsonrpc:"2.0"` is required. For Codex
app-server documents, harness and cx select
`maelys_jsonrpc_limits_t limits = {.dialect = MAELYS_JSONRPC_DIALECT_CODEX}`
and pass it to `maelys_jsonrpc_classify`. CODEX permits an absent version member;
a present member must still be "2.0". Writers always include "2.0".
The reader's `missing_version` counts accepted object documents without the
member, independently of classification; malformed envelopes such as `{}` count,
while rejected JSON, arrays/scalars, blanks and preambles do not.

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

Pass the already classified response and its document to `calls_settle`.
Settlement trusts classification and checks only response/error kind, integer ID
and membership; there is no second dialect decision. Replies to incoming
requests use `begin_response` to copy integer/string/null IDs independently of
calls. Clients emitting string IDs switch to integers returned by `calls_open`;
Codex 0.161 interoperability is verified by the harness.
Calls return an owned inline method copy and unchanged borrowed userdata.
Drain with `calls_cancel` before release; release reports the number lost.
The corpus capture client did not use calls: its responses with ID 0 cannot be
produced by a calls client, whose IDs start at 1. The corpus replay reserves
1..max ID in one table per stream, counts repeated responses as UNKNOWN_ID,
and cancels all remaining entries before verifying that release reports zero.
Zero is unknown because this table never emitted it, not a special protocol ID.

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
