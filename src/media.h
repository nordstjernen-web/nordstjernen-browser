/* Nordstjernen — external media player launching (frontend-agnostic).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_MEDIA_H
#define NS_MEDIA_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    NS_MEDIA_LAUNCHED = 0,
    NS_MEDIA_FAILED,
    NS_MEDIA_UNSAFE,
    NS_MEDIA_NO_PLAYER,
    NS_MEDIA_NEED_YTDLP,
} ns_media_status;

void ns_media_broker_start(void);

gboolean ns_media_url_is_safe(const char *url, gboolean allow_local);

/* TRUE for a streaming video *page* URL (a YouTube watch/shorts/embed/live
 * link or youtu.be) that should be handed to an external player + yt-dlp
 * directly on navigation rather than rendered in-page. */
gboolean ns_media_is_video_page(const char *url);

ns_media_status ns_media_try_launch(const char *url, gboolean stream,
                                    char **suggest_app, char **suggest_url);

G_END_DECLS

#endif
