/* Nordstjernen — UI string translation: OS-language lookup over the
 * data/i18n catalogue files.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_I18N_H
#define NS_I18N_H

#include <glib.h>

G_BEGIN_DECLS

void        ns_i18n_init(const char *self_exe);
const char *ns_i18n(const char *text);
const char *ns_i18n_language(void);

G_END_DECLS

#endif
