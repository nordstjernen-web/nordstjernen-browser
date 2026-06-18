/* Nordstjernen — runtime environment info shared by the JS console and about: page.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_ENV_H
#define NS_ENV_H

#include <glib.h>

#include "html.h"

G_BEGIN_DECLS

typedef void (*ns_env_emit_fn)(const char *label, const char *value,
                               gpointer user_data);


G_END_DECLS

#endif
