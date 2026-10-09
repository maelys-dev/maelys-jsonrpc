/* SPDX-License-Identifier: MPL-2.0 */
#include "framework.h"

void test_calls(void) {
    maelys_jsonrpc_calls_t *calls = NULL; R(maelys_jsonrpc_calls_create(0u, &calls));
    int64_t ids[64]; int userdata[64];
    for (size_t i = 0u; i < 64u; ++i) {
        userdata[i] = (int)i;
        R(maelys_jsonrpc_calls_open(calls, "echo", 0u, 100u - i, &userdata[i], &ids[i]));
        CHECK(ids[i] == (int64_t)i + 1);
        /* Twin of the id-0 UNKNOWN_ID assertion: the table never emitted 0. */
        if (!i) CHECK(ids[i] == 1 && ids[i] != 0);
    }
    int64_t untouched = -1;
    CHECK(maelys_jsonrpc_calls_open(calls, "extra", 0u, 100u, NULL, &untouched) == MAELYS_JSONRPC_FULL);
    CHECK(untouched == -1 && maelys_jsonrpc_calls_pending(calls) == 64u);
    const char *responses[] = {"{\"jsonrpc\":\"2.0\",\"id\":999,\"result\":null}",
        "{\"jsonrpc\":\"2.0\",\"id\":\"1\",\"result\":null}",
        "{\"jsonrpc\":\"2.0\",\"id\":null,\"result\":null}",
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-1,\"message\":\"x\"}}",
        "{\"jsonrpc\":\"2.0\",\"id\":0,\"result\":null}"};
    maelys_jsonrpc_call_t returned = {0};
    for (size_t i = 0u; i < 5u; ++i) {
        maelys_json_document_t *doc = NULL;
        R(maelys_jsonrpc_parse(responses[i], strlen(responses[i]), NULL, &doc, NULL));
        maelys_jsonrpc_message_t message; R(maelys_jsonrpc_classify(doc, NULL, &message));
        CHECK(maelys_jsonrpc_calls_settle(calls, doc, &message, &returned) ==
            (i == 3u ? MAELYS_JSONRPC_OK : MAELYS_JSONRPC_UNKNOWN_ID));
        if (i == 3u) {
            CHECK(returned.id == 1 && returned.userdata == &userdata[0]);
            CHECK(!strcmp(returned.method, "echo"));
            CHECK(maelys_jsonrpc_calls_settle(calls, doc, &message, &returned) == MAELYS_JSONRPC_UNKNOWN_ID);
        }
        maelys_json_document_release(doc);
    }
    CHECK(maelys_jsonrpc_calls_expire(calls, 36u, &returned) == MAELYS_JSONRPC_AGAIN);
    for (size_t i = 63u; i > 0u; --i) {
        R(maelys_jsonrpc_calls_expire(calls, 100u, &returned));
        CHECK(returned.id == ids[i] && returned.userdata == &userdata[i]);
        CHECK(returned.deadline_ms == 100u - i);
    }
    CHECK(maelys_jsonrpc_calls_pending(calls) == 0u);
    int64_t next; R(maelys_jsonrpc_calls_open(calls, "new", 100u, 100u, &userdata[0], &next));
    CHECK(next == 65);
    R(maelys_jsonrpc_calls_expire(calls, 100u, &returned)); CHECK(returned.id == next);
    for (size_t i = 0u; i < 10u; ++i) R(maelys_jsonrpc_calls_open(calls, "cancel", 0u, UINT64_MAX, &userdata[i], &next));
    maelys_jsonrpc_call_t saved;
    for (size_t i = 0u; i < 10u; ++i) {
        R(maelys_jsonrpc_calls_cancel(calls, &returned)); CHECK(returned.userdata == &userdata[i]);
        if (!i) saved = returned;
    }
    CHECK(maelys_jsonrpc_calls_cancel(calls, &returned) == MAELYS_JSONRPC_AGAIN);
    CHECK(maelys_jsonrpc_calls_release(&calls) == 0u && !calls);
    CHECK(!strcmp(saved.method, "cancel") && saved.userdata == &userdata[0]);
    R(maelys_jsonrpc_calls_create(10u, &calls));
    for (size_t i = 0u; i < 10u; ++i) R(maelys_jsonrpc_calls_open(calls, "lost", 0u, 0u, &userdata[i], &next));
    CHECK(maelys_jsonrpc_calls_release(&calls) == 10u && !calls);
    CHECK(maelys_jsonrpc_calls_release(&calls) == 0u);
    CHECK(maelys_jsonrpc_calls_create(SIZE_MAX, &calls) == MAELYS_JSONRPC_LIMIT);
    R(maelys_jsonrpc_calls_create(2u, &calls));
    CHECK(maelys_jsonrpc_calls_open(calls, "method", 10u, 9u, NULL, &next) == MAELYS_JSONRPC_ARGUMENT);
    CHECK(maelys_jsonrpc_calls_open(calls, "", 0u, 1u, NULL, &next) == MAELYS_JSONRPC_ARGUMENT);
    R(maelys_jsonrpc_calls_open(calls, "tie1", 0u, 1u, NULL, &next)); CHECK(next == 1);
    R(maelys_jsonrpc_calls_open(calls, "tie2", 0u, 1u, NULL, &next)); CHECK(next == 2);
    R(maelys_jsonrpc_calls_expire(calls, 1u, &returned)); CHECK(returned.id == 1);
    R(maelys_jsonrpc_calls_expire(calls, 1u, &returned)); CHECK(returned.id == 2);
    CHECK(maelys_jsonrpc_calls_release(&calls) == 0u);
}
