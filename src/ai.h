/* Nordstjernen — local on-CPU language model chat backend (llama.cpp).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_AI_H
#define NS_AI_H

#include <glib.h>

G_BEGIN_DECLS


void ns_ai_select_download(const char *model_id);

char *ns_ai_status_json(void);

char *ns_ai_chat_start(const char *user_msg);

char *ns_ai_chat_poll(void);

void ns_ai_chat_reset(void);

void ns_ai_shutdown(void);

G_END_DECLS

#endif
