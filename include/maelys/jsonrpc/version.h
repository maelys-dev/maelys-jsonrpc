/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_JSONRPC_VERSION_H
#define MAELYS_JSONRPC_VERSION_H
#define MAELYS_JSONRPC_VERSION_MAJOR 0
#define MAELYS_JSONRPC_VERSION_MINOR 2
#define MAELYS_JSONRPC_VERSION_PATCH 0
#define MAELYS_JSONRPC_VERSION_STRING "0.2.0"
#define MAELYS_JSONRPC_ABI_VERSION 2u
#ifdef __cplusplus
extern "C" {
#endif
const char *maelys_jsonrpc_version(void);
#ifdef __cplusplus
}
#endif
#endif
