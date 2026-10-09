/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/jsonrpc.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include "classification.h"

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    maelys_jsonrpc_dialect_t dialect = !strcmp(argv[2], "codex-app-server") ?
        MAELYS_JSONRPC_DIALECT_CODEX : MAELYS_JSONRPC_DIALECT_STRICT;
    FILE *file = fopen(argv[1], "rb"); if (!file) return 2;
    const size_t ceiling = 1048576u;
    char *line = malloc(ceiling + 2u); if (!line) { fclose(file); return 2; }
    size_t number = 0u;
    for (;;) {
        size_t n = 0u; int overflow = 0, c;
        while ((c = fgetc(file)) != EOF && c != '\n') {
            if (n <= ceiling) line[n++] = (char)c;
            else overflow = 1;
        }
        if (c == EOF && !n && !overflow) break;
        ++number;
        if (!overflow && n && line[n - 1u] == '\r') --n;
        line[n] = '\0';
        if (!n && !overflow) continue;
        json_error_t old_why;
        json_t *mcp = !overflow && n <= ceiling ? json_loadb(line, n, JSON_REJECT_DUPLICATES, &old_why) : NULL;
        json_t *codex = !overflow && n <= ceiling ? json_loads(line, 0u, &old_why) : NULL;
        maelys_json_document_t *doc = NULL; maelys_json_error_t why;
        maelys_jsonrpc_result_t parsed = maelys_jsonrpc_parse(line, overflow ? ceiling + 1u : n, NULL, &doc, &why);
        maelys_json_result_t copied = MAELYS_JSON_ERR_STATE;
        int equal_mcp = 0, equal_codex = 0;
        maelys_json_writer_t *writer = NULL; char *text = NULL; size_t size = 0u;
        if (parsed == MAELYS_JSONRPC_OK) {
            copied = maelys_json_writer_create(MAELYS_JSON_PROFILE_RFC8259,
                &(maelys_json_limits_t){ceiling, 64u, 65536u}, 0u, &writer);
            if (copied == MAELYS_JSON_OK) copied = maelys_json_writer_value(writer, doc, maelys_json_document_root(doc));
            if (copied == MAELYS_JSON_OK) copied = maelys_json_writer_finish(writer, &text, &size);
            if (copied == MAELYS_JSON_OK) {
                json_t *roundtrip = json_loadb(text, size, JSON_DECODE_ANY, &old_why);
                equal_mcp = mcp && roundtrip && json_equal(mcp, roundtrip);
                equal_codex = codex && roundtrip && json_equal(codex, roundtrip);
                json_decref(roundtrip);
            }
        }
        maelys_jsonrpc_message_t strict = {.kind = MAELYS_JSONRPC_INVALID};
        maelys_jsonrpc_message_t codex_message = {.kind = MAELYS_JSONRPC_INVALID};
        maelys_jsonrpc_limits_t codex_limits = {.dialect = MAELYS_JSONRPC_DIALECT_CODEX};
        maelys_jsonrpc_result_t strict_result = doc ? maelys_jsonrpc_classify(doc, NULL, &strict) : MAELYS_JSONRPC_PROTOCOL;
        maelys_jsonrpc_result_t codex_result = doc ? maelys_jsonrpc_classify(doc, &codex_limits, &codex_message) : MAELYS_JSONRPC_PROTOCOL;
        maelys_jsonrpc_result_t classified = dialect == MAELYS_JSONRPC_DIALECT_CODEX ? codex_result : strict_result;
        maelys_jsonrpc_kind_t selected = dialect == MAELYS_JSONRPC_DIALECT_CODEX ? codex_message.kind : strict.kind;
        maelys_jsonrpc_kind_t expected = legacy_kind(!strcmp(argv[2], "mcp-stdio") ? mcp : codex, dialect);
        int matches = selected == expected && classified ==
            (expected == MAELYS_JSONRPC_INVALID ? MAELYS_JSONRPC_PROTOCOL : MAELYS_JSONRPC_OK);
        printf("{\"classified_strict\":\"%s\",\"classified_codex\":\"%s\","
            "\"legacy_classified\":\"%s\",\"classification_equal\":%d,",
            kind_name(strict.kind), kind_name(codex_message.kind), kind_name(expected), matches);
        printf("\"json_minor\":%d,\"classified\":%d,\"line\":%zu,\"jsonrpc\":%d,\"why\":%d,\"writer\":%d,\"mcp\":%d,\"codex\":%d,\"equal_mcp\":%d,\"equal_codex\":%d}\n",
            MAELYS_JSON_VERSION_MINOR, (int)classified, number, (int)parsed, (int)why.code, (int)copied, mcp != NULL, codex != NULL, equal_mcp, equal_codex);
        free(text); maelys_json_writer_release(writer); maelys_json_document_release(doc);
        json_decref(mcp); json_decref(codex);
        if (c == EOF) break;
    }
    int failed = ferror(file); fclose(file); free(line);
    return failed ? 2 : 0;
}
