/* SPDX-License-Identifier: MPL-2.0 */
#ifndef JSONRPC_TEST_CLASSIFICATION_H
#define JSONRPC_TEST_CLASSIFICATION_H
#include <maelys/jsonrpc.h>
#include <jansson.h>
#include <string.h>

static const char *kind_name(maelys_jsonrpc_kind_t kind) {
    switch (kind) {
        case MAELYS_JSONRPC_REQUEST: return "REQUEST";
        case MAELYS_JSONRPC_NOTIFICATION: return "NOTIFICATION";
        case MAELYS_JSONRPC_RESPONSE: return "RESPONSE";
        case MAELYS_JSONRPC_ERROR: return "ERROR";
        case MAELYS_JSONRPC_INVALID: return "INVALID";
    }
    return "UNKNOWN";
}

/* Independent envelope oracle on the legacy parser's DOM, for the bounded
 * integer IDs/error codes in the admitted corpus. No mutable production DOM. */
static maelys_jsonrpc_kind_t legacy_kind(const json_t *root, maelys_jsonrpc_dialect_t dialect) {
    if (!json_is_object(root)) return MAELYS_JSONRPC_INVALID;
    json_t *version = json_object_get(root, "jsonrpc");
    if (version ? (!json_is_string(version) || strcmp(json_string_value(version), "2.0")) :
        dialect != MAELYS_JSONRPC_DIALECT_CODEX) return MAELYS_JSONRPC_INVALID;
    json_t *id = json_object_get(root, "id"), *method = json_object_get(root, "method");
    json_t *params = json_object_get(root, "params"), *result = json_object_get(root, "result");
    json_t *error = json_object_get(root, "error");
    if (id && !json_is_integer(id) && !json_is_string(id) && !json_is_null(id))
        return MAELYS_JSONRPC_INVALID;
    if (method) {
        if (!json_is_string(method) || !json_string_length(method) ||
            json_string_length(method) > 256u || result || error ||
            (params && !json_is_object(params) && !json_is_array(params))) return MAELYS_JSONRPC_INVALID;
        return id ? MAELYS_JSONRPC_REQUEST : MAELYS_JSONRPC_NOTIFICATION;
    }
    if (!id || params || (!result == !error)) return MAELYS_JSONRPC_INVALID;
    if (error) {
        if (!json_is_object(error) || !json_is_integer(json_object_get(error, "code")) ||
            !json_is_string(json_object_get(error, "message"))) return MAELYS_JSONRPC_INVALID;
        return MAELYS_JSONRPC_ERROR;
    }
    return MAELYS_JSONRPC_RESPONSE;
}
#endif
