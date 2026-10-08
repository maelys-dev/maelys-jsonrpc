/* SPDX-License-Identifier: MPL-2.0 */
#include "framework.h"
int main(void) {
    CHECK(!strcmp(maelys_jsonrpc_version(), MAELYS_JSONRPC_VERSION_STRING));
    for (int i = MAELYS_JSONRPC_OK; i <= MAELYS_JSONRPC_STATE; ++i)
        CHECK(maelys_jsonrpc_result_string((maelys_jsonrpc_result_t)i) != NULL);
    CHECK(!strcmp(maelys_jsonrpc_result_string((maelys_jsonrpc_result_t)99), "unknown result"));
    test_reader(); test_message(); test_calls();
    puts("jsonrpc: reader, messages, calls OK");
    return 0;
}
