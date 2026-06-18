/* Nordstjernen — optional spell checking over the Enchant library.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_SPELLCHECK_H
#define NS_SPELLCHECK_H

#include <glib.h>

G_BEGIN_DECLS

void      ns_spell_init(void);
gboolean  ns_spell_available(void);
gboolean  ns_spell_word_ok(const char *word, gssize len, const char *lang);

G_END_DECLS

#endif
