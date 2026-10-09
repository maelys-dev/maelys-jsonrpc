/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_JSONRPC_MESSAGE_H
#define MAELYS_JSONRPC_MESSAGE_H
#include <maelys/jsonrpc/result.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MAELYS_JSONRPC_DEFAULT_MAX_METHOD_LENGTH 256u
#define MAELYS_JSONRPC_DEFAULT_MAX_ERROR_EXCERPT 512u
typedef enum maelys_jsonrpc_kind {
    MAELYS_JSONRPC_REQUEST, MAELYS_JSONRPC_NOTIFICATION,
    MAELYS_JSONRPC_RESPONSE, MAELYS_JSONRPC_ERROR, MAELYS_JSONRPC_INVALID
} maelys_jsonrpc_kind_t;
typedef enum maelys_jsonrpc_dialect {
    MAELYS_JSONRPC_DIALECT_STRICT = 0,
    MAELYS_JSONRPC_DIALECT_CODEX = 1
} maelys_jsonrpc_dialect_t;
typedef struct maelys_jsonrpc_limits {
    size_t max_method_length; /* zero selects 256 */
    size_t max_error_excerpt; /* caller excerpt policy: zero selects 512; pass cap accordingly */
    maelys_jsonrpc_dialect_t dialect; /* zero/NULL: STRICT; CODEX permits an absent jsonrpc */
} maelys_jsonrpc_limits_t;
typedef struct maelys_jsonrpc_message {
    maelys_jsonrpc_kind_t kind;
    maelys_json_value_t id;
    const char *method; /* borrowed from document */
    maelys_json_value_t params, result, error;
} maelys_jsonrpc_message_t;
/* No allocation. A malformed envelope returns PROTOCOL with kind INVALID.
 * STRICT requires jsonrpc:"2.0". CODEX tolerates only its absence; a present
 * member must still be the string "2.0". Unknown dialects return ARGUMENT.
 * IDs are string, null, or an integer lexeme (no fraction/exponent); a lexeme
 * outside int64 still classifies, but cannot settle our integer-emitter calls.
 * Params, when present, must be an object or array. Extra members are allowed.
 * ERROR requires an object with integer int64 code and string message.
 * Error excerpt policy is applied by the caller through error_read cap;
 * classification never rejects a long message for excerpt policy. */
maelys_jsonrpc_result_t maelys_jsonrpc_classify(const maelys_json_document_t *,
    const maelys_jsonrpc_limits_t *, maelys_jsonrpc_message_t *out);
/* Copies at most min(cap-1,512) decoded bytes, stopping at a UTF-8 boundary.
 * cap=0 permits excerpt=NULL (code-only); max_error_excerpt in classify does
 * not change this stateless operation. A smaller cap supplies a tighter bound. */
maelys_jsonrpc_result_t maelys_jsonrpc_error_read(const maelys_json_document_t *,
    maelys_json_value_t error, int64_t *code, char *excerpt, size_t cap);
/* Caller creates an RFC 8259 writer; flags FINAL_NEWLINE select JSON Lines.
 * begin leaves the root open. Write params/result using the maelys-json writer.
 * Invalid arguments/method length are checked before modifying the writer.
 * Subsequent writer failures follow maelys-json's sticky failure model. */
maelys_jsonrpc_result_t maelys_jsonrpc_begin_request(maelys_json_writer_t *, int64_t id, const char *method);
maelys_jsonrpc_result_t maelys_jsonrpc_begin_notification(maelys_json_writer_t *, const char *method);
maelys_jsonrpc_result_t maelys_jsonrpc_begin_response(maelys_json_writer_t *,
    const maelys_json_document_t *, maelys_json_value_t id);
/* Complete object, without data; then call writer_finish (not jsonrpc_end).
 * message C0 characters are escaped by the underlying writer. */
maelys_jsonrpc_result_t maelys_jsonrpc_write_error(maelys_json_writer_t *,
    const maelys_json_document_t *, maelys_json_value_t id, int64_t code, const char *message);
maelys_jsonrpc_result_t maelys_jsonrpc_end(maelys_json_writer_t *, char **out, size_t *n);
#ifdef __cplusplus
}
#endif
#endif
