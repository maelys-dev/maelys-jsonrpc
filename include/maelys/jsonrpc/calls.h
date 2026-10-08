/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_JSONRPC_CALLS_H
#define MAELYS_JSONRPC_CALLS_H
#include <maelys/jsonrpc/message.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MAELYS_JSONRPC_DEFAULT_MAX_PENDING 64u
typedef struct maelys_jsonrpc_calls maelys_jsonrpc_calls_t;
typedef struct maelys_jsonrpc_call {
    int64_t id;
    /* Inline copy, valid independently of the table and its reused slots.
     * Userdata is borrowed, never freed or dereferenced by the library. */
    char method[MAELYS_JSONRPC_DEFAULT_MAX_METHOD_LENGTH + 1u];
    uint64_t opened_ms;
    uint64_t deadline_ms;
    void *userdata;
} maelys_jsonrpc_call_t;
maelys_jsonrpc_result_t maelys_jsonrpc_calls_create(size_t max_pending, maelys_jsonrpc_calls_t **);
/* IDs start at 1, increase to INT64_MAX, never wrap or get reused. After
 * exhaustion open returns LIMIT. Failed opens do not reserve IDs.
 * deadline_ms is absolute, must be >= now_ms, and is inclusive for expiry.
 * UINT64_MAX is a valid deadline, not a no-timeout sentinel. */
maelys_jsonrpc_result_t maelys_jsonrpc_calls_open(maelys_jsonrpc_calls_t *, const char *method,
    uint64_t now_ms, uint64_t deadline_ms, void *userdata, int64_t *out_id);
/* No allocation. Only a classified RESPONSE/ERROR from this document can
 * settle; invalid kind/handle is ARGUMENT. Unknown/string/null/out-of-range
 * IDs or duplicate responses return UNKNOWN_ID, without changing out. */
maelys_jsonrpc_result_t maelys_jsonrpc_calls_settle(maelys_jsonrpc_calls_t *,
    const maelys_json_document_t *, const maelys_jsonrpc_message_t *, maelys_jsonrpc_call_t *out);
/* Earliest deadline first, then lowest ID for equal deadlines. AGAIN when
 * nothing is due. Caller supplies time; no clock, waiting or callbacks. */
maelys_jsonrpc_result_t maelys_jsonrpc_calls_expire(maelys_jsonrpc_calls_t *, uint64_t now_ms,
    maelys_jsonrpc_call_t *out);
/* Pull one arbitrary open entry and remove it; AGAIN when empty. */
maelys_jsonrpc_result_t maelys_jsonrpc_calls_cancel(maelys_jsonrpc_calls_t *, maelys_jsonrpc_call_t *out);
size_t maelys_jsonrpc_calls_pending(const maelys_jsonrpc_calls_t *);
/* Returns the number of lost entries, sets *calls=NULL. NULL is harmless.
 * Drain with cancel before release to recover every userdata. */
size_t maelys_jsonrpc_calls_release(maelys_jsonrpc_calls_t **);
#ifdef __cplusplus
}
#endif
#endif
