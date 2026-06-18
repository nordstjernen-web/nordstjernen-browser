/* Nordstjernen — runtime environment info shared by the JS console and about: page.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "env.h"

#ifdef NS_HAVE_GTK
#include <gtk/gtk.h>
#endif

#include "css.h"
#include "js.h"
#include "net.h"
#include "quickjs.h"

#ifndef G_OS_WIN32
#include <sys/utsname.h>
#endif

