/* Nordstjernen — the text layer the engine talks to.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-or-later
 */

#ifndef NS_PANGO_H
#define NS_PANGO_H

#ifdef NS_USE_WEB_TEXT_LIB
#include <ns-pango/pangocairo.h>
#ifdef NS_HAVE_PANGOFT2
#include <ns-pango/pangofc-fontmap.h>
#endif
#else
#include <pango/pangocairo.h>
#ifdef NS_HAVE_PANGOFT2
#include <pango/pangofc-fontmap.h>
#endif
#include "ns_pango_names.h"
#endif

#endif
