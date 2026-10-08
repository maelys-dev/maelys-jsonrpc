/* SPDX-License-Identifier: MPL-2.0 */
#include "internal.h"

const char *maelys_jsonrpc_version(void) { return MAELYS_JSONRPC_VERSION_STRING; }

const char *maelys_jsonrpc_result_string(maelys_jsonrpc_result_t result) {
    switch (result) {
        case MAELYS_JSONRPC_OK: return "success";
        case MAELYS_JSONRPC_AGAIN: return "more input or no pending entry";
        case MAELYS_JSONRPC_ARGUMENT: return "invalid argument";
        case MAELYS_JSONRPC_PROTOCOL: return "invalid protocol document";
        case MAELYS_JSONRPC_LIMIT: return "configured limit exceeded";
        case MAELYS_JSONRPC_UNKNOWN_ID: return "unknown response ID";
        case MAELYS_JSONRPC_FULL: return "pending call table full";
        case MAELYS_JSONRPC_MEMORY: return "allocation failed";
        case MAELYS_JSONRPC_STATE: return "invalid state";
    }
    return "unknown result";
}

maelys_jsonrpc_result_t maelys_jsonrpc_from_json(maelys_json_result_t result) {
    switch (result) {
        case MAELYS_JSON_OK: return MAELYS_JSONRPC_OK;
        case MAELYS_JSON_ERR_ARGUMENT: return MAELYS_JSONRPC_ARGUMENT;
        case MAELYS_JSON_ERR_MEMORY: return MAELYS_JSONRPC_MEMORY;
        case MAELYS_JSON_ERR_LIMIT: return MAELYS_JSONRPC_LIMIT;
        case MAELYS_JSON_ERR_STATE: return MAELYS_JSONRPC_STATE;
        case MAELYS_JSON_ERR_SYNTAX:
        case MAELYS_JSON_ERR_UTF8:
        case MAELYS_JSON_ERR_DUPLICATE_KEY:
        case MAELYS_JSON_ERR_TYPE:
        case MAELYS_JSON_ERR_RANGE:
        case MAELYS_JSON_ERR_NOT_FOUND:
        case MAELYS_JSON_ERR_NOT_INTEGER:
        case MAELYS_JSON_ERR_IO: return MAELYS_JSONRPC_PROTOCOL;
    }
    return MAELYS_JSONRPC_ARGUMENT;
}
