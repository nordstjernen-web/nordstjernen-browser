/* Nordstjernen — WebAssembly JS API implemented over the vendored WAMR interpreter.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_WASM_H
#define NS_WASM_H

#include <quickjs.h>

void ns_wasm_install(JSContext *ctx, JSValueConst global);

#endif
