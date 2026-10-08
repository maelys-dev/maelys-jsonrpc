/* SPDX-License-Identifier: MPL-2.0 */
/* Executable observations of the exact prerequisite, not JSON-RPC tests. */
#include <maelys/json.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(MAELYS_JSON_VERSION_MAJOR == 0 &&
    MAELYS_JSON_VERSION_MINOR == 3 && MAELYS_JSON_VERSION_PATCH == 0 && MAELYS_JSON_ABI_VERSION == 3u,
    "audit requires maelys-json v0.3.0");

static const maelys_json_limits_t limits = {1048576u, 64u, 65536u};
static int failures;

static void observe(json_t *report, const char *name,
    maelys_json_result_t result, maelys_json_result_t expected) {
    if (json_object_set_new(report, name, json_integer(result)) != 0) exit(2);
    if (result != expected) ++failures;
}

static void parse_case(json_t *report, const char *name, const void *bytes,
    size_t n, maelys_json_result_t expected) {
    maelys_json_document_t *document = NULL;
    maelys_json_error_t why;
    maelys_json_result_t r = maelys_json_document_parse(bytes, n,
        MAELYS_JSON_PROFILE_RFC8259, &limits, &document, &why);
    observe(report, name, r, expected);
    if (why.code != r) ++failures;
    maelys_json_document_release(document);
}

static void copy_case(json_t *report, const char *name, const char *bytes,
    maelys_json_result_t expected) {
    maelys_json_document_t *document = NULL;
    maelys_json_writer_t *writer = NULL;
    maelys_json_result_t r = maelys_json_document_parse(bytes, strlen(bytes),
        MAELYS_JSON_PROFILE_RFC8259, &limits, &document, NULL);
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_create(
        MAELYS_JSON_PROFILE_RFC8259, &limits, 0u, &writer);
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_value(writer, document,
        maelys_json_document_root(document));
    observe(report, name, r, expected);
    maelys_json_writer_release(writer);
    maelys_json_document_release(document);
}

int main(void) {
    json_t *report = json_object();
    if (!report) return 2;
    const char duplicate[] = "{\"id\":1,\"id\":2}";
    const char escaped_duplicate[] = "{\"id\":1,\"\\u0069d\":2}";
    const char bad_utf8[] = {'{','"','x','"',':','"',(char)0xc0,(char)0xaf,'"','}'};
    const char nul[] = "{\"x\":\"\\u0000\"}";
    const unsigned char bom[] = {0xef,0xbb,0xbf,'{','}'};
    parse_case(report, "duplicate_key", duplicate, sizeof duplicate - 1u,
        MAELYS_JSON_ERR_DUPLICATE_KEY);
    parse_case(report, "escaped_duplicate_key", escaped_duplicate,
        sizeof escaped_duplicate - 1u, MAELYS_JSON_ERR_DUPLICATE_KEY);
    parse_case(report, "invalid_utf8", bad_utf8, sizeof bad_utf8,
        MAELYS_JSON_ERR_UTF8);
    parse_case(report, "escaped_u0000", nul, sizeof nul - 1u,
        MAELYS_JSON_ERR_SYNTAX);
    parse_case(report, "bom", bom, sizeof bom, MAELYS_JSON_ERR_SYNTAX);
    copy_case(report, "relay_1_5e3", "{\"result\":1.5e3}",
        MAELYS_JSON_OK);
    copy_case(report, "relay_large_integer", "{\"result\":18446744073709551616}",
        MAELYS_JSON_ERR_RANGE);

    maelys_json_writer_t *writer = NULL;
    char *output = NULL;
    size_t n = 0u;
    maelys_json_result_t r = maelys_json_writer_create(
        MAELYS_JSON_PROFILE_RFC8259, &limits, 0u, &writer);
    const char controls[] = {'\n', '\x01'};
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_string(writer, controls,
        sizeof controls);
    observe(report, "writer_c0", r, MAELYS_JSON_OK);
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_finish(writer, &output, &n);
    observe(report, "writer_c0_finish", r, MAELYS_JSON_OK);
    if (r == MAELYS_JSON_OK) {
        if (strcmp(output, "\"\\n\\u0001\"") != 0) ++failures;
        if (json_object_set_new(report, "writer_c0_output",
            json_stringn(output, n)) != 0) return 2;
    }
    free(output);
    maelys_json_writer_release(writer);

    json_error_t error;
    json_t *old = json_loads(duplicate, 0, &error);
    if (!old || json_integer_value(json_object_get(old, "id")) != 2) ++failures;
    if (json_object_set_new(report, "codexmanager_duplicate_last_wins",
        json_boolean(old != NULL)) != 0) return 2;
    json_decref(old);
    old = json_loadb(duplicate, sizeof duplicate - 1u,
        JSON_REJECT_DUPLICATES, &error);
    if (old) ++failures;
    if (json_object_set_new(report, "mcp_duplicate_rejected",
        json_boolean(old == NULL)) != 0) return 2;
    json_decref(old);
    if (json_object_set_new(report, "maelys_json_abi", json_integer(MAELYS_JSON_ABI_VERSION)) != 0) return 2;
    if (json_object_set_new(report, "maelys_json_version",
        json_string(maelys_json_version())) != 0) return 2;
    if (json_object_set_new(report, "jansson_version", json_string(JANSSON_VERSION)) != 0)
        return 2;
    if (json_object_set_new(report, "observation_failures", json_integer(failures)) != 0)
        return 2;
    char *text = json_dumps(report, JSON_INDENT(2) | JSON_SORT_KEYS);
    if (!text) return 2;
    puts(text);
    free(text);
    json_decref(report);
    return failures ? 1 : 0;
}
