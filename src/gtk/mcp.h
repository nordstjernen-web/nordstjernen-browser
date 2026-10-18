/* Nordstjernen — stdio Model Context Protocol server that drives the GUI over its control channel. */

#ifndef NORDSTJERNEN_GTK_MCP_H
#define NORDSTJERNEN_GTK_MCP_H

#include <glib.h>

G_BEGIN_DECLS

int ns_mcp_run(const char *self_exe);

G_END_DECLS

#endif
