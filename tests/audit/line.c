/* SPDX-License-Identifier: MPL-2.0 */
/* Audit one bounded line; never prints agent content or diagnostic excerpts. */
#include <maelys/json.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(MAELYS_JSON_VERSION_MAJOR == 0 &&
    MAELYS_JSON_VERSION_MINOR == 3 && MAELYS_JSON_VERSION_PATCH == 0 && MAELYS_JSON_ABI_VERSION == 3u,
    "audit requires maelys-json v0.3.0");

static const maelys_json_limits_t limits = {1048576u, 64u, 65536u};

static const char *reason(maelys_json_result_t r) {
    switch (r) {
        case MAELYS_JSON_OK: return "OK";
        case MAELYS_JSON_ERR_ARGUMENT: return "ARGUMENT";
        case MAELYS_JSON_ERR_MEMORY: return "MEMORY";
        case MAELYS_JSON_ERR_LIMIT: return "LIMIT";
        case MAELYS_JSON_ERR_SYNTAX: return "SYNTAX";
        case MAELYS_JSON_ERR_UTF8: return "UTF8";
        case MAELYS_JSON_ERR_DUPLICATE_KEY: return "DUPLICATE_KEY";
        case MAELYS_JSON_ERR_TYPE: return "TYPE";
        case MAELYS_JSON_ERR_RANGE: return "RANGE";
        case MAELYS_JSON_ERR_STATE: return "STATE";
        case MAELYS_JSON_ERR_NOT_FOUND: return "NOT_FOUND";
        case MAELYS_JSON_ERR_NOT_INTEGER: return "NOT_INTEGER";
        case MAELYS_JSON_ERR_IO: return "IO";
    }
    return "UNKNOWN";
}

static size_t non_integers(const maelys_json_document_t *doc,
    maelys_json_value_t value) {
    size_t count = 0u, n = 0u;
    maelys_json_type_t type = maelys_json_value_type(doc, value);
    if (type == MAELYS_JSON_TYPE_NUMBER) {
        maelys_json_view_t text;
        if (maelys_json_value_number_text(doc, value, &text) == MAELYS_JSON_OK)
            return strpbrk(text.data, ".eE") != NULL ? 1u : 0u;
    } else if (type == MAELYS_JSON_TYPE_OBJECT) {
        if (maelys_json_object_size(doc, value, &n) != MAELYS_JSON_OK) return 0u;
        for (size_t i = 0u; i < n; ++i) {
            maelys_json_view_t key;
            maelys_json_value_t member;
            if (maelys_json_object_member_at(doc, value, i, &key, &member) == MAELYS_JSON_OK)
                count += non_integers(doc, member);
        }
    } else if (type == MAELYS_JSON_TYPE_ARRAY) {
        if (maelys_json_array_size(doc, value, &n) != MAELYS_JSON_OK) return 0u;
        for (size_t i = 0u; i < n; ++i) {
            maelys_json_value_t member;
            if (maelys_json_array_get(doc, value, i, &member) == MAELYS_JSON_OK)
                count += non_integers(doc, member);
        }
    }
    return count;
}

int main(void) {
    /* One extra byte detects a line beyond the configured maximum. */
    char *bytes = malloc(limits.maximum_bytes + 2u);
    if (!bytes) return 2;
    size_t n = fread(bytes, 1u, limits.maximum_bytes + 1u, stdin);
    if (ferror(stdin)) { free(bytes); return 2; }
    bytes[n] = '\0';
    maelys_json_document_t *doc = NULL;
    maelys_json_error_t why;
    maelys_json_result_t r = maelys_json_document_parse(bytes, n,
        MAELYS_JSON_PROFILE_RFC8259, &limits, &doc, &why);
    /* Reproduce both legacy parser modes, after their line-length check. */
    json_error_t error;
    json_t *codex = n <= limits.maximum_bytes ? json_loads(bytes, 0u, &error) : NULL;
    json_t *mcp = n <= limits.maximum_bytes ?
        json_loadb(bytes, n, JSON_REJECT_DUPLICATES, &error) : NULL;
    maelys_json_result_t copy = MAELYS_JSON_ERR_STATE;
    size_t floats = 0u;
    int equal_codex = 0, equal_mcp = 0, non_integer_id = 0, non_integer_code = 0;
    maelys_json_writer_t *writer = NULL;
    char *output = NULL;
    size_t output_n = 0u;
    if (r == MAELYS_JSON_OK) {
        maelys_json_value_t root = maelys_json_document_root(doc), id, err, code;
        floats = non_integers(doc, root);
        if (maelys_json_object_get(doc, root, "id", &id) == MAELYS_JSON_OK &&
            maelys_json_value_type(doc, id) == MAELYS_JSON_TYPE_NUMBER)
            non_integer_id = non_integers(doc, id) != 0u;
        if (maelys_json_object_get(doc, root, "error", &err) == MAELYS_JSON_OK &&
            maelys_json_object_get(doc, err, "code", &code) == MAELYS_JSON_OK &&
            maelys_json_value_type(doc, code) == MAELYS_JSON_TYPE_NUMBER)
            non_integer_code = non_integers(doc, code) != 0u;
        copy = maelys_json_writer_create(MAELYS_JSON_PROFILE_RFC8259,
            &limits, 0u, &writer);
        if (copy == MAELYS_JSON_OK) copy = maelys_json_writer_value(writer, doc, root);
        if (copy == MAELYS_JSON_OK) copy = maelys_json_writer_finish(writer, &output, &output_n);
        if (copy == MAELYS_JSON_OK) {
            json_t *roundtrip = json_loadb(output, output_n, JSON_DECODE_ANY, &error);
            equal_codex = codex && roundtrip && json_equal(codex, roundtrip);
            equal_mcp = mcp && roundtrip && json_equal(mcp, roundtrip);
            json_decref(roundtrip);
        }
    }
    printf("{\"parse\":\"%s\",\"offset\":%zu,\"codex_accepted\":%s,"
        "\"mcp_accepted\":%s,\"writer\":\"%s\",\"non_integer_numbers\":%zu,"
        "\"non_integer_id\":%s,\"non_integer_error_code\":%s,"
        "\"equal_codex\":%s,\"equal_mcp\":%s}\n",
        reason(r), why.offset, codex ? "true" : "false", mcp ? "true" : "false",
        reason(copy), floats, non_integer_id ? "true" : "false",
        non_integer_code ? "true" : "false", equal_codex ? "true" : "false",
        equal_mcp ? "true" : "false");
    free(output);
    maelys_json_writer_release(writer);
    maelys_json_document_release(doc);
    json_decref(codex);
    json_decref(mcp);
    free(bytes);
    return r == MAELYS_JSON_ERR_MEMORY || copy == MAELYS_JSON_ERR_MEMORY ? 2 : 0;
}
