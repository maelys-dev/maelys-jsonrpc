/* SPDX-License-Identifier: MPL-2.0 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct line {
    struct line *next;
    size_t size;
    unsigned overflow;
    unsigned char bytes[];
} line_t;

struct maelys_jsonrpc_reader {
    maelys_jsonrpc_reader_options_t options;
    maelys_jsonrpc_reader_stats_t stats;
    unsigned char *partial;
    size_t length, capacity;
    unsigned dropping, failed;
    line_t *head, *tail;
};

static void add(uint64_t *value, uint64_t n) {
    *value = UINT64_MAX - *value < n ? UINT64_MAX : *value + n;
}

static maelys_jsonrpc_result_t normalize(const maelys_json_limits_t *in, maelys_json_limits_t *out) {
    *out = in ? *in : (maelys_json_limits_t){0u, 0u, 0u};
    if (!out->maximum_bytes) out->maximum_bytes = MAELYS_JSONRPC_DEFAULT_MAXIMUM_BYTES;
    if (!out->maximum_depth) out->maximum_depth = MAELYS_JSONRPC_DEFAULT_MAXIMUM_DEPTH;
    if (!out->maximum_tokens) out->maximum_tokens = MAELYS_JSONRPC_DEFAULT_MAXIMUM_TOKENS;
    if (out->maximum_depth > MAELYS_JSON_MAXIMUM_DEPTH ||
        out->maximum_bytes > SIZE_MAX - sizeof(line_t) - 1u) return MAELYS_JSONRPC_ARGUMENT;
    return MAELYS_JSONRPC_OK;
}

maelys_jsonrpc_result_t maelys_jsonrpc_parse(const void *bytes, size_t n,
    const maelys_json_limits_t *limits, maelys_json_document_t **out, maelys_json_error_t *why) {
    maelys_json_limits_t normalized;
    maelys_jsonrpc_result_t r = normalize(limits, &normalized);
    if (!out || (!bytes && n) || r != MAELYS_JSONRPC_OK) {
        if (why) *why = (maelys_json_error_t){MAELYS_JSON_ERR_ARGUMENT, 0u, 1u, 1u};
        return MAELYS_JSONRPC_ARGUMENT;
    }
    maelys_json_result_t json = maelys_json_document_parse(bytes, n,
        MAELYS_JSON_PROFILE_RFC8259, &normalized, out, why);
    if (json == MAELYS_JSON_ERR_LIMIT) return MAELYS_JSONRPC_PROTOCOL;
    return maelys_jsonrpc_from_json(json);
}

maelys_jsonrpc_result_t maelys_jsonrpc_reader_create(
    const maelys_jsonrpc_reader_options_t *options, maelys_jsonrpc_reader_t **out) {
    if (!out) return MAELYS_JSONRPC_ARGUMENT;
    maelys_jsonrpc_reader_options_t normalized = {0};
    if (options) normalized = *options;
    maelys_jsonrpc_result_t r = normalize(options ? &options->limits : NULL, &normalized.limits);
    if (r != MAELYS_JSONRPC_OK) return r;
    maelys_jsonrpc_reader_t *reader = calloc(1u, sizeof(*reader));
    if (!reader) return MAELYS_JSONRPC_MEMORY;
    reader->options = normalized;
    *out = reader;
    return MAELYS_JSONRPC_OK;
}

static maelys_jsonrpc_result_t complete(maelys_jsonrpc_reader_t *reader) {
    size_t n = reader->length;
    if (reader->options.strip_cr && n && reader->partial[n - 1u] == '\r') --n;
    add(&reader->stats.lines_seen, 1u);
    unsigned overflow = reader->dropping || n > reader->options.limits.maximum_bytes;
    if (!n && !overflow) {
        add(&reader->stats.blank_lines, 1u);
        reader->length = 0u;
        return MAELYS_JSONRPC_OK;
    }
    if (overflow) { n = 0u; add(&reader->stats.line_overflows, 1u); }
    line_t *line = malloc(sizeof(*line) + n);
    if (!line) return MAELYS_JSONRPC_MEMORY;
    line->next = NULL;
    line->size = n;
    line->overflow = overflow;
    if (n) memcpy(line->bytes, reader->partial, n);
    if (reader->tail) reader->tail->next = line;
    else reader->head = line;
    reader->tail = line;
    reader->length = 0u;
    reader->dropping = 0u;
    return MAELYS_JSONRPC_OK;
}

static maelys_jsonrpc_result_t append(maelys_jsonrpc_reader_t *reader, unsigned char byte) {
    size_t ceiling = reader->options.limits.maximum_bytes;
    /* Retain a single optional CR beyond the content bound until LF arrives. */
    if (reader->length >= ceiling &&
        !(reader->options.strip_cr && reader->length == ceiling && byte == '\r')) {
        reader->length = 0u;
        reader->dropping = 1u;
        return MAELYS_JSONRPC_OK;
    }
    if (reader->length == reader->capacity) {
        size_t cap = reader->capacity;
        cap = !cap ? 256u : cap > (ceiling + 1u) / 2u ? ceiling + 1u : cap * 2u;
        if (cap > ceiling + 1u) cap = ceiling + 1u;
        unsigned char *grown = realloc(reader->partial, cap);
        if (!grown) return MAELYS_JSONRPC_MEMORY;
        reader->partial = grown;
        reader->capacity = cap;
    }
    reader->partial[reader->length++] = byte;
    return MAELYS_JSONRPC_OK;
}

maelys_jsonrpc_result_t maelys_jsonrpc_reader_feed(maelys_jsonrpc_reader_t *reader,
    const void *bytes, size_t n) {
    if (!reader || (!bytes && n)) return MAELYS_JSONRPC_ARGUMENT;
    if (reader->failed) return MAELYS_JSONRPC_STATE;
    const unsigned char *input = bytes;
    add(&reader->stats.bytes_seen, (uint64_t)n);
    for (size_t i = 0u; i < n; ++i) {
        maelys_jsonrpc_result_t r = MAELYS_JSONRPC_OK;
        if (input[i] == '\n') r = complete(reader);
        else if (!reader->dropping) r = append(reader, input[i]);
        if (r != MAELYS_JSONRPC_OK) { reader->failed = 1u; return r; }
    }
    return MAELYS_JSONRPC_OK;
}

static int preamble(const line_t *line) {
    /* Never hide a BOM behind preamble tolerance. */
    if (line->size >= 3u && line->bytes[0] == 0xefu && line->bytes[1] == 0xbbu &&
        line->bytes[2] == 0xbfu) return 0;
    size_t i = 0u;
    while (i < line->size && (line->bytes[i] == ' ' || line->bytes[i] == '\t' ||
        line->bytes[i] == '\r')) ++i;
    return i == line->size || line->bytes[i] != '{';
}

maelys_jsonrpc_result_t maelys_jsonrpc_reader_next(maelys_jsonrpc_reader_t *reader,
    maelys_json_document_t **out, maelys_json_error_t *why) {
    if (why) *why = (maelys_json_error_t){MAELYS_JSON_OK, 0u, 1u, 1u};
    if (!reader || !out) {
        if (why) why->code = MAELYS_JSON_ERR_ARGUMENT;
        return MAELYS_JSONRPC_ARGUMENT;
    }
    if (reader->failed) return MAELYS_JSONRPC_STATE;
    while (reader->head) {
        line_t *line = reader->head;
        reader->head = line->next;
        if (!reader->head) reader->tail = NULL;
        if (line->overflow) {
            add(&reader->stats.rejected_lines, 1u);
            if (why) why->code = MAELYS_JSON_ERR_LIMIT;
            free(line);
            return MAELYS_JSONRPC_PROTOCOL;
        }
        if (reader->options.tolerate_preamble && !reader->stats.stream_started && preamble(line)) {
            add(&reader->stats.preamble_lines, 1u);
            free(line);
            continue;
        }
        reader->stats.stream_started = 1u;
        maelys_jsonrpc_result_t r = maelys_jsonrpc_parse(line->bytes, line->size,
            &reader->options.limits, out, why);
        free(line);
        if (r == MAELYS_JSONRPC_OK) add(&reader->stats.documents, 1u);
        else add(&reader->stats.rejected_lines, 1u);
        return r;
    }
    return MAELYS_JSONRPC_AGAIN;
}

maelys_jsonrpc_result_t maelys_jsonrpc_reader_finish(const maelys_jsonrpc_reader_t *reader) {
    if (!reader) return MAELYS_JSONRPC_ARGUMENT;
    if (reader->failed) return MAELYS_JSONRPC_STATE;
    return reader->length || reader->dropping ? MAELYS_JSONRPC_PROTOCOL : MAELYS_JSONRPC_OK;
}

void maelys_jsonrpc_reader_stats(const maelys_jsonrpc_reader_t *reader, maelys_jsonrpc_reader_stats_t *out) {
    if (out) *out = reader ? reader->stats : (maelys_jsonrpc_reader_stats_t){0};
}

void maelys_jsonrpc_reader_release(maelys_jsonrpc_reader_t **reader) {
    if (!reader || !*reader) return;
    line_t *line = (*reader)->head;
    while (line) { line_t *next = line->next; free(line); line = next; }
    free((*reader)->partial);
    free(*reader);
    *reader = NULL;
}
