/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/jsonrpc.h>
#include <stdlib.h>
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size) {
    if (!size || size > 65536u) return 0;
    size_t maximum = (data[0] & 15u) + 1u, active = 0u;
    maelys_jsonrpc_calls_t *calls = NULL;
    if (maelys_jsonrpc_calls_create(maximum, &calls) != MAELYS_JSONRPC_OK) return 0;
    int64_t last = 0;
    for (size_t i = 1u; i < size; ++i) {
        maelys_jsonrpc_call_t out;
        maelys_jsonrpc_result_t r;
        switch (data[i] % 4u) {
            case 0: {
                int64_t id = 0;
                r = maelys_jsonrpc_calls_open(calls, "fuzz", 0u, data[i], NULL, &id);
                if (r == MAELYS_JSONRPC_OK) { if (id <= last || active == maximum) abort(); ++active; last = id; }
                else if (r != MAELYS_JSONRPC_FULL) abort();
                break;
            }
            case 1:
                r = maelys_jsonrpc_calls_expire(calls, data[i], &out);
                if (r == MAELYS_JSONRPC_OK) { if (!active || out.deadline_ms > data[i]) abort(); --active; }
                else if (r != MAELYS_JSONRPC_AGAIN) abort();
                break;
            case 2:
                r = maelys_jsonrpc_calls_cancel(calls, &out);
                if (r == MAELYS_JSONRPC_OK) { if (!active) abort(); --active; }
                else if (r != MAELYS_JSONRPC_AGAIN) abort();
                break;
            default: {
                /* Additional bytes may be arbitrary response documents. */
                maelys_json_document_t *doc = NULL;
                maelys_json_limits_t limits = {4096u, 8u, 128u};
                size_t n = size - i < 4096u ? size - i : 4096u;
                if (maelys_jsonrpc_parse(data + i, n, &limits, &doc, NULL) == MAELYS_JSONRPC_OK) {
                    maelys_jsonrpc_message_t message;
                    if (maelys_jsonrpc_classify(doc, NULL, &message) == MAELYS_JSONRPC_OK &&
                        (message.kind == MAELYS_JSONRPC_RESPONSE || message.kind == MAELYS_JSONRPC_ERROR)) {
                        r = maelys_jsonrpc_calls_settle(calls, doc, &message, &out);
                        if (r == MAELYS_JSONRPC_OK) { if (!active) abort(); --active; }
                        else if (r != MAELYS_JSONRPC_UNKNOWN_ID) abort();
                    }
                    maelys_json_document_release(doc);
                }
                break;
            }
        }
        if (maelys_jsonrpc_calls_pending(calls) != active) abort();
    }
    if (maelys_jsonrpc_calls_release(&calls) != active || calls) abort();
    return 0;
}
