/* SPDX-License-Identifier: MPL-2.0 */
#include "internal.h"
#include <string.h>

/* Validate bounded caller-supplied method text before touching any writer.
 * Documents already carry this guarantee from maelys-json's parser. */
static int utf8(const unsigned char *text, size_t n) {
    size_t i = 0u;
    while (i < n) {
        unsigned char a = text[i++];
        if (a < 0x80u) continue;
        size_t rest;
        if (a >= 0xc2u && a <= 0xdfu) rest = 1u;
        else if (a >= 0xe0u && a <= 0xefu) rest = 2u;
        else if (a >= 0xf0u && a <= 0xf4u) rest = 3u;
        else return 0;
        if (rest > n - i) return 0;
        unsigned char b = text[i];
        if ((a == 0xe0u && b < 0xa0u) || (a == 0xedu && b > 0x9fu) ||
            (a == 0xf0u && b < 0x90u) || (a == 0xf4u && b > 0x8fu)) return 0;
        for (size_t j = 0u; j < rest; ++j) if ((text[i++] & 0xc0u) != 0x80u) return 0;
    }
    return 1;
}

maelys_jsonrpc_result_t maelys_jsonrpc_method_length(const char *method, size_t max, size_t *out) {
    if (!method || !*method || !out) return MAELYS_JSONRPC_ARGUMENT;
    size_t n = 0u;
    while (n < max && method[n]) ++n;
    if (n == max && method[n]) return MAELYS_JSONRPC_LIMIT;
    if (!utf8((const unsigned char *)method, n)) return MAELYS_JSONRPC_ARGUMENT;
    *out = n;
    return MAELYS_JSONRPC_OK;
}

int maelys_jsonrpc_id_valid(const maelys_json_document_t *doc, maelys_json_value_t id) {
    maelys_json_type_t type = maelys_json_value_type(doc, id);
    if (type == MAELYS_JSON_TYPE_NULL || type == MAELYS_JSON_TYPE_STRING) return 1;
    if (type != MAELYS_JSON_TYPE_NUMBER) return 0;
    maelys_json_view_t text;
    if (maelys_json_value_number_text(doc, id, &text) != MAELYS_JSON_OK) return 0;
    return strpbrk(text.data, ".eE") == NULL;
}

static maelys_json_value_t member(const maelys_json_document_t *doc,
    maelys_json_value_t root, const char *key) {
    maelys_json_value_t value = MAELYS_JSON_VALUE_NONE;
    (void)maelys_json_object_get(doc, root, key, &value);
    return value;
}

static int error_fields(const maelys_json_document_t *doc, maelys_json_value_t err,
    int64_t *code, maelys_json_view_t *message) {
    return maelys_json_value_type(doc, err) == MAELYS_JSON_TYPE_OBJECT &&
        maelys_json_object_get_i64(doc, err, "code", code) == MAELYS_JSON_OK &&
        maelys_json_object_get_string(doc, err, "message", message) == MAELYS_JSON_OK;
}

maelys_jsonrpc_result_t maelys_jsonrpc_classify(const maelys_json_document_t *doc,
    const maelys_jsonrpc_limits_t *limits, maelys_jsonrpc_message_t *out) {
    if (!doc || !out) return MAELYS_JSONRPC_ARGUMENT;
    const maelys_jsonrpc_message_t invalid = {MAELYS_JSONRPC_INVALID,
        MAELYS_JSON_VALUE_NONE, NULL, MAELYS_JSON_VALUE_NONE,
        MAELYS_JSON_VALUE_NONE, MAELYS_JSON_VALUE_NONE};
    *out = invalid;
    maelys_json_value_t root = maelys_json_document_root(doc);
    maelys_json_view_t version;
    if (maelys_json_value_type(doc, root) != MAELYS_JSON_TYPE_OBJECT ||
        maelys_json_object_get_string(doc, root, "jsonrpc", &version) != MAELYS_JSON_OK ||
        version.size != 3u || memcmp(version.data, "2.0", 3u)) return MAELYS_JSONRPC_PROTOCOL;
    maelys_jsonrpc_message_t message = invalid;
    message.id = member(doc, root, "id");
    message.params = member(doc, root, "params");
    message.result = member(doc, root, "result");
    message.error = member(doc, root, "error");
    maelys_json_value_t method = member(doc, root, "method");
    if (message.id != MAELYS_JSON_VALUE_NONE && !maelys_jsonrpc_id_valid(doc, message.id))
        return MAELYS_JSONRPC_PROTOCOL;
    if (method != MAELYS_JSON_VALUE_NONE) {
        maelys_json_view_t name;
        size_t max = limits && limits->max_method_length ? limits->max_method_length :
            MAELYS_JSONRPC_DEFAULT_MAX_METHOD_LENGTH;
        if (maelys_json_value_string(doc, method, &name) != MAELYS_JSON_OK ||
            !name.size || name.size > max || message.result != MAELYS_JSON_VALUE_NONE ||
            message.error != MAELYS_JSON_VALUE_NONE) return MAELYS_JSONRPC_PROTOCOL;
        maelys_json_type_t params_type = maelys_json_value_type(doc, message.params);
        if (message.params != MAELYS_JSON_VALUE_NONE && params_type != MAELYS_JSON_TYPE_OBJECT &&
            params_type != MAELYS_JSON_TYPE_ARRAY) return MAELYS_JSONRPC_PROTOCOL;
        message.method = name.data;
        message.kind = message.id == MAELYS_JSON_VALUE_NONE ?
            MAELYS_JSONRPC_NOTIFICATION : MAELYS_JSONRPC_REQUEST;
    } else {
        if (message.id == MAELYS_JSON_VALUE_NONE || message.params != MAELYS_JSON_VALUE_NONE ||
            (message.result == MAELYS_JSON_VALUE_NONE) == (message.error == MAELYS_JSON_VALUE_NONE))
            return MAELYS_JSONRPC_PROTOCOL;
        if (message.error != MAELYS_JSON_VALUE_NONE) {
            int64_t code;
            maelys_json_view_t text;
            if (!error_fields(doc, message.error, &code, &text)) return MAELYS_JSONRPC_PROTOCOL;
            message.kind = MAELYS_JSONRPC_ERROR;
        } else message.kind = MAELYS_JSONRPC_RESPONSE;
    }
    *out = message;
    return MAELYS_JSONRPC_OK;
}

maelys_jsonrpc_result_t maelys_jsonrpc_error_read(const maelys_json_document_t *doc,
    maelys_json_value_t error, int64_t *code, char *excerpt, size_t cap) {
    if (!doc || !code || (!excerpt && cap)) return MAELYS_JSONRPC_ARGUMENT;
    int64_t value;
    maelys_json_view_t text;
    if (!error_fields(doc, error, &value, &text)) return MAELYS_JSONRPC_PROTOCOL;
    size_t n = cap ? cap - 1u : 0u;
    if (n > MAELYS_JSONRPC_DEFAULT_MAX_ERROR_EXCERPT) n = MAELYS_JSONRPC_DEFAULT_MAX_ERROR_EXCERPT;
    if (n > text.size) n = text.size;
    if (n < text.size) while (n && ((unsigned char)text.data[n] & 0xc0u) == 0x80u) --n;
    if (cap) { memcpy(excerpt, text.data, n); excerpt[n] = '\0'; }
    *code = value;
    return MAELYS_JSONRPC_OK;
}

static maelys_json_result_t start(maelys_json_writer_t *writer) {
    maelys_json_result_t r = maelys_json_writer_object_begin(writer);
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_key_cstr(writer, "jsonrpc");
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_string_cstr(writer, "2.0");
    return r;
}

static maelys_jsonrpc_result_t begin_method(maelys_json_writer_t *writer,
    const char *method, int with_id, int64_t id) {
    if (!writer) return MAELYS_JSONRPC_ARGUMENT;
    size_t length;
    maelys_jsonrpc_result_t validation = maelys_jsonrpc_method_length(method,
        MAELYS_JSONRPC_DEFAULT_MAX_METHOD_LENGTH, &length);
    if (validation != MAELYS_JSONRPC_OK) return validation;
    maelys_json_result_t r = start(writer);
    if (with_id && r == MAELYS_JSON_OK) r = maelys_json_writer_key_cstr(writer, "id");
    if (with_id && r == MAELYS_JSON_OK) r = maelys_json_writer_i64(writer, id);
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_key_cstr(writer, "method");
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_string(writer, method, length);
    return maelys_jsonrpc_from_json(r);
}

maelys_jsonrpc_result_t maelys_jsonrpc_begin_request(maelys_json_writer_t *writer,
    int64_t id, const char *method) { return begin_method(writer, method, 1, id); }

maelys_jsonrpc_result_t maelys_jsonrpc_begin_notification(maelys_json_writer_t *writer,
    const char *method) { return begin_method(writer, method, 0, 0); }

maelys_jsonrpc_result_t maelys_jsonrpc_begin_response(maelys_json_writer_t *writer,
    const maelys_json_document_t *doc, maelys_json_value_t id) {
    if (!writer || !doc || !maelys_jsonrpc_id_valid(doc, id)) return MAELYS_JSONRPC_ARGUMENT;
    maelys_json_result_t r = start(writer);
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_key_cstr(writer, "id");
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_value(writer, doc, id);
    return maelys_jsonrpc_from_json(r);
}

maelys_jsonrpc_result_t maelys_jsonrpc_write_error(maelys_json_writer_t *writer,
    const maelys_json_document_t *doc, maelys_json_value_t id, int64_t code, const char *message) {
    if (!message) return MAELYS_JSONRPC_ARGUMENT;
    maelys_jsonrpc_result_t status = maelys_jsonrpc_begin_response(writer, doc, id);
    if (status != MAELYS_JSONRPC_OK) return status;
    maelys_json_result_t r = maelys_json_writer_key_cstr(writer, "error");
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_object_begin(writer);
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_key_cstr(writer, "code");
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_i64(writer, code);
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_key_cstr(writer, "message");
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_string_cstr(writer, message);
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_object_end(writer);
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_object_end(writer);
    return maelys_jsonrpc_from_json(r);
}

maelys_jsonrpc_result_t maelys_jsonrpc_end(maelys_json_writer_t *writer, char **out, size_t *n) {
    if (!writer || !out || !n) return MAELYS_JSONRPC_ARGUMENT;
    maelys_json_result_t r = maelys_json_writer_object_end(writer);
    if (r == MAELYS_JSON_OK) r = maelys_json_writer_finish(writer, out, n);
    return maelys_jsonrpc_from_json(r);
}
