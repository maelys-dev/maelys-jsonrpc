/* SPDX-License-Identifier: MPL-2.0 */
#include "framework.h"

static void stats_equal(const maelys_jsonrpc_reader_stats_t *a, const maelys_jsonrpc_reader_stats_t *b) {
    CHECK(a->bytes_seen == b->bytes_seen); CHECK(a->lines_seen == b->lines_seen);
    CHECK(a->blank_lines == b->blank_lines); CHECK(a->preamble_lines == b->preamble_lines);
    CHECK(a->line_overflows == b->line_overflows); CHECK(a->rejected_lines == b->rejected_lines);
    CHECK(a->documents == b->documents); CHECK(a->stream_started == b->stream_started);
}

static void chunk_invariance(void) {
    const char stream[] = "Starting server…\n\n{\"jsonrpc\":\"2.0\",\"method\":\"a\"}\r\n"
        "{\"id\":1,\"id\":2}\n{broken}\n{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"b\":2,\"a\":1}}\n";
    maelys_jsonrpc_reader_stats_t baseline = {0};
    for (size_t chunk = 1u; chunk <= sizeof stream; ++chunk) {
        maelys_jsonrpc_reader_options_t options = {0};
        options.strip_cr = 1u; options.tolerate_preamble = 1u;
        maelys_jsonrpc_reader_t *reader = NULL;
        R(maelys_jsonrpc_reader_create(&options, &reader));
        size_t accepted = 0u, rejected = 0u;
        for (size_t offset = 0u; offset < sizeof stream - 1u;) {
            size_t n = chunk;
            if (n > sizeof stream - 1u - offset) n = sizeof stream - 1u - offset;
            R(maelys_jsonrpc_reader_feed(reader, stream + offset, n));
            offset += n;
            for (;;) {
                maelys_json_document_t *doc = NULL;
                maelys_json_error_t why;
                maelys_jsonrpc_result_t r = maelys_jsonrpc_reader_next(reader, &doc, &why);
                if (r == MAELYS_JSONRPC_AGAIN) break;
                if (r == MAELYS_JSONRPC_PROTOCOL) {
                    CHECK(why.code == (rejected == 0u ? MAELYS_JSON_ERR_DUPLICATE_KEY : MAELYS_JSON_ERR_SYNTAX));
                    CHECK(!doc); ++rejected; continue;
                }
                CHECK(r == MAELYS_JSONRPC_OK);
                maelys_jsonrpc_message_t message;
                R(maelys_jsonrpc_classify(doc, NULL, &message));
                CHECK(message.kind == (accepted == 0u ? MAELYS_JSONRPC_NOTIFICATION : MAELYS_JSONRPC_RESPONSE));
                maelys_json_document_release(doc); ++accepted;
            }
        }
        CHECK(accepted == 2u && rejected == 2u);
        R(maelys_jsonrpc_reader_finish(reader));
        maelys_jsonrpc_reader_stats_t stats;
        maelys_jsonrpc_reader_stats(reader, &stats);
        CHECK(stats.preamble_lines == 1u && stats.blank_lines == 1u);
        CHECK(stats.bytes_seen == sizeof stream - 1u);
        if (chunk == 1u) baseline = stats;
        else stats_equal(&baseline, &stats);
        maelys_jsonrpc_reader_release(&reader); CHECK(!reader);
    }
}

static void limits_and_recovery(void) {
    maelys_jsonrpc_reader_t *reader = NULL;
    R(maelys_jsonrpc_reader_create(NULL, &reader));
    size_t n = MAELYS_JSONRPC_DEFAULT_MAXIMUM_BYTES + 1u;
    char *large = malloc(n); CHECK(large); memset(large, 'x', n);
    for (size_t i = 0u; i < 10u; ++i) {
        if (i == 4u) { R(maelys_jsonrpc_reader_feed(reader, large, n)); R(maelys_jsonrpc_reader_feed(reader, "\n", 1u)); }
        else R(maelys_jsonrpc_reader_feed(reader, "{}\n", 3u));
    }
    free(large);
    size_t accepted = 0u, rejected = 0u;
    for (;;) {
        maelys_json_document_t *doc = NULL;
        maelys_json_error_t why;
        maelys_jsonrpc_result_t r = maelys_jsonrpc_reader_next(reader, &doc, &why);
        if (r == MAELYS_JSONRPC_AGAIN) break;
        if (r == MAELYS_JSONRPC_PROTOCOL) { CHECK(why.code == MAELYS_JSON_ERR_LIMIT); ++rejected; }
        else { CHECK(r == MAELYS_JSONRPC_OK); ++accepted; maelys_json_document_release(doc); }
    }
    CHECK(accepted == 9u && rejected == 1u);
    maelys_jsonrpc_reader_stats_t stats;
    maelys_jsonrpc_reader_stats(reader, &stats);
    CHECK(stats.line_overflows == 1u && stats.rejected_lines == 1u);
    R(maelys_jsonrpc_reader_finish(reader));
    maelys_jsonrpc_reader_release(&reader);

    maelys_jsonrpc_reader_options_t options = {0}; options.limits.maximum_bytes = 2u; options.strip_cr = 1u;
    R(maelys_jsonrpc_reader_create(&options, &reader));
    R(maelys_jsonrpc_reader_feed(reader, "{}\r", 3u));
    CHECK(maelys_jsonrpc_reader_finish(reader) == MAELYS_JSONRPC_PROTOCOL);
    const char tail[] = "\n{}x\n{}\rX\n{}\n";
    R(maelys_jsonrpc_reader_feed(reader, tail, sizeof tail - 1u));
    for (size_t i = 0u; i < 4u; ++i) {
        maelys_json_document_t *doc = NULL;
        maelys_json_error_t why;
        maelys_jsonrpc_result_t r = maelys_jsonrpc_reader_next(reader, &doc, &why);
        CHECK(r == (i == 1u || i == 2u ? MAELYS_JSONRPC_PROTOCOL : MAELYS_JSONRPC_OK));
        if (r == MAELYS_JSONRPC_PROTOCOL) CHECK(why.code == MAELYS_JSON_ERR_LIMIT);
        maelys_json_document_release(doc);
    }
    R(maelys_jsonrpc_reader_finish(reader)); maelys_jsonrpc_reader_release(&reader);
}

static void rejected_encodings(void) {
    const unsigned char bad_utf8[] = {'{','"','s','"',':','"',0xc0,0xaf,'"','}','\n'};
    const char nul[] = "{\"s\":\"\\u0000\"}\n";
    const unsigned char bom[] = {0xef,0xbb,0xbf,'{','}','\n'};
    maelys_jsonrpc_reader_options_t options = {0}; options.tolerate_preamble = 1u;
    maelys_jsonrpc_reader_t *reader = NULL; R(maelys_jsonrpc_reader_create(&options, &reader));
    R(maelys_jsonrpc_reader_feed(reader, bom, sizeof bom));
    R(maelys_jsonrpc_reader_feed(reader, bad_utf8, sizeof bad_utf8));
    R(maelys_jsonrpc_reader_feed(reader, nul, sizeof nul - 1u));
    R(maelys_jsonrpc_reader_feed(reader, "{}\n", 3u));
    const maelys_json_result_t expected[] = {MAELYS_JSON_ERR_SYNTAX, MAELYS_JSON_ERR_UTF8, MAELYS_JSON_ERR_SYNTAX};
    for (size_t i = 0u; i < 3u; ++i) {
        maelys_json_document_t *doc = NULL; maelys_json_error_t why;
        CHECK(maelys_jsonrpc_reader_next(reader, &doc, &why) == MAELYS_JSONRPC_PROTOCOL);
        CHECK(why.code == expected[i]); CHECK(!doc);
    }
    maelys_json_document_t *doc = NULL;
    R(maelys_jsonrpc_reader_next(reader, &doc, NULL)); maelys_json_document_release(doc);
    R(maelys_jsonrpc_reader_finish(reader)); maelys_jsonrpc_reader_release(&reader);
}

static void validation_bounds(void) {
    maelys_json_document_t *doc = NULL; maelys_json_error_t why;
    maelys_json_limits_t limits = {0u, 1u, 0u};
    CHECK(maelys_jsonrpc_parse("[[0]]", 5u, &limits, &doc, &why) == MAELYS_JSONRPC_PROTOCOL);
    CHECK(why.code == MAELYS_JSON_ERR_LIMIT && !doc);
    limits = (maelys_json_limits_t){0u, 0u, 2u};
    CHECK(maelys_jsonrpc_parse("[1,2]", 5u, &limits, &doc, &why) == MAELYS_JSONRPC_PROTOCOL);
    CHECK(why.code == MAELYS_JSON_ERR_LIMIT);
    limits.maximum_depth = MAELYS_JSON_MAXIMUM_DEPTH + 1u;
    CHECK(maelys_jsonrpc_parse("{}", 2u, &limits, &doc, &why) == MAELYS_JSONRPC_ARGUMENT);
    CHECK(maelys_jsonrpc_parse(NULL, 1u, NULL, &doc, &why) == MAELYS_JSONRPC_ARGUMENT);
    CHECK(maelys_jsonrpc_parse(NULL, 0u, NULL, &doc, &why) == MAELYS_JSONRPC_PROTOCOL);
}

static void many_invalid(void) {
    maelys_jsonrpc_reader_t *reader = NULL; R(maelys_jsonrpc_reader_create(NULL, &reader));
    for (size_t i = 0u; i < 10000u; ++i) {
        R(maelys_jsonrpc_reader_feed(reader, "{}\n", 3u));
        maelys_json_document_t *doc = NULL; R(maelys_jsonrpc_reader_next(reader, &doc, NULL));
        maelys_jsonrpc_message_t message;
        CHECK(maelys_jsonrpc_classify(doc, NULL, &message) == MAELYS_JSONRPC_PROTOCOL);
        CHECK(message.kind == MAELYS_JSONRPC_INVALID);
        maelys_json_document_release(doc);
    }
    maelys_jsonrpc_reader_stats_t stats; maelys_jsonrpc_reader_stats(reader, &stats);
    CHECK(stats.documents == 10000u && stats.rejected_lines == 0u);
    R(maelys_jsonrpc_reader_finish(reader)); maelys_jsonrpc_reader_release(&reader);
}

void test_reader(void) {
    chunk_invariance(); limits_and_recovery(); rejected_encodings(); validation_bounds(); many_invalid();
    maelys_jsonrpc_reader_t *reader = NULL; R(maelys_jsonrpc_reader_create(NULL, &reader));
    CHECK(maelys_jsonrpc_reader_feed(reader, NULL, 1u) == MAELYS_JSONRPC_ARGUMENT);
    const char lines[] = "Starting server\n\n{}\r\n";
    R(maelys_jsonrpc_reader_feed(reader, lines, sizeof lines - 1u));
    maelys_json_document_t *doc = NULL;
    CHECK(maelys_jsonrpc_reader_next(reader, &doc, NULL) == MAELYS_JSONRPC_PROTOCOL);
    R(maelys_jsonrpc_reader_next(reader, &doc, NULL)); maelys_json_document_release(doc);
    R(maelys_jsonrpc_reader_feed(reader, "\r", 1u));
    CHECK(maelys_jsonrpc_reader_finish(reader) == MAELYS_JSONRPC_PROTOCOL);
    maelys_jsonrpc_reader_release(&reader); maelys_jsonrpc_reader_release(&reader);
    CHECK(maelys_jsonrpc_reader_finish(NULL) == MAELYS_JSONRPC_ARGUMENT);
}
