/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/jsonrpc.h>
#include <stdlib.h>
#include <string.h>
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);

typedef struct summary { uint64_t hash, documents, errors; maelys_jsonrpc_reader_stats_t stats; } summary_t;
static summary_t run(const unsigned char *data, size_t size, size_t chunk) {
    summary_t out = {0};
    maelys_jsonrpc_reader_options_t options = {0};
    options.limits = (maelys_json_limits_t){4096u, 16u, 512u};
    options.strip_cr = 1u; options.tolerate_preamble = size && (data[0] & 1u);
    maelys_jsonrpc_reader_t *reader = NULL;
    if (maelys_jsonrpc_reader_create(&options, &reader) != MAELYS_JSONRPC_OK) return out;
    for (size_t offset = 0u; offset < size;) {
        size_t n = size - offset < chunk ? size - offset : chunk;
        if (maelys_jsonrpc_reader_feed(reader, data + offset, n) != MAELYS_JSONRPC_OK) break;
        offset += n;
        for (;;) {
            maelys_json_document_t *doc = NULL; maelys_json_error_t why;
            maelys_jsonrpc_result_t r = maelys_jsonrpc_reader_next(reader, &doc, &why);
            if (r == MAELYS_JSONRPC_AGAIN) break;
            if (r == MAELYS_JSONRPC_OK) {
                maelys_jsonrpc_message_t message;
                maelys_jsonrpc_result_t strict = maelys_jsonrpc_classify(doc, NULL, &message);
                for (int dialect = 0; dialect < 2; ++dialect) {
                    maelys_jsonrpc_limits_t limits = {.dialect = (maelys_jsonrpc_dialect_t)dialect};
                    maelys_jsonrpc_message_t classified;
                    maelys_jsonrpc_result_t result = maelys_jsonrpc_classify(doc, &limits, &classified);
                    if (!dialect && (strict != result || message.kind != classified.kind)) abort();
                    out.hash = out.hash * 131u + (uint64_t)result;
                    out.hash = out.hash * 131u + (uint64_t)classified.kind;
                }
                ++out.documents;
                maelys_json_writer_t *writer = NULL; char *bytes = NULL; size_t length = 0u;
                if (maelys_json_writer_create(MAELYS_JSON_PROFILE_RFC8259, &options.limits, 0u, &writer) == MAELYS_JSON_OK &&
                    maelys_json_writer_value(writer, doc, maelys_json_document_root(doc)) == MAELYS_JSON_OK &&
                    maelys_json_writer_finish(writer, &bytes, &length) == MAELYS_JSON_OK) {
                    for (size_t i = 0u; i < length; ++i) out.hash = out.hash * 131u + (unsigned char)bytes[i];
                    maelys_json_document_t *again = NULL;
                    maelys_json_writer_t *second = NULL; char *fixed = NULL; size_t fixed_n = 0u;
                    maelys_json_result_t copied = maelys_json_document_parse(bytes, length,
                        MAELYS_JSON_PROFILE_RFC8259, &options.limits, &again, NULL);
                    if (copied == MAELYS_JSON_OK) copied = maelys_json_writer_create(
                        MAELYS_JSON_PROFILE_RFC8259, &options.limits, 0u, &second);
                    if (copied == MAELYS_JSON_OK) copied = maelys_json_writer_value(second, again, maelys_json_document_root(again));
                    if (copied == MAELYS_JSON_OK) copied = maelys_json_writer_finish(second, &fixed, &fixed_n);
                    if (copied != MAELYS_JSON_OK || fixed_n != length || memcmp(fixed, bytes, length)) abort();
                    free(fixed); maelys_json_writer_release(second); maelys_json_document_release(again);
                }
                free(bytes); maelys_json_writer_release(writer); maelys_json_document_release(doc);
            } else if (r == MAELYS_JSONRPC_PROTOCOL) {
                ++out.errors; out.hash = out.hash * 131u + (uint64_t)why.code;
            } else break;
        }
    }
    out.hash = out.hash * 131u + (uint64_t)maelys_jsonrpc_reader_finish(reader);
    maelys_jsonrpc_reader_stats(reader, &out.stats); maelys_jsonrpc_reader_release(&reader);
    return out;
}
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size) {
    if (size > 65536u) return 0;
    summary_t a = run(data, size, 1u), b = run(data, size, size ? size : 1u);
    if (a.hash != b.hash || a.documents != b.documents || a.errors != b.errors ||
        a.stats.bytes_seen != b.stats.bytes_seen || a.stats.lines_seen != b.stats.lines_seen ||
        a.stats.preamble_lines != b.stats.preamble_lines || a.stats.blank_lines != b.stats.blank_lines ||
        a.stats.line_overflows != b.stats.line_overflows || a.stats.rejected_lines != b.stats.rejected_lines ||
        a.stats.missing_version != b.stats.missing_version || a.stats.documents != b.stats.documents ||
        a.stats.stream_started != b.stats.stream_started) abort();
    return 0;
}
