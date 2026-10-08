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

static int drain(maelys_jsonrpc_reader_t *a, maelys_jsonrpc_reader_t *b) {
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
    maelys_jsonrpc_reader_options_t options = {0}; options.strip_cr = 1u;
    options.tolerate_preamble = !strcmp(argv[2], "gemini-acp");
    maelys_jsonrpc_reader_t *a = NULL, *b = NULL;
    int bad = maelys_jsonrpc_reader_create(&options, &a) != MAELYS_JSONRPC_OK ||
        maelys_jsonrpc_reader_create(&options, &b) != MAELYS_JSONRPC_OK;
    if (!bad) bad = maelys_jsonrpc_reader_feed(b, bytes, n) != MAELYS_JSONRPC_OK;
    for (size_t i = 0u; i < n && !bad; ++i)
        bad = maelys_jsonrpc_reader_feed(a, bytes + i, 1u) != MAELYS_JSONRPC_OK || drain(a, b);
    if (!bad) {
        maelys_json_document_t *unused = NULL;
        bad = maelys_jsonrpc_reader_next(b, &unused, NULL) != MAELYS_JSONRPC_AGAIN;
        maelys_json_document_release(unused);
        bad |= maelys_jsonrpc_reader_finish(a) != maelys_jsonrpc_reader_finish(b);
        maelys_jsonrpc_reader_stats_t x, y;
        maelys_jsonrpc_reader_stats(a, &x); maelys_jsonrpc_reader_stats(b, &y);
        bad |= x.bytes_seen != y.bytes_seen || x.lines_seen != y.lines_seen ||
            x.blank_lines != y.blank_lines || x.preamble_lines != y.preamble_lines ||
            x.line_overflows != y.line_overflows || x.rejected_lines != y.rejected_lines ||
            x.documents != y.documents || x.stream_started != y.stream_started;
    }
    maelys_jsonrpc_reader_release(&a); maelys_jsonrpc_reader_release(&b); free(bytes);
    return bad ? 1 : 0;
}
