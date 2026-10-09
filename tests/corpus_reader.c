/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/jsonrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int equal(const maelys_json_document_t *a, maelys_json_value_t av,
                 const maelys_json_document_t *b, maelys_json_value_t bv) {
    maelys_json_type_t type = maelys_json_value_type(a, av);
    if (type != maelys_json_value_type(b, bv)) return 0;
    maelys_json_view_t x, y;
    size_t n, m;
    if (type == MAELYS_JSON_TYPE_STRING || type == MAELYS_JSON_TYPE_NUMBER) {
        if (type == MAELYS_JSON_TYPE_STRING) {
            if (maelys_json_value_string(a, av, &x) || maelys_json_value_string(b, bv, &y)) return 0;
        } else if (maelys_json_value_number_text(a, av, &x) || maelys_json_value_number_text(b, bv, &y)) return 0;
        return x.size == y.size && !memcmp(x.data, y.data, x.size);
    }
    if (type == MAELYS_JSON_TYPE_BOOLEAN) {
        int p, q;
        return !maelys_json_value_boolean(a, av, &p) && !maelys_json_value_boolean(b, bv, &q) && p == q;
    }
    if (type == MAELYS_JSON_TYPE_NULL) return 1;
    if (type == MAELYS_JSON_TYPE_ARRAY) {
        if (maelys_json_array_size(a, av, &n) || maelys_json_array_size(b, bv, &m) || n != m) return 0;
    } else if (type == MAELYS_JSON_TYPE_OBJECT) {
        if (maelys_json_object_size(a, av, &n) || maelys_json_object_size(b, bv, &m) || n != m) return 0;
    } else return 0;
    for (size_t i = 0u; i < n; ++i) {
        maelys_json_value_t u, v;
        if (type == MAELYS_JSON_TYPE_ARRAY) {
            if (maelys_json_array_get(a, av, i, &u) || maelys_json_array_get(b, bv, i, &v)) return 0;
        } else {
            if (maelys_json_object_member_at(a, av, i, &x, &u) ||
                maelys_json_object_member_at(b, bv, i, &y, &v) ||
                x.size != y.size || memcmp(x.data, y.data, x.size)) return 0;
        }
        if (!equal(a, u, b, v)) return 0;
    }
    return 1;
}

typedef struct counts {
    size_t strict_accepted, strict_invalid, codex_accepted, codex_invalid;
    size_t settled, unknown_zero, invalid_argument;
    int correlate;
} counts_t;
/* Replay each response against a fresh table reserved through its exact ID.
 * Corpus IDs are bounded to 0..6; zero is never emitted by calls_open. */
static int settle(const maelys_json_document_t *doc, const maelys_jsonrpc_message_t *message,
                  counts_t *counts) {
    if (message->kind != MAELYS_JSONRPC_RESPONSE && message->kind != MAELYS_JSONRPC_ERROR) return 0;
    int64_t id;
    if (maelys_json_value_i64(doc, message->id, &id) != MAELYS_JSON_OK || id < 0 || id > 64) return 1;
    maelys_jsonrpc_calls_t *calls = NULL;
    if (maelys_jsonrpc_calls_create(64u, &calls) != MAELYS_JSONRPC_OK) return 1;
    int bad = 0;
    int userdata = 17;
    for (int64_t expected = 1; expected <= id && !bad; ++expected) {
        int64_t actual = 0;
        bad = maelys_jsonrpc_calls_open(calls, "synthetic", 0u, 1u, &userdata, &actual) != MAELYS_JSONRPC_OK ||
            actual != expected;
    }
    maelys_jsonrpc_message_t invalid = *message; invalid.kind = MAELYS_JSONRPC_INVALID;
    maelys_jsonrpc_call_t out = {.id = -1};
    if (!bad) {
        bad = maelys_jsonrpc_calls_settle(calls, doc, &invalid, &out) != MAELYS_JSONRPC_ARGUMENT ||
            out.id != -1 || maelys_jsonrpc_calls_pending(calls) != (size_t)id;
        if (!bad) ++counts->invalid_argument;
    }
    if (!bad) {
        maelys_jsonrpc_result_t r = maelys_jsonrpc_calls_settle(calls, doc, message, &out);
        if (id) {
            bad = r != MAELYS_JSONRPC_OK || out.id != id || out.userdata != &userdata ||
                maelys_jsonrpc_calls_pending(calls) != (size_t)(id - 1);
            if (!bad) ++counts->settled;
            bad |= maelys_jsonrpc_calls_settle(calls, doc, message, &out) != MAELYS_JSONRPC_UNKNOWN_ID;
        } else {
            bad = r != MAELYS_JSONRPC_UNKNOWN_ID || out.id != -1;
            if (!bad) ++counts->unknown_zero;
        }
    }
    maelys_jsonrpc_call_t cancelled;
    while (maelys_jsonrpc_calls_cancel(calls, &cancelled) == MAELYS_JSONRPC_OK) {}
    bad |= maelys_jsonrpc_calls_release(&calls) != 0u;
    return bad;
}

static int drain(maelys_jsonrpc_reader_t *a, maelys_jsonrpc_reader_t *b, counts_t *counts) {
    for (;;) {
        maelys_json_document_t *x = NULL, *y = NULL;
        maelys_json_error_t ex, ey;
        maelys_jsonrpc_result_t rx = maelys_jsonrpc_reader_next(a, &x, &ex);
        if (rx == MAELYS_JSONRPC_AGAIN) return 0;
        maelys_jsonrpc_result_t ry = maelys_jsonrpc_reader_next(b, &y, &ey);
        int bad = rx != ry || (rx != MAELYS_JSONRPC_OK && rx != MAELYS_JSONRPC_PROTOCOL) ||
            ex.code != ey.code || ex.offset != ey.offset || ex.line != ey.line || ex.column != ey.column;
        if (!bad && rx == MAELYS_JSONRPC_OK)
            bad = !equal(x, maelys_json_document_root(x), y, maelys_json_document_root(y));
        if (!bad && rx == MAELYS_JSONRPC_OK) {
            for (int dialect = 0; dialect < 2; ++dialect) {
                maelys_jsonrpc_limits_t limits = {.dialect = (maelys_jsonrpc_dialect_t)dialect};
                maelys_jsonrpc_message_t mx, my;
                maelys_jsonrpc_result_t cx = maelys_jsonrpc_classify(x, &limits, &mx);
                maelys_jsonrpc_result_t cy = maelys_jsonrpc_classify(y, &limits, &my);
                bad |= cx != cy || mx.kind != my.kind;
                if (dialect) {
                    counts->codex_accepted += cx == MAELYS_JSONRPC_OK;
                    counts->codex_invalid += cx != MAELYS_JSONRPC_OK;
                    if (counts->correlate && cx == MAELYS_JSONRPC_OK) bad |= settle(x, &mx, counts);
                } else {
                    counts->strict_accepted += cx == MAELYS_JSONRPC_OK;
                    counts->strict_invalid += cx != MAELYS_JSONRPC_OK;
                }
            }
        }
        maelys_json_document_release(x); maelys_json_document_release(y);
        if (bad) return 1;
    }
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    FILE *file = fopen(argv[1], "rb"); if (!file) return 2;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return 2; }
    long length = ftell(file);
    if (length < 0 || length > 16L * 1048576L || fseek(file, 0, SEEK_SET)) { fclose(file); return 2; }
    size_t n = (size_t)length;
    unsigned char *bytes = malloc(n ? n : 1u);
    if (!bytes) { fclose(file); return 2; }
    if (fread(bytes, 1u, n, file) != n) { free(bytes); fclose(file); return 2; }
    fclose(file);
    counts_t counts = {0}; counts.correlate = !strcmp(argv[2], "codex-app-server");
    maelys_jsonrpc_reader_stats_t x = {0}, y = {0};
    maelys_jsonrpc_reader_options_t options = {0}; options.strip_cr = 1u;
    options.tolerate_preamble = !strcmp(argv[2], "gemini-acp");
    maelys_jsonrpc_reader_t *a = NULL, *b = NULL;
    int bad = maelys_jsonrpc_reader_create(&options, &a) != MAELYS_JSONRPC_OK ||
        maelys_jsonrpc_reader_create(&options, &b) != MAELYS_JSONRPC_OK;
    if (!bad) bad = maelys_jsonrpc_reader_feed(b, bytes, n) != MAELYS_JSONRPC_OK;
    for (size_t i = 0u; i < n && !bad; ++i)
        bad = maelys_jsonrpc_reader_feed(a, bytes + i, 1u) != MAELYS_JSONRPC_OK || drain(a, b, &counts);
    if (!bad) {
        maelys_json_document_t *unused = NULL;
        bad = maelys_jsonrpc_reader_next(b, &unused, NULL) != MAELYS_JSONRPC_AGAIN;
        maelys_json_document_release(unused);
        bad |= maelys_jsonrpc_reader_finish(a) != maelys_jsonrpc_reader_finish(b);
        maelys_jsonrpc_reader_stats(a, &x); maelys_jsonrpc_reader_stats(b, &y);
        bad |= x.bytes_seen != y.bytes_seen || x.lines_seen != y.lines_seen ||
            x.blank_lines != y.blank_lines || x.preamble_lines != y.preamble_lines ||
            x.line_overflows != y.line_overflows || x.rejected_lines != y.rejected_lines ||
            x.documents != y.documents || x.stream_started != y.stream_started ||
            x.missing_version != y.missing_version;
    }
    maelys_jsonrpc_reader_release(&a); maelys_jsonrpc_reader_release(&b); free(bytes);
    if (!bad) printf("{\"documents\":%llu,\"missing_version\":%llu,"
        "\"classified_strict\":{\"accepted\":%zu,\"invalid\":%zu},"
        "\"classified_codex\":{\"accepted\":%zu,\"invalid\":%zu},"
        "\"correlation\":{\"settled\":%zu,\"unknown_zero\":%zu,\"invalid_argument\":%zu}}\n",
        (unsigned long long)x.documents, (unsigned long long)x.missing_version,
        counts.strict_accepted, counts.strict_invalid, counts.codex_accepted, counts.codex_invalid,
        counts.settled, counts.unknown_zero, counts.invalid_argument);
    return bad ? 1 : 0;
}
