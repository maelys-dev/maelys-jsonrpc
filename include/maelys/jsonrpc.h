/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_JSONRPC_H
#define MAELYS_JSONRPC_H
/* Output parameters change only on success, except classify's INVALID and
 * JSON diagnostic outputs. Objects have no global state or synchronization;
 * serialize access to a reader, writer, or calls table in the calling product. */
#include <maelys/jsonrpc/version.h>
#include <maelys/jsonrpc/frame.h>
#include <maelys/jsonrpc/message.h>
#include <maelys/jsonrpc/calls.h>
#endif
