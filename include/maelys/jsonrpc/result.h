/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_JSONRPC_RESULT_H
#define MAELYS_JSONRPC_RESULT_H
#include <maelys/json.h>
#ifdef __cplusplus
static_assert(MAELYS_JSON_VERSION_MAJOR == 0 && MAELYS_JSON_VERSION_MINOR == 3 && MAELYS_JSON_ABI_VERSION == 3u,
    "maelys-jsonrpc requires maelys-json 0.3.x (ABI 3)");
extern "C" {
#else
_Static_assert(MAELYS_JSON_VERSION_MAJOR == 0 && MAELYS_JSON_VERSION_MINOR == 3 && MAELYS_JSON_ABI_VERSION == 3u,
    "maelys-jsonrpc requires maelys-json 0.3.x (ABI 3)");
#endif
typedef enum maelys_jsonrpc_result {
    MAELYS_JSONRPC_OK = 0,
    MAELYS_JSONRPC_AGAIN,
    MAELYS_JSONRPC_ARGUMENT,
    MAELYS_JSONRPC_PROTOCOL,
    MAELYS_JSONRPC_LIMIT,
    MAELYS_JSONRPC_UNKNOWN_ID,
    MAELYS_JSONRPC_FULL,
    MAELYS_JSONRPC_MEMORY,
    MAELYS_JSONRPC_STATE
} maelys_jsonrpc_result_t;
const char *maelys_jsonrpc_result_string(maelys_jsonrpc_result_t result);
#ifdef __cplusplus
}
#endif
#endif
