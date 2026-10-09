/* SPDX-License-Identifier: MPL-2.0 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct entry {
    maelys_jsonrpc_call_t call;
    unsigned active;
} entry_t;

struct maelys_jsonrpc_calls {
    size_t capacity, pending;
    int64_t next_id; /* zero marks exhaustion */
    entry_t entries[];
};

maelys_jsonrpc_result_t maelys_jsonrpc_calls_create(size_t maximum, maelys_jsonrpc_calls_t **out) {
    if (!out) return MAELYS_JSONRPC_ARGUMENT;
    if (!maximum) maximum = MAELYS_JSONRPC_DEFAULT_MAX_PENDING;
    if (maximum > (SIZE_MAX - sizeof(maelys_jsonrpc_calls_t)) / sizeof(entry_t))
        return MAELYS_JSONRPC_LIMIT;
    maelys_jsonrpc_calls_t *calls = calloc(1u, sizeof(*calls) + maximum * sizeof(entry_t));
    if (!calls) return MAELYS_JSONRPC_MEMORY;
    calls->capacity = maximum;
    calls->next_id = 1;
    *out = calls;
    return MAELYS_JSONRPC_OK;
}

maelys_jsonrpc_result_t maelys_jsonrpc_calls_open(maelys_jsonrpc_calls_t *calls,
    const char *method, uint64_t now, uint64_t deadline, void *userdata, int64_t *out) {
    if (!calls || !out || deadline < now) return MAELYS_JSONRPC_ARGUMENT;
    size_t length;
    maelys_jsonrpc_result_t r = maelys_jsonrpc_method_length(method,
        MAELYS_JSONRPC_DEFAULT_MAX_METHOD_LENGTH, &length);
    if (r != MAELYS_JSONRPC_OK) return r;
    if (calls->pending == calls->capacity) return MAELYS_JSONRPC_FULL;
    if (!calls->next_id) return MAELYS_JSONRPC_LIMIT;
    for (size_t i = 0u; i < calls->capacity; ++i) {
        entry_t *entry = &calls->entries[i];
        if (entry->active) continue;
        entry->call = (maelys_jsonrpc_call_t){0};
        entry->call.id = calls->next_id;
        memcpy(entry->call.method, method, length + 1u);
        entry->call.opened_ms = now;
        entry->call.deadline_ms = deadline;
        entry->call.userdata = userdata;
        entry->active = 1u;
        ++calls->pending;
        *out = calls->next_id;
        calls->next_id = calls->next_id == INT64_MAX ? 0 : calls->next_id + 1;
        return MAELYS_JSONRPC_OK;
    }
    return MAELYS_JSONRPC_STATE;
}

static maelys_jsonrpc_result_t take(maelys_jsonrpc_calls_t *calls, size_t index,
    maelys_jsonrpc_call_t *out) {
    *out = calls->entries[index].call;
    calls->entries[index].active = 0u;
    --calls->pending;
    /* Remove retained userdata as soon as its entry is returned. */
    calls->entries[index].call = (maelys_jsonrpc_call_t){0};
    return MAELYS_JSONRPC_OK;
}

maelys_jsonrpc_result_t maelys_jsonrpc_calls_settle(maelys_jsonrpc_calls_t *calls,
    const maelys_json_document_t *doc, const maelys_jsonrpc_message_t *message,
    maelys_jsonrpc_call_t *out) {
    if (!calls || !doc || !message || !out ||
        (message->kind != MAELYS_JSONRPC_RESPONSE && message->kind != MAELYS_JSONRPC_ERROR))
        return MAELYS_JSONRPC_ARGUMENT;
    int64_t id;
    if (maelys_json_value_i64(doc, message->id, &id) != MAELYS_JSON_OK)
        return MAELYS_JSONRPC_UNKNOWN_ID;
    for (size_t i = 0u; i < calls->capacity; ++i)
        if (calls->entries[i].active && calls->entries[i].call.id == id) return take(calls, i, out);
    return MAELYS_JSONRPC_UNKNOWN_ID;
}

maelys_jsonrpc_result_t maelys_jsonrpc_calls_expire(maelys_jsonrpc_calls_t *calls,
    uint64_t now, maelys_jsonrpc_call_t *out) {
    if (!calls || !out) return MAELYS_JSONRPC_ARGUMENT;
    size_t first = SIZE_MAX;
    for (size_t i = 0u; i < calls->capacity; ++i) {
        const entry_t *entry = &calls->entries[i];
        if (!entry->active || entry->call.deadline_ms > now) continue;
        if (first == SIZE_MAX || entry->call.deadline_ms < calls->entries[first].call.deadline_ms ||
            (entry->call.deadline_ms == calls->entries[first].call.deadline_ms &&
             entry->call.id < calls->entries[first].call.id)) first = i;
    }
    return first == SIZE_MAX ? MAELYS_JSONRPC_AGAIN : take(calls, first, out);
}

maelys_jsonrpc_result_t maelys_jsonrpc_calls_cancel(maelys_jsonrpc_calls_t *calls,
    maelys_jsonrpc_call_t *out) {
    if (!calls || !out) return MAELYS_JSONRPC_ARGUMENT;
    for (size_t i = 0u; i < calls->capacity; ++i)
        if (calls->entries[i].active) return take(calls, i, out);
    return MAELYS_JSONRPC_AGAIN;
}

size_t maelys_jsonrpc_calls_pending(const maelys_jsonrpc_calls_t *calls) {
    return calls ? calls->pending : 0u;
}

size_t maelys_jsonrpc_calls_release(maelys_jsonrpc_calls_t **calls) {
    if (!calls || !*calls) return 0u;
    size_t lost = (*calls)->pending;
    free(*calls);
    *calls = NULL;
    return lost;
}
