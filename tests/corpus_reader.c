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

#define MAX_CORPUS_ID 64u

typedef struct counts {
    size_t strict_accepted, strict_invalid, codex_accepted, codex_invalid;
    size_t opened, settled, duplicate_responses, unknown_zero, invalid_argument, cancelled;
    int correlate;
} counts_t;
typedef struct replay {
    maelys_jsonrpc_calls_t *calls;
    size_t maximum_id;
    unsigned char settled[MAX_CORPUS_ID];
    int userdata[MAX_CORPUS_ID];
} replay_t;

/* One table per descriptor stream, populated with every ID 1..maximum_id.
 * The test bounds corpus IDs to 64 before allocating; this is no library limit. */
static int prepare(replay_t *replay, counts_t *counts, const unsigned char *bytes, size_t n,
                   const maelys_jsonrpc_reader_options_t *options) {
    maelys_jsonrpc_reader_t *scan = NULL;
    int bad = maelys_jsonrpc_reader_create(options, &scan) != MAELYS_JSONRPC_OK;
    if (!bad) bad = maelys_jsonrpc_reader_feed(scan, bytes, n) != MAELYS_JSONRPC_OK;
    while (!bad) {
        maelys_json_document_t *doc = NULL;
        maelys_jsonrpc_result_t r = maelys_jsonrpc_reader_next(scan, &doc, NULL);
        if (r == MAELYS_JSONRPC_AGAIN) break;
        if (r == MAELYS_JSONRPC_OK) {
            maelys_jsonrpc_limits_t limits = {.dialect = MAELYS_JSONRPC_DIALECT_CODEX};
            maelys_jsonrpc_message_t message;
            bad = maelys_jsonrpc_classify(doc, &limits, &message) != MAELYS_JSONRPC_OK;
            int64_t id;
            if (!bad && maelys_json_value_i64(doc, message.id, &id) == MAELYS_JSON_OK && id > 0) {
                if (id > (int64_t)MAX_CORPUS_ID) bad = 1;
                else if ((size_t)id > replay->maximum_id) replay->maximum_id = (size_t)id;
            }
        } else if (r != MAELYS_JSONRPC_PROTOCOL) bad = 1;
        maelys_json_document_release(doc);
    }
    maelys_jsonrpc_reader_release(&scan);
    if (!bad) bad = maelys_jsonrpc_calls_create(replay->maximum_id ? replay->maximum_id : 1u,
                                               &replay->calls) != MAELYS_JSONRPC_OK;
    for (size_t i = 0u; i < replay->maximum_id && !bad; ++i) {
        int64_t actual = 0;
        replay->userdata[i] = (int)i + 1;
        bad = maelys_jsonrpc_calls_open(replay->calls, "synthetic", 0u, 1u,
            &replay->userdata[i], &actual) != MAELYS_JSONRPC_OK || actual != (int64_t)i + 1;
        if (!bad) ++counts->opened;
    }
    return bad;
}

/* The capture client did not use calls. Its id 0 is UNKNOWN_ID because this
 * table never emitted it, not because zero has a special protocol meaning.
 * test_calls pairs this assertion with the first open returning 1, never 0. */
static int settle(replay_t *replay, const maelys_json_document_t *doc,
                  const maelys_jsonrpc_message_t *message, counts_t *counts) {
    if (message->kind != MAELYS_JSONRPC_RESPONSE && message->kind != MAELYS_JSONRPC_ERROR) return 0;
    int64_t id;
    if (maelys_json_value_i64(doc, message->id, &id) != MAELYS_JSON_OK ||
        id < 0 || (uint64_t)id > replay->maximum_id) return 1;
    size_t pending = maelys_jsonrpc_calls_pending(replay->calls);
    maelys_jsonrpc_message_t invalid = *message; invalid.kind = MAELYS_JSONRPC_INVALID;
    maelys_jsonrpc_call_t out = {.id = -1};
    if (maelys_jsonrpc_calls_settle(replay->calls, doc, &invalid, &out) != MAELYS_JSONRPC_ARGUMENT ||
        out.id != -1 || maelys_jsonrpc_calls_pending(replay->calls) != pending) return 1;
    ++counts->invalid_argument;
    maelys_jsonrpc_result_t r = maelys_jsonrpc_calls_settle(replay->calls, doc, message, &out);
    if (!id) {
        if (r != MAELYS_JSONRPC_UNKNOWN_ID || out.id != -1 ||
            maelys_jsonrpc_calls_pending(replay->calls) != pending) return 1;
        ++counts->unknown_zero;
    } else {
        size_t index = (size_t)id - 1u;
        if (replay->settled[index]) {
            if (r != MAELYS_JSONRPC_UNKNOWN_ID || out.id != -1 ||
                maelys_jsonrpc_calls_pending(replay->calls) != pending) return 1;
            ++counts->duplicate_responses;
        } else {
            if (r != MAELYS_JSONRPC_OK || out.id != id || out.userdata != &replay->userdata[index] ||
                *(int *)out.userdata != (int)id || strcmp(out.method, "synthetic") ||
                !pending || maelys_jsonrpc_calls_pending(replay->calls) != pending - 1u) return 1;
            replay->settled[index] = 1u;
            ++counts->settled;
        }
    }
    return 0;
}

static int cleanup(replay_t *replay, counts_t *counts) {
    if (!replay->calls) return 0;
    maelys_jsonrpc_call_t out;
    maelys_jsonrpc_result_t r;
    int bad = 0;
    while ((r = maelys_jsonrpc_calls_cancel(replay->calls, &out)) == MAELYS_JSONRPC_OK) {
        if (out.id < 1 || (uint64_t)out.id > replay->maximum_id) bad = 1;
        else {
            size_t index = (size_t)out.id - 1u;
            if (replay->settled[index] || out.userdata != &replay->userdata[index] ||
                *(int *)out.userdata != (int)out.id) bad = 1;
            replay->settled[index] = 1u;
        }
        ++counts->cancelled;
    }
    bad |= r != MAELYS_JSONRPC_AGAIN || maelys_jsonrpc_calls_pending(replay->calls) != 0u;
    bad |= maelys_jsonrpc_calls_release(&replay->calls) != 0u;
    bad |= counts->opened != counts->settled + counts->cancelled;
    return bad;
}

static int drain(maelys_jsonrpc_reader_t *a, maelys_jsonrpc_reader_t *b, counts_t *counts, replay_t *replay) {
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
                    if (counts->correlate && cx == MAELYS_JSONRPC_OK) bad |= settle(replay, x, &mx, counts);
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
    replay_t replay = {0};
    counts_t counts = {0}; counts.correlate = !strcmp(argv[2], "codex-app-server");
    maelys_jsonrpc_reader_stats_t x = {0}, y = {0};
    maelys_jsonrpc_reader_options_t options = {0}; options.strip_cr = 1u;
    options.tolerate_preamble = !strcmp(argv[2], "gemini-acp");
    maelys_jsonrpc_reader_t *a = NULL, *b = NULL;
    int bad = maelys_jsonrpc_reader_create(&options, &a) != MAELYS_JSONRPC_OK ||
        maelys_jsonrpc_reader_create(&options, &b) != MAELYS_JSONRPC_OK;
    if (!bad && counts.correlate) bad = prepare(&replay, &counts, bytes, n, &options);
    if (!bad) bad = maelys_jsonrpc_reader_feed(b, bytes, n) != MAELYS_JSONRPC_OK;
    for (size_t i = 0u; i < n && !bad; ++i)
        bad = maelys_jsonrpc_reader_feed(a, bytes + i, 1u) != MAELYS_JSONRPC_OK || drain(a, b, &counts, &replay);
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
    if (counts.correlate) bad |= cleanup(&replay, &counts);
    maelys_jsonrpc_reader_release(&a); maelys_jsonrpc_reader_release(&b); free(bytes);
    if (!bad) printf("{\"documents\":%llu,\"missing_version\":%llu,"
        "\"classified_strict\":{\"accepted\":%zu,\"invalid\":%zu},"
        "\"classified_codex\":{\"accepted\":%zu,\"invalid\":%zu},"
        "\"correlation\":{\"opened\":%zu,\"settled\":%zu,\"duplicate_responses\":%zu,"
        "\"unknown_zero\":%zu,\"invalid_argument\":%zu,\"cancelled\":%zu,\"release_lost\":0}}\n",
        (unsigned long long)x.documents, (unsigned long long)x.missing_version,
        counts.strict_accepted, counts.strict_invalid, counts.codex_accepted, counts.codex_invalid,
        counts.opened, counts.settled, counts.duplicate_responses,
        counts.unknown_zero, counts.invalid_argument, counts.cancelled);
    return bad ? 1 : 0;
}
