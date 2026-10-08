/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_JSONRPC_FRAME_H
#define MAELYS_JSONRPC_FRAME_H
#include <maelys/jsonrpc/result.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MAELYS_JSONRPC_DEFAULT_MAXIMUM_BYTES 1048576u
#define MAELYS_JSONRPC_DEFAULT_MAXIMUM_DEPTH 64u
#define MAELYS_JSONRPC_DEFAULT_MAXIMUM_TOKENS 65536u
typedef struct maelys_jsonrpc_reader maelys_jsonrpc_reader_t;
typedef struct maelys_jsonrpc_reader_options {
    maelys_json_limits_t limits;
    unsigned tolerate_preamble : 1;
    unsigned strip_cr : 1;
} maelys_jsonrpc_reader_options_t;
typedef struct maelys_jsonrpc_reader_stats {
    uint64_t bytes_seen;
    uint64_t lines_seen;
    uint64_t blank_lines;
    uint64_t preamble_lines;
    uint64_t line_overflows;
    uint64_t rejected_lines;
    uint64_t documents;
    unsigned stream_started;
} maelys_jsonrpc_reader_stats_t;
maelys_jsonrpc_result_t maelys_jsonrpc_reader_create(
    const maelys_jsonrpc_reader_options_t *, maelys_jsonrpc_reader_t **);
/* Copies all bytes, without callbacks. Pending complete lines occupy memory
 * proportional to undrained input; call next between feeds for backpressure.
 * Each retained line is bounded by maximum_bytes (excluding LF and stripped CR).
 * MEMORY leaves a failed reader: release it, do not retry a partially copied feed. */
maelys_jsonrpc_result_t maelys_jsonrpc_reader_feed(
    maelys_jsonrpc_reader_t *, const void *bytes, size_t n);
/* Transfers one immutable document; caller releases it with document_release.
 * Empty lines and tolerated preamble are skipped. PROTOCOL consumes the entire
 * refused line, including overflows; why carries the underlying JSON error.
 * Output document is written only on OK. MEMORY also consumes that line. */
maelys_jsonrpc_result_t maelys_jsonrpc_reader_next(
    maelys_jsonrpc_reader_t *, maelys_json_document_t **out, maelys_json_error_t *why);
/* Checks only orphan bytes, without closing/sealing the reader. Complete queued
 * lines can still be pulled after finish. A dropped line without LF is orphaned. */
maelys_jsonrpc_result_t maelys_jsonrpc_reader_finish(const maelys_jsonrpc_reader_t *);
/* Counters saturate at UINT64_MAX. Byte/line/overflow/blank counters advance at
 * feed, document/rejection/preamble counters at next. No allocation. */
void maelys_jsonrpc_reader_stats(const maelys_jsonrpc_reader_t *, maelys_jsonrpc_reader_stats_t *);
void maelys_jsonrpc_reader_release(maelys_jsonrpc_reader_t **);
/* JSON validation only, no line splitting or message classification; RFC 8259
 * is fixed. LIMIT and malformed JSON return PROTOCOL with the underlying why.
 * Zero limits use JSON-RPC defaults, not maelys-json's smaller defaults. */
maelys_jsonrpc_result_t maelys_jsonrpc_parse(const void *bytes, size_t n,
    const maelys_json_limits_t *, maelys_json_document_t **, maelys_json_error_t *);
#ifdef __cplusplus
}
#endif
#endif
