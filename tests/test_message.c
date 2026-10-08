/* SPDX-License-Identifier: MPL-2.0 */
#include "framework.h"

static maelys_json_document_t *parse(const char *text) {
    maelys_json_document_t *doc = NULL; R(maelys_jsonrpc_parse(text, strlen(text), NULL, &doc, NULL)); return doc;
}

static void classify_cases(void) {
    const struct { const char *json; maelys_jsonrpc_kind_t kind; } cases[] = {
        {"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"a\",\"params\":{}}", MAELYS_JSONRPC_REQUEST},
        {"{\"jsonrpc\":\"2.0\",\"method\":\"a\"}", MAELYS_JSONRPC_NOTIFICATION},
        {"{\"jsonrpc\":\"2.0\",\"id\":null,\"result\":null}", MAELYS_JSONRPC_RESPONSE},
        {"{\"jsonrpc\":\"2.0\",\"id\":\"xyz\",\"error\":{\"code\":-32601,\"message\":\"error\"}}", MAELYS_JSONRPC_ERROR},
        {"{}", MAELYS_JSONRPC_INVALID}, {"[]", MAELYS_JSONRPC_INVALID}, {"1", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":2,\"id\":1,\"result\":0}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":\"1.0\",\"id\":1,\"result\":0}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":\"2.0\",\"id\":1.0,\"result\":0}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":\"2.0\",\"id\":true,\"method\":\"a\"}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":\"2.0\",\"id\":1}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":\"2.0\",\"result\":0}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":0,\"error\":{}}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":\"2.0\",\"method\":\"\"}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":\"2.0\",\"method\":1}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":\"2.0\",\"method\":\"a\",\"params\":null}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":\"2.0\",\"method\":\"a\",\"result\":0}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":1.5,\"message\":\"error\"}}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":1,\"message\":4}}", MAELYS_JSONRPC_INVALID},
        {"{\"jsonrpc\":\"2.0\",\"id\":18446744073709551616,\"result\":1.5e3}", MAELYS_JSONRPC_RESPONSE},
    };
    for (size_t i = 0u; i < sizeof cases / sizeof cases[0]; ++i) {
        maelys_json_document_t *doc = parse(cases[i].json); maelys_jsonrpc_message_t message;
        maelys_jsonrpc_result_t r = maelys_jsonrpc_classify(doc, NULL, &message);
        CHECK(r == (cases[i].kind == MAELYS_JSONRPC_INVALID ? MAELYS_JSONRPC_PROTOCOL : MAELYS_JSONRPC_OK));
        CHECK(message.kind == cases[i].kind);
        if (message.kind == MAELYS_JSONRPC_NOTIFICATION) CHECK(message.id == MAELYS_JSON_VALUE_NONE);
        maelys_json_document_release(doc);
    }
    maelys_json_document_t *doc = parse("{\"jsonrpc\":\"2.0\",\"method\":\"abcd\"}");
    maelys_jsonrpc_limits_t limits = {3u, 1u}; maelys_jsonrpc_message_t message;
    CHECK(maelys_jsonrpc_classify(doc, &limits, &message) == MAELYS_JSONRPC_PROTOCOL);
    maelys_json_document_release(doc);
}

static void writer_cases(void) {
    maelys_json_limits_t limits = {1048576u, 64u, 65536u};
    maelys_json_writer_t *writer = NULL; char *out = NULL; size_t n = 0u;
    J(maelys_json_writer_create(MAELYS_JSON_PROFILE_RFC8259, &limits, MAELYS_JSON_WRITER_FINAL_NEWLINE, &writer));
    char method[258]; memset(method, 'x', 257u); method[257] = '\0';
    CHECK(maelys_jsonrpc_begin_request(writer, 1, method) == MAELYS_JSONRPC_LIMIT);
    CHECK(maelys_jsonrpc_begin_request(writer, 1, "\xc0\xaf") == MAELYS_JSONRPC_ARGUMENT);
    R(maelys_jsonrpc_begin_request(writer, 1, "quote\"\nmethod"));
    J(maelys_json_writer_key_cstr(writer, "params")); J(maelys_json_writer_array_begin(writer));
    J(maelys_json_writer_i64(writer, 3)); J(maelys_json_writer_array_end(writer));
    R(maelys_jsonrpc_end(writer, &out, &n));
    CHECK(!strcmp(out, "{\"id\":1,\"jsonrpc\":\"2.0\",\"method\":\"quote\\\"\\nmethod\",\"params\":[3]}\n"));
    free(out); maelys_json_writer_release(writer);
    maelys_json_document_t *doc = parse("{\"jsonrpc\":\"2.0\",\"id\":\"a\\\"b\",\"method\":\"x\"}");
    maelys_jsonrpc_message_t message; R(maelys_jsonrpc_classify(doc, NULL, &message));
    J(maelys_json_writer_create(MAELYS_JSON_PROFILE_RFC8259, &limits, MAELYS_JSON_WRITER_FINAL_NEWLINE, &writer));
    R(maelys_jsonrpc_write_error(writer, doc, message.id, -1, "line\n\x01"));
    J(maelys_json_writer_finish(writer, &out, &n));
    CHECK(!strcmp(out, "{\"error\":{\"code\":-1,\"message\":\"line\\n\\u0001\"},\"id\":\"a\\\"b\",\"jsonrpc\":\"2.0\"}\n"));
    for (size_t i = 0u; i + 1u < n; ++i) CHECK((unsigned char)out[i] >= 0x20u);
    free(out); maelys_json_writer_release(writer); maelys_json_document_release(doc);
    J(maelys_json_writer_create(MAELYS_JSON_PROFILE_RFC8259, NULL, 0u, &writer));
    R(maelys_jsonrpc_begin_notification(writer, "hello")); R(maelys_jsonrpc_end(writer, &out, &n));
    CHECK(!strcmp(out, "{\"jsonrpc\":\"2.0\",\"method\":\"hello\"}"));
    maelys_json_document_t *canonical_doc = parse(out); int canonical = 0;
    J(maelys_json_document_is_canonical(canonical_doc, 0u, &canonical)); CHECK(canonical);
    maelys_json_document_release(canonical_doc);
    free(out); maelys_json_writer_release(writer);
}

/* A relay is a byte fixed point. Fractions/exponents remain outside the
 * canonical domain; integer accessors still reject those lexemes. */
static void relay_twice(const char *input, const char *expected, const char *long_lexeme) {
    maelys_json_document_t *doc = parse(input);
    char *first = NULL; size_t first_n = 0u;
    for (size_t pass = 0u; pass < 2u; ++pass) {
        maelys_jsonrpc_message_t message; R(maelys_jsonrpc_classify(doc, NULL, &message));
        CHECK(message.kind == MAELYS_JSONRPC_RESPONSE);
        int64_t integer;
        CHECK(maelys_json_value_i64(doc, message.result, &integer) == MAELYS_JSON_ERR_NOT_INTEGER ||
            maelys_json_value_type(doc, message.result) == MAELYS_JSON_TYPE_OBJECT);
        maelys_json_writer_t *writer = NULL; char *out = NULL; size_t n = 0u;
        J(maelys_json_writer_create(MAELYS_JSON_PROFILE_RFC8259, NULL, 0u, &writer));
        if (long_lexeme) {
            CHECK(strlen(long_lexeme) > MAELYS_JSON_MAXIMUM_NUMBER_TEXT);
            CHECK(maelys_json_writer_number_text(writer, long_lexeme, strlen(long_lexeme)) == MAELYS_JSON_ERR_ARGUMENT);
            J(maelys_json_writer_status(writer));
        }
        R(maelys_jsonrpc_begin_response(writer, doc, message.id));
        J(maelys_json_writer_key_cstr(writer, "result"));
        J(maelys_json_writer_value(writer, doc, message.result));
        R(maelys_jsonrpc_end(writer, &out, &n));
        CHECK(!strcmp(out, expected));
        maelys_json_document_release(doc); doc = parse(out);
        int canonical = 1;
        J(maelys_json_document_is_canonical(doc, 0u, &canonical)); CHECK(!canonical);
        if (!pass) { first = out; first_n = n; out = NULL; }
        else { CHECK(n == first_n); CHECK(!memcmp(first, out, n)); }
        free(out); maelys_json_writer_release(writer);
    }
    free(first); maelys_json_document_release(doc);
    /* Relay a root while removing an extension: numbers take the same exact
     * copy path, including lexemes too long for the public number_text call. */
    doc = parse(input);
    maelys_json_writer_t *writer = NULL; char *out = NULL; size_t n = 0u;
    J(maelys_json_writer_create(MAELYS_JSON_PROFILE_RFC8259, NULL, 0u, &writer));
    const char *excluded[] = {"drop"};
    J(maelys_json_writer_object_begin_except(writer, doc, maelys_json_document_root(doc), excluded, 1u));
    R(maelys_jsonrpc_end(writer, &out, &n)); CHECK(!strcmp(out, expected));
    free(out); maelys_json_writer_release(writer); maelys_json_document_release(doc);
}

static void relay_case(void) {
    relay_twice("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"z\":1.5e3,\"a\":2},\"drop\":true}",
        "{\"id\":1,\"jsonrpc\":\"2.0\",\"result\":{\"a\":2,\"z\":1.5e3}}", NULL);
    relay_twice("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":1e400,\"drop\":true}",
        "{\"id\":1,\"jsonrpc\":\"2.0\",\"result\":1e400}", NULL);
    char fraction[71], input[160], expected[160];
    fraction[0] = '0'; fraction[1] = '.'; memset(fraction + 2, '1', 68u); fraction[70] = '\0';
    (void)snprintf(input, sizeof input, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":%s,\"drop\":true}", fraction);
    (void)snprintf(expected, sizeof expected, "{\"id\":1,\"jsonrpc\":\"2.0\",\"result\":%s}", fraction);
    relay_twice(input, expected, fraction);
    maelys_json_document_t *doc = parse("{\"jsonrpc\":\"2.0\",\"id\":18446744073709551616,\"result\":null}");
    maelys_jsonrpc_message_t message; R(maelys_jsonrpc_classify(doc, NULL, &message));
    maelys_json_writer_t *writer = NULL;
    J(maelys_json_writer_create(MAELYS_JSON_PROFILE_RFC8259, NULL, 0u, &writer));
    CHECK(maelys_jsonrpc_begin_response(writer, doc, message.id) == MAELYS_JSONRPC_PROTOCOL);
    CHECK(maelys_json_writer_status(writer) == MAELYS_JSON_ERR_RANGE);
    maelys_json_writer_release(writer); maelys_json_document_release(doc);
}

void test_message(void) {
    classify_cases(); writer_cases(); relay_case();
    maelys_json_document_t *doc = parse("{\"code\":-2,\"message\":\"ééerror\"}");
    int64_t code = 0; char excerpt[4];
    R(maelys_jsonrpc_error_read(doc, maelys_json_document_root(doc), &code, excerpt, sizeof excerpt));
    CHECK(code == -2 && !strcmp(excerpt, "é"));
    R(maelys_jsonrpc_error_read(doc, maelys_json_document_root(doc), &code, NULL, 0u));
    CHECK(maelys_jsonrpc_error_read(doc, maelys_json_document_root(doc), &code, NULL, 1u) == MAELYS_JSONRPC_ARGUMENT);
    maelys_json_document_release(doc);
}
