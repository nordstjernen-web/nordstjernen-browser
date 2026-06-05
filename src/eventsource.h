/* Nordstjernen — minimal EventSource / Server-Sent Events client (libcurl). */

#ifndef ND_EVENTSOURCE_H
#define ND_EVENTSOURCE_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct nd_es nd_es;

typedef struct nd_es_callbacks {
    void (*on_open)    (gpointer user_data);
    void (*on_message) (const char *event, const char *data,
                        const char *last_id, gpointer user_data);
    void (*on_error)   (gboolean fatal, gpointer user_data);
    gboolean (*busy)   (gpointer user_data);
} nd_es_callbacks;

nd_es *nd_es_new(const char *url, const char *origin, const char *last_event_id,
                 const nd_es_callbacks *cbs, gpointer user_data);

void nd_es_close(nd_es *es);
void nd_es_free(nd_es *es);

G_END_DECLS

#endif
