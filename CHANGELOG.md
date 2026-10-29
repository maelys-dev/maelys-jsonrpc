# Changelog

## 0.3.0 — 2026-10-29

- Require maelys-json 0.4.0 (interface generation 4): the pin moves to
  v0.4.0, `result.h` asserts ABI 4 instead of exactly 0.3 and ABI 3, and
  pkg-config and CMake require `maelys-json >= 0.4.0`. The JSON-RPC ABI
  stays 2.
- Keep every reader path strict. `maelys_jsonrpc_classify` also refuses,
  with PROTOCOL and kind INVALID, a document whose source repeated a key,
  read through `maelys_json_document_duplicate_count`, even when a caller
  parsed it with the tolerant `RFC8259_LAST_WINS` profile and passes it in
  directly.
- Adopt maelys-release v0.65.1 (from v0.63.0); the checkout scripts move
  with it, as socle 0.64.0 asks.
- Update the executable audit probes to the pinned maelys-json v0.4.0;
  historical stage-0 reports retain their recorded dependency version.

### Decisions

- Duplicate provenance is rejected at classification in both dialects;
  tolerant parsing never makes a duplicate-bearing document a valid message.
- The JSON-RPC ABI remains 2; the required JSON interface generation is 4.

## 0.2.0 — 2026-10-09

- Add the CODEX read dialect for envelopes without a jsonrpc member; STRICT
  remains the default for NULL and zero-initialized limits.
- Count accepted object documents without the version member in reader stats.
- Settle caller-classified responses without a second envelope classification.
- Extend corpus differential checks to classification by protocol, call
  settlement and both dialects; retain named parsing exceptions.
- Public structure additions raise the library ABI to 2; layout and source
  compatibility tests preserve the existing zero/NULL caller behavior.

### Décisions

- Tolerance is read-only and counted; present jsonrpc must still equal "2.0".
  Writers always emit "2.0". STRICT remains the default.
- Harness (app-server client) and cx (WebSocket daemon) consume the CODEX dialect.
- Calls API and integer ID rules are unchanged. Classification decides the
  dialect once; settle checks only response/error kind, integer ID and membership.
- Corpus responses with ID 0 come from a capture client not built on calls.
  A calls client cannot emit them: IDs start at 1. UNKNOWN_ID means the table
  never emitted that ID, not special treatment of zero. Replay uses one table
  per stream, counts duplicate responses, cancels leftovers and loses no entries.
- Content-Length still has no named live consumer and remains excluded.

## 0.1.0 — 2026-10-08

- Socle generated with maelys-release v0.63.0; maelys-json pinned at v0.3.0
  (`d106890b02b3fbfd599ecd22bdeffb600db66123`), with ABI 3 asserted.
- Implemented bounded framing, immutable message classification, writer
  envelopes and integer call correlation with cancel/lost-entry reporting.
- Captured admitted synthetic binary streams; recorded Gemini fixture provenance,
  named differential exceptions and document/ordering/statistics chunk invariance.
- Added strict builds, installation consumers, sanitizers, coverage, two fuzzers,
  release archives and the libmaelys-jsonrpc Homebrew template.
- Codex turn/declined approval evidence is captured.
  Non-integer relay is required, with exact lexemes and a two-pass byte fixed
  point. Fractions/exponents remain non-canonical according to is_canonical.

### Décisions

- C11; immutable maelys-json documents, RFC 8259; no production jansson.
- No transport, thread, clock, known method or mutable DOM.
- Duplicate keys are rejected; rejected lines must leave the stream open.
- Content-Length excluded: no live consumer found in the audited references.
- Request correlation belongs to the library; emitted IDs are integers.
- Gate `none`; 1.0.0 requires two published consumers, harness and cx.
- maelys-mcp adoption waits for ABI 7; the audited release exposes ABI 6.

- Revision 2.2: C0 is escaped; corpus admission precedes writing; calls_cancel
  drains entries, while calls_release reports any remaining lost userdata.
- Number text: the public 64-byte bound does not constrain document copy;
  copy remains bounded by maximum_bytes. Out-of-range integers remain RANGE.

## 0.0.0 — 2026-10-08

- Initial unpublished repository scaffold.
