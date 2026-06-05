/* Nordstjernen — serial per-tab worker for GTK-free page work.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "tab_worker.h"

#include "css.h"
#include "html.h"
#include "image.h"

#include <string.h>

typedef enum ns_tab_job_type {
    NS_TAB_JOB_LOAD,
    NS_TAB_JOB_IMAGE,
    NS_TAB_JOB_CSS,
} ns_tab_job_type;

typedef struct ns_tab_job {
    ns_tab_job_type type;
    ns_response *resp;
    gboolean parse_html;
    char *scope_id;
    gpointer cb;
    gpointer user_data;
    GDestroyNotify user_data_destroy;
} ns_tab_job;

struct ns_tab_worker {
    GMutex lock;
    GCond  cond;
    GQueue jobs;
    GThread *thread;
    gboolean stopping;
};

typedef struct ns_tab_load_delivery {
    ns_tab_load_ready_cb cb;
    ns_tab_load_result *result;
    gpointer user_data;
} ns_tab_load_delivery;

typedef struct ns_tab_image_delivery {
    ns_tab_image_ready_cb cb;
    ns_tab_image_result *result;
    gpointer user_data;
} ns_tab_image_delivery;

typedef struct ns_tab_css_delivery {
    ns_tab_css_ready_cb cb;
    ns_tab_css_result *result;
    gpointer user_data;
} ns_tab_css_delivery;

static void
ns_tab_job_free(ns_tab_job *job)
{
    if (!job) return;
    ns_response_free(job->resp);
    g_free(job->scope_id);
    if (job->user_data_destroy && job->user_data)
        job->user_data_destroy(job->user_data);
    g_free(job);
}

void
ns_tab_load_result_free(ns_tab_load_result *result)
{
    if (!result) return;
    ns_response_free(result->resp);
    ns_node_free(result->doc);
    g_free(result->body);
    g_free(result);
}

void
ns_tab_image_result_free(ns_tab_image_result *result)
{
    if (!result) return;
    ns_response_free(result->resp);
    g_free(result->pixels);
    if (result->anim_frames) g_array_free(result->anim_frames, TRUE);
    g_free(result);
}

void
ns_tab_css_result_free(ns_tab_css_result *result)
{
    if (!result) return;
    ns_response_free(result->resp);
    ns_css_stylesheet_free(result->sheet);
    g_free(result);
}

static gboolean
ns_tab_deliver_load(gpointer data)
{
    ns_tab_load_delivery *d = data;
    if (d->cb)
        d->cb(d->result, d->user_data);
    else
        ns_tab_load_result_free(d->result);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static gboolean
ns_tab_deliver_image(gpointer data)
{
    ns_tab_image_delivery *d = data;
    if (d->cb)
        d->cb(d->result, d->user_data);
    else
        ns_tab_image_result_free(d->result);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static gboolean
ns_tab_deliver_css(gpointer data)
{
    ns_tab_css_delivery *d = data;
    if (d->cb)
        d->cb(d->result, d->user_data);
    else
        ns_tab_css_result_free(d->result);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static void
ns_tab_worker_process_load(ns_tab_job *job)
{
    ns_tab_load_result *result = g_new0(ns_tab_load_result, 1);
    result->resp = job->resp;
    job->resp = NULL;
    if (result->resp && result->resp->body && result->resp->body->len > 0) {
        result->body = ns_html_decode_body(
            (const char *)result->resp->body->data,
            result->resp->body->len);
        if (!result->body) result->body = g_strdup("");
        result->body_len = strlen(result->body);
        if (job->parse_html) {
            result->doc = ns_html_parse(result->body,
                                        (gssize)result->body_len);
            result->parsed = result->doc != NULL;
        }
    }
    ns_tab_load_delivery *delivery = g_new0(ns_tab_load_delivery, 1);
    delivery->cb = (ns_tab_load_ready_cb)job->cb;
    delivery->result = result;
    delivery->user_data = job->user_data;
    job->user_data = NULL;
    g_main_context_invoke_full(NULL, G_PRIORITY_DEFAULT,
                               ns_tab_deliver_load, delivery, NULL);
}

static void
ns_tab_worker_process_image(ns_tab_job *job)
{
    ns_tab_image_result *result = g_new0(ns_tab_image_result, 1);
    result->resp = job->resp;
    job->resp = NULL;
    ns_response *resp = result->resp;
    if (resp && !resp->error && resp->status < 400 &&
        resp->body && resp->body->len > 0) {
        gboolean gif = resp->body->len >= 6 &&
            resp->body->data[0] == 'G' && resp->body->data[1] == 'I' &&
            resp->body->data[2] == 'F';
        if (gif) {
            result->anim_frames = ns_image_decode_wuffs_anim_to_pixels(
                resp->body->data, resp->body->len,
                &result->width, &result->height);
            if (result->anim_frames && result->anim_frames->len == 1) {
                ns_image_pixel_frame *f =
                    &g_array_index(result->anim_frames,
                                   ns_image_pixel_frame, 0);
                result->pixels = f->pixels;
                result->pixels_len = f->pixels_len;
                result->stride = f->stride;
                result->format = f->format;
                result->width = f->width;
                result->height = f->height;
                f->pixels = NULL;
                g_array_free(result->anim_frames, TRUE);
                result->anim_frames = NULL;
            }
        } else {
            result->pixels = ns_image_decode_bytes_to_pixels(
                resp->body->data, resp->body->len,
                &result->width, &result->height, &result->stride,
                &result->pixels_len, &result->format);
        }
    }
    ns_tab_image_delivery *delivery = g_new0(ns_tab_image_delivery, 1);
    delivery->cb = (ns_tab_image_ready_cb)job->cb;
    delivery->result = result;
    delivery->user_data = job->user_data;
    job->user_data = NULL;
    g_main_context_invoke_full(NULL, G_PRIORITY_DEFAULT,
                               ns_tab_deliver_image, delivery, NULL);
}

static void
ns_tab_worker_process_css(ns_tab_job *job)
{
    ns_tab_css_result *result = g_new0(ns_tab_css_result, 1);
    result->resp = job->resp;
    job->resp = NULL;
    ns_response *resp = result->resp;
    if (resp && !resp->error && resp->status < 400 &&
        resp->body && resp->body->len > 0) {
        char *scoped = job->scope_id
            ? ns_css_scope_css((const char *)resp->body->data,
                               (gssize)resp->body->len, job->scope_id)
            : NULL;
        result->sheet = scoped
            ? ns_css_stylesheet_parse(scoped, (gssize)strlen(scoped))
            : ns_css_stylesheet_parse((const char *)resp->body->data,
                                      (gssize)resp->body->len);
        g_free(scoped);
    }
    ns_tab_css_delivery *delivery = g_new0(ns_tab_css_delivery, 1);
    delivery->cb = (ns_tab_css_ready_cb)job->cb;
    delivery->result = result;
    delivery->user_data = job->user_data;
    job->user_data = NULL;
    g_main_context_invoke_full(NULL, G_PRIORITY_DEFAULT,
                               ns_tab_deliver_css, delivery, NULL);
}

static gpointer
ns_tab_worker_thread(gpointer data)
{
    ns_tab_worker *worker = data;
    for (;;) {
        g_mutex_lock(&worker->lock);
        while (!worker->stopping && g_queue_is_empty(&worker->jobs))
            g_cond_wait(&worker->cond, &worker->lock);
        if (worker->stopping && g_queue_is_empty(&worker->jobs)) {
            g_mutex_unlock(&worker->lock);
            break;
        }
        ns_tab_job *job = g_queue_pop_head(&worker->jobs);
        g_mutex_unlock(&worker->lock);

        if (job->type == NS_TAB_JOB_LOAD)
            ns_tab_worker_process_load(job);
        else if (job->type == NS_TAB_JOB_IMAGE)
            ns_tab_worker_process_image(job);
        else if (job->type == NS_TAB_JOB_CSS)
            ns_tab_worker_process_css(job);
        ns_tab_job_free(job);
    }
    return NULL;
}

ns_tab_worker *
ns_tab_worker_new(const char *name)
{
    ns_tab_worker *worker = g_new0(ns_tab_worker, 1);
    g_mutex_init(&worker->lock);
    g_cond_init(&worker->cond);
    g_queue_init(&worker->jobs);
    GError *err = NULL;
    worker->thread = g_thread_try_new(name && *name ? name : "nd-tab",
                                      ns_tab_worker_thread, worker, &err);
    if (!worker->thread) {
        g_clear_error(&err);
        g_cond_clear(&worker->cond);
        g_mutex_clear(&worker->lock);
        g_free(worker);
        return NULL;
    }
    return worker;
}

void
ns_tab_worker_free(ns_tab_worker *worker)
{
    if (!worker) return;
    g_mutex_lock(&worker->lock);
    worker->stopping = TRUE;
    while (!g_queue_is_empty(&worker->jobs))
        ns_tab_job_free(g_queue_pop_head(&worker->jobs));
    g_cond_signal(&worker->cond);
    g_mutex_unlock(&worker->lock);
    g_thread_join(worker->thread);
    g_cond_clear(&worker->cond);
    g_mutex_clear(&worker->lock);
    g_free(worker);
}

static gboolean
ns_tab_worker_submit(ns_tab_worker *worker, ns_tab_job *job)
{
    if (!worker || !job) return FALSE;
    g_mutex_lock(&worker->lock);
    if (worker->stopping) {
        g_mutex_unlock(&worker->lock);
        return FALSE;
    }
    g_queue_push_tail(&worker->jobs, job);
    g_cond_signal(&worker->cond);
    g_mutex_unlock(&worker->lock);
    return TRUE;
}

gboolean
ns_tab_worker_load_response(ns_tab_worker *worker,
                            ns_response *resp,
                            gboolean parse_html,
                            ns_tab_load_ready_cb cb,
                            gpointer user_data,
                            GDestroyNotify user_data_destroy)
{
    ns_tab_job *job = g_new0(ns_tab_job, 1);
    job->type = NS_TAB_JOB_LOAD;
    job->resp = resp;
    job->parse_html = parse_html;
    job->cb = cb;
    job->user_data = user_data;
    job->user_data_destroy = user_data_destroy;
    if (ns_tab_worker_submit(worker, job)) return TRUE;
    job->resp = NULL;
    job->user_data = NULL;
    ns_tab_job_free(job);
    return FALSE;
}

gboolean
ns_tab_worker_decode_image_response(ns_tab_worker *worker,
                                    ns_response *resp,
                                    ns_tab_image_ready_cb cb,
                                    gpointer user_data,
                                    GDestroyNotify user_data_destroy)
{
    ns_tab_job *job = g_new0(ns_tab_job, 1);
    job->type = NS_TAB_JOB_IMAGE;
    job->resp = resp;
    job->cb = cb;
    job->user_data = user_data;
    job->user_data_destroy = user_data_destroy;
    if (ns_tab_worker_submit(worker, job)) return TRUE;
    job->resp = NULL;
    job->user_data = NULL;
    ns_tab_job_free(job);
    return FALSE;
}

gboolean
ns_tab_worker_parse_css_response(ns_tab_worker *worker,
                                 ns_response *resp,
                                 const char *scope_id,
                                 ns_tab_css_ready_cb cb,
                                 gpointer user_data,
                                 GDestroyNotify user_data_destroy)
{
    ns_tab_job *job = g_new0(ns_tab_job, 1);
    job->type = NS_TAB_JOB_CSS;
    job->resp = resp;
    job->scope_id = g_strdup(scope_id);
    job->cb = cb;
    job->user_data = user_data;
    job->user_data_destroy = user_data_destroy;
    if (ns_tab_worker_submit(worker, job)) return TRUE;
    job->resp = NULL;
    job->user_data = NULL;
    ns_tab_job_free(job);
    return FALSE;
}
