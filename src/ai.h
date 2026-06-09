/* Nordstjernen — local on-CPU language model chat backend (llama.cpp).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_AI_H
#define NS_AI_H

#include <glib.h>

G_BEGIN_DECLS

gboolean ns_ai_available(void);

const char *ns_ai_model_name(void);

char *ns_ai_chat(const char *user_msg);

G_END_DECLS

#endif
