/* SPDX-License-Identifier: MPL-2.0 */
#include "framework.h"
#include <stddef.h>

typedef struct legacy_limits { size_t max_method_length, max_error_excerpt; } legacy_limits_t;
typedef struct legacy_stats {
    uint64_t bytes_seen, lines_seen, blank_lines, preamble_lines;
    uint64_t line_overflows, rejected_lines, documents;
    unsigned stream_started;
} legacy_stats_t;
_Static_assert(MAELYS_JSONRPC_DIALECT_STRICT == 0, "zero preserves strict");
_Static_assert(MAELYS_JSONRPC_DIALECT_CODEX == 1, "Codex dialect value");
_Static_assert(offsetof(maelys_jsonrpc_limits_t, dialect) == sizeof(legacy_limits_t), "append dialect");
_Static_assert(sizeof(maelys_jsonrpc_limits_t) ==
    ((sizeof(legacy_limits_t) + sizeof(maelys_jsonrpc_dialect_t) + _Alignof(maelys_jsonrpc_limits_t) - 1u) /
    _Alignof(maelys_jsonrpc_limits_t)) * _Alignof(maelys_jsonrpc_limits_t), "limits layout");
_Static_assert(offsetof(maelys_jsonrpc_reader_stats_t, missing_version) == sizeof(legacy_stats_t), "append counter");
_Static_assert(sizeof(maelys_jsonrpc_reader_stats_t) == sizeof(legacy_stats_t) + sizeof(uint64_t), "stats layout");

static void same(const maelys_jsonrpc_message_t *a, const maelys_jsonrpc_message_t *b) {
    CHECK(a->kind == b->kind && a->id == b->id && a->method == b->method);
    CHECK(a->params == b->params && a->result == b->result && a->error == b->error);
}

void test_dialect(void) {
    const struct { const char *text; maelys_jsonrpc_kind_t kind; } cases[] = {
        {"{\"id\":1,\"method\":\"echo\",\"params\":[]}", MAELYS_JSONRPC_REQUEST},
        {"{\"method\":\"echo\"}", MAELYS_JSONRPC_NOTIFICATION},
        {"{\"id\":1,\"result\":null}", MAELYS_JSONRPC_RESPONSE},
        {"{\"id\":1,\"error\":{\"code\":-1,\"message\":\"x\"}}", MAELYS_JSONRPC_ERROR},
        {"{\"id\":\"approval\",\"method\":\"echo\"}", MAELYS_JSONRPC_REQUEST},
        {"{\"id\":null,\"method\":\"echo\"}", MAELYS_JSONRPC_REQUEST},
        {"{}", MAELYS_JSONRPC_INVALID}, {"[]", MAELYS_JSONRPC_INVALID},
        {"null", MAELYS_JSONRPC_INVALID},
        {"{\"id\":1,\"result\":{},\"error\":{}}", MAELYS_JSONRPC_INVALID},
        {"{\"id\":1.0,\"result\":{}}", MAELYS_JSONRPC_INVALID},
        {"{\"id\":true,\"result\":{}}", MAELYS_JSONRPC_INVALID},
        {"{\"method\":\"echo\",\"params\":null}", MAELYS_JSONRPC_INVALID},
        {"{\"id\":1}", MAELYS_JSONRPC_INVALID},
        {"{\"method\":\"\"}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":\"1.0\",\"id\":1,\"result\":{}}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":null,\"id\":1,\"result\":{}}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":2,\"id\":1,\"result\":{}}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}", MAELYS_JSONRPC_RESPONSE}
    };
    /* These initializations and calls are unchanged v0.1.0 source. */
    maelys_jsonrpc_limits_t zero = {0};
    maelys_jsonrpc_limits_t explicit_strict = {.dialect = MAELYS_JSONRPC_DIALECT_STRICT};
    maelys_jsonrpc_limits_t codex = {.dialect = MAELYS_JSONRPC_DIALECT_CODEX};
    for (size_t i = 0u; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        maelys_json_document_t *doc = NULL;
        R(maelys_jsonrpc_parse(cases[i].text, strlen(cases[i].text), NULL, &doc, NULL));
        maelys_jsonrpc_message_t a, b, c, d;
        maelys_jsonrpc_result_t strict = maelys_jsonrpc_classify(doc, NULL, &a);
        CHECK(maelys_jsonrpc_classify(doc, &zero, &b) == strict); same(&a, &b);
        CHECK(maelys_jsonrpc_classify(doc, &explicit_strict, &c) == strict); same(&a, &c);
        CHECK(maelys_jsonrpc_classify(doc, &codex, &d) ==
            (cases[i].kind == MAELYS_JSONRPC_INVALID ? MAELYS_JSONRPC_PROTOCOL : MAELYS_JSONRPC_OK));
        CHECK(d.kind == cases[i].kind);
        if (strstr(cases[i].text, "jsonrpc")) same(&a, &d);
        else CHECK(a.kind == MAELYS_JSONRPC_INVALID);
        if (d.kind == MAELYS_JSONRPC_REQUEST) {
            maelys_json_writer_t *writer = NULL; char *text = NULL; size_t n = 0u;
            J(maelys_json_writer_create(MAELYS_JSON_PROFILE_RFC8259, NULL, MAELYS_JSON_WRITER_FINAL_NEWLINE, &writer));
            R(maelys_jsonrpc_begin_response(writer, doc, d.id));
            J(maelys_json_writer_key_cstr(writer, "result")); J(maelys_json_writer_null(writer));
            R(maelys_jsonrpc_end(writer, &text, &n));
            maelys_json_document_t *reply = NULL;
            R(maelys_jsonrpc_parse(text, n, NULL, &reply, NULL));
            R(maelys_jsonrpc_classify(reply, NULL, &b)); CHECK(b.kind == MAELYS_JSONRPC_RESPONSE);
            CHECK(maelys_json_value_type(doc, d.id) == maelys_json_value_type(reply, b.id));
            maelys_json_document_release(reply); free(text); maelys_json_writer_release(writer);
        }
        codex.max_method_length = 3u;
        if (d.method) CHECK(maelys_jsonrpc_classify(doc, &codex, &b) == MAELYS_JSONRPC_PROTOCOL);
        codex.max_method_length = 0u;
        maelys_jsonrpc_limits_t invalid = {.dialect = (maelys_jsonrpc_dialect_t)2};
        CHECK(maelys_jsonrpc_classify(doc, &invalid, &b) == MAELYS_JSONRPC_ARGUMENT);
        CHECK(b.kind == MAELYS_JSONRPC_INVALID);
        maelys_json_document_release(doc);
    }
    for (size_t length = 256u; length <= 257u; ++length) {
        char text[300], method[258]; memset(method, 'x', length); method[length] = '\0';
        int n = snprintf(text, sizeof(text), "{\"method\":\"%s\"}", method);
        CHECK(n > 0 && (size_t)n < sizeof(text));
        maelys_json_document_t *doc = NULL;
        R(maelys_jsonrpc_parse(text, (size_t)n, NULL, &doc, NULL));
        maelys_jsonrpc_message_t message;
        CHECK(maelys_jsonrpc_classify(doc, &codex, &message) ==
            (length == 256u ? MAELYS_JSONRPC_OK : MAELYS_JSONRPC_PROTOCOL));
        maelys_json_document_release(doc);
    }
    const char *stream = "Starting server\n\n{}\n[]\nnull\n{\"id\":1,\"result\":{}}\n"
        "{\"jsonrpc\":null}\n{\"jsonrpc\":\"1.0\"}\n{\"jsonrpc\":\"2.0\"}\n{bad}\n";
    maelys_jsonrpc_reader_options_t options = {0}; options.tolerate_preamble = 1u;
    maelys_jsonrpc_reader_t *reader = NULL; R(maelys_jsonrpc_reader_create(&options, &reader));
    R(maelys_jsonrpc_reader_feed(reader, stream, strlen(stream)));
    for (;;) {
        maelys_json_document_t *doc = NULL;
        maelys_jsonrpc_result_t r = maelys_jsonrpc_reader_next(reader, &doc, NULL);
        if (r == MAELYS_JSONRPC_AGAIN) break;
        CHECK(r == MAELYS_JSONRPC_OK || r == MAELYS_JSONRPC_PROTOCOL);
        maelys_json_document_release(doc);
    }
    maelys_jsonrpc_reader_stats_t stats; maelys_jsonrpc_reader_stats(reader, &stats);
    CHECK(stats.documents == 7u && stats.missing_version == 2u);
    CHECK(stats.blank_lines == 1u && stats.preamble_lines == 1u && stats.rejected_lines == 1u);
    R(maelys_jsonrpc_reader_finish(reader)); maelys_jsonrpc_reader_release(&reader);

    maelys_jsonrpc_calls_t *calls = NULL; R(maelys_jsonrpc_calls_create(2u, &calls));
    for (int i = 0; i < 2; ++i) {
        int64_t id; int userdata = i;
        R(maelys_jsonrpc_calls_open(calls, "echo", 0u, 1u, &userdata, &id));
        char response[100];
        int n = snprintf(response, sizeof(response), i ?
            "{\"id\":%lld,\"error\":{\"code\":-1,\"message\":\"x\"}}" :
            "{\"id\":%lld,\"result\":null}", (long long)id);
        CHECK(n > 0 && (size_t)n < sizeof(response));
        maelys_json_document_t *doc = NULL; R(maelys_jsonrpc_parse(response, (size_t)n, NULL, &doc, NULL));
        maelys_jsonrpc_message_t message; maelys_jsonrpc_call_t out = {.id = -1};
        CHECK(maelys_jsonrpc_classify(doc, NULL, &message) == MAELYS_JSONRPC_PROTOCOL);
        CHECK(maelys_jsonrpc_calls_settle(calls, doc, &message, &out) == MAELYS_JSONRPC_ARGUMENT);
        CHECK(out.id == -1 && maelys_jsonrpc_calls_pending(calls) == 1u);
        R(maelys_jsonrpc_classify(doc, &codex, &message));
        R(maelys_jsonrpc_calls_settle(calls, doc, &message, &out));
        CHECK(out.id == id && out.userdata == &userdata && *(int *)out.userdata == i);
        CHECK(maelys_jsonrpc_calls_settle(calls, doc, &message, &out) == MAELYS_JSONRPC_UNKNOWN_ID);
        maelys_json_document_release(doc);
    }
    CHECK(maelys_jsonrpc_calls_release(&calls) == 0u);
}
