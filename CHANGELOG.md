# Changelog

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
