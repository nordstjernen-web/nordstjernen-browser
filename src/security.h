/* Nordstjernen — startup security: refuse-root + landlock + seccomp sandbox.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_SECURITY_H
#define NS_SECURITY_H

#include <glib.h>

G_BEGIN_DECLS

gboolean ns_security_refuse_root(void);

void ns_security_sandbox_init(const char *self_exe);

void ns_security_add_writable_dir(const char *dir);

void ns_security_add_exec_dir(const char *dir);

void ns_security_seccomp_init(void);

void ns_security_win32_mitigations_init(gboolean allow_child_processes);

gboolean ns_security_csprng_fill(void *buf, gsize len);

gboolean ns_security_sri_check(const char *integrity_attr,
                               const void *body,
                               gsize       body_len);

void ns_security_mark_download_origin(const char *path, const char *url);

void ns_security_harden_allocator(void);

G_END_DECLS

#endif
