/* SPDX-License-Identifier: MPL-2.0 */
#ifndef JSONRPC_TEST_FRAMEWORK_H
#define JSONRPC_TEST_FRAMEWORK_H
#include <maelys/jsonrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); exit(1); \
} } while (0)
#define J(expression) CHECK((expression) == MAELYS_JSON_OK)
#define R(expression) CHECK((expression) == MAELYS_JSONRPC_OK)
void test_reader(void);
void test_message(void);
void test_calls(void);
#endif
