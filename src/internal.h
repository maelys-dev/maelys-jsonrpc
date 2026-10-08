/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_JSONRPC_INTERNAL_H
#define MAELYS_JSONRPC_INTERNAL_H
#include <maelys/jsonrpc.h>
maelys_jsonrpc_result_t maelys_jsonrpc_from_json(maelys_json_result_t result);
int maelys_jsonrpc_id_valid(const maelys_json_document_t *, maelys_json_value_t);
maelys_jsonrpc_result_t maelys_jsonrpc_method_length(const char *, size_t, size_t *);
#endif
