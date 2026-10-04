/* Nordstjernen — IndexedDB backend bridge.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0 OR GPL-3.0-only
 */

#ifndef NS_IDB_H
#define NS_IDB_H

#include "ns_quickjs.h"

void ns_idb_install(JSContext *ctx, JSValueConst global);

#endif
