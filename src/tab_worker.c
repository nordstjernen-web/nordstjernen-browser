/* Nordstjernen — serial per-tab worker for GTK-free page work.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "tab_worker.h"

#include "css.h"
#include "html.h"
#include "image.h"

#include <string.h>

typedef enum nd_tab_job_type {
    ND_TAB_JOB_LOAD,
    ND_TAB_JOB_IMAGE,
    ND_TAB_JOB_CSS,
} nd_tab_job_type;

typedef struct nd_tab_job {
    nd_tab_job_type type;
    nd_response *resp;
    gboolean parse_html;
    char *scope_id;
    gpointer cb;
    gpointer user_data;
    GDestroyNotify user_data_destroy;
} nd_tab_job;

struct nd_tab_worker {
    GMutex lock;
    GCond  cond;
    GQueue jobs;
    GThread *thread;
    gboolean stopping;
};

typedef struct nd_tab_load_delivery {
    nd_tab_load_ready_cb cb;
    nd_tab_load_result *result;
    gpointer user_data;
} nd_tab_load_delivery;

typedef struct nd_tab_image_delivery {
    nd_tab_image_ready_cb cb;
    nd_tab_image_result *result;
    gpointer user_data;
} nd_tab_image_delivery;

typedef struct nd_tab_css_delivery {
    nd_tab_css_ready_cb cb;
    nd_tab_css_result *result;
    gpointer user_data;
} nd_tab_css_delivery;

static void
nd_tab_job_free(nd_tab_job *job)
{
    if (!job) return;
    nd_response_free(job->resp);
    g_free(job->scope_id);
    if (job->user_data_destroy && job->user_data)
        job->user_data_destroy(job->user_data);
    g_free(job);
}

void
nd_tab_load_result_free(nd_tab_load_result *result)
{
    if (!result) return;
    nd_response_free(result->resp);
    nd_node_free(result->doc);
    g_free(result->body);
    g_free(result);
}

void
nd_tab_image_result_free(nd_tab_image_result *result)
{
    if (!result) return;
    nd_response_free(result->resp);
    g_free(result->pixels);
    if (result->anim_frames) g_array_free(result->anim_frames, TRUE);
    g_free(result);
}

void
nd_tab_css_result_free(nd_tab_css_result *result)
{
    if (!result) return;
    nd_response_free(result->resp);
    nd_css_stylesheet_free(result->sheet);
    g_free(result);
}

static gboolean
nd_tab_deliver_load(gpointer data)
{
    nd_tab_load_delivery *d = data;
    if (d->cb)
        d->cb(d->result, d->user_data);
    else
        nd_tab_load_result_free(d->result);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static gboolean
nd_tab_deliver_image(gpointer data)
{
    nd_tab_image_delivery *d = data;
    if (d->cb)
        d->cb(d->result, d->user_data);
    else
        nd_tab_image_result_free(d->result);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static gboolean
nd_tab_deliver_css(gpointer data)
{
    nd_tab_css_delivery *d = data;
    if (d->cb)
        d->cb(d->result, d->user_data);
    else
        nd_tab_css_result_free(d->result);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static void
nd_tab_worker_process_load(nd_tab_job *job)
{
    nd_tab_load_result *result = g_new0(nd_tab_load_result, 1);
    result->resp = job->resp;
    job->resp = NULL;
    if (result->resp && result->resp->body && result->resp->body->len > 0) {
        result->body = nd_html_decode_body(
            (const char *)result->resp->body->data,
            result->resp->body->len);
        if (!result->body) result->body = g_strdup("");
        result->body_len = strlen(result->body);
        if (job->parse_html) {
            result->doc = nd_html_parse(result->body,
                                        (gssize)result->body_len);
            result->parsed = result->doc != NULL;
        }
    }
    nd_tab_load_delivery *delivery = g_new0(nd_tab_load_delivery, 1);
    delivery->cb = (nd_tab_load_ready_cb)job->cb;
    delivery->result = result;
    delivery->user_data = job->user_data;
    job->user_data = NULL;
    g_main_context_invoke_full(NULL, G_PRIORITY_DEFAULT,
                               nd_tab_deliver_load, delivery, NULL);
}

static void
nd_tab_worker_process_image(nd_tab_job *job)
{
    nd_tab_image_result *result = g_new0(nd_tab_image_result, 1);
    result->resp = job->resp;
    job->resp = NULL;
    nd_response *resp = result->resp;
    if (resp && !resp->error && resp->status < 400 &&
        resp->body && resp->body->len > 0) {
        gboolean gif = resp->body->len >= 6 &&
            resp->body->data[0] == 'G' && resp->body->data[1] == 'I' &&
            resp->body->data[2] == 'F';
        if (gif) {
            result->anim_frames = nd_image_decode_wuffs_anim_to_pixels(
                resp->body->data, resp->body->len,
                &result->width, &result->height);
            if (result->anim_frames && result->anim_frames->len == 1) {
                nd_image_pixel_frame *f =
                    &g_array_index(result->anim_frames,
                                   nd_image_pixel_frame, 0);
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
            result->pixels = nd_image_decode_bytes_to_pixels(
                resp->body->data, resp->body->len,
                &result->width, &result->height, &result->stride,
                &result->pixels_len, &result->format);
        }
    }
    nd_tab_image_delivery *delivery = g_new0(nd_tab_image_delivery, 1);
    delivery->cb = (nd_tab_image_ready_cb)job->cb;
    delivery->result = result;
    delivery->user_data = job->user_data;
    job->user_data = NULL;
    g_main_context_invoke_full(NULL, G_PRIORITY_DEFAULT,
                               nd_tab_deliver_image, delivery, NULL);
}

static void
nd_tab_worker_process_css(nd_tab_job *job)
{
    nd_tab_css_result *result = g_new0(nd_tab_css_result, 1);
    result->resp = job->resp;
    job->resp = NULL;
    nd_response *resp = result->resp;
    if (resp && !resp->error && resp->status < 400 &&
        resp->body && resp->body->len > 0) {
        char *scoped = job->scope_id
            ? nd_css_scope_css((const char *)resp->body->data,
                               (gssize)resp->body->len, job->scope_id)
            : NULL;
        result->sheet = scoped
            ? nd_css_stylesheet_parse(scoped, (gssize)strlen(scoped))
            : nd_css_stylesheet_parse((const char *)resp->body->data,
                                      (gssize)resp->body->len);
        g_free(scoped);
    }
    nd_tab_css_delivery *delivery = g_new0(nd_tab_css_delivery, 1);
    delivery->cb = (nd_tab_css_ready_cb)job->cb;
    delivery->result = result;
    delivery->user_data = job->user_data;
    job->user_data = NULL;
    g_main_context_invoke_full(NULL, G_PRIORITY_DEFAULT,
                               nd_tab_deliver_css, delivery, NULL);
}

static gpointer
nd_tab_worker_thread(gpointer data)
{
    nd_tab_worker *worker = data;
    for (;;) {
        g_mutex_lock(&worker->lock);
        while (!worker->stopping && g_queue_is_empty(&worker->jobs))
            g_cond_wait(&worker->cond, &worker->lock);
        if (worker->stopping && g_queue_is_empty(&worker->jobs)) {
            g_mutex_unlock(&worker->lock);
            break;
        }
        nd_tab_job *job = g_queue_pop_head(&worker->jobs);
        g_mutex_unlock(&worker->lock);

        if (job->type == ND_TAB_JOB_LOAD)
            nd_tab_worker_process_load(job);
        else if (job->type == ND_TAB_JOB_IMAGE)
            nd_tab_worker_process_image(job);
        else if (job->type == ND_TAB_JOB_CSS)
            nd_tab_worker_process_css(job);
        nd_tab_job_free(job);
    }
    return NULL;
}

nd_tab_worker *
nd_tab_worker_new(const char *name)
{
    nd_tab_worker *worker = g_new0(nd_tab_worker, 1);
    g_mutex_init(&worker->lock);
    g_cond_init(&worker->cond);
    g_queue_init(&worker->jobs);
    GError *err = NULL;
    worker->thread = g_thread_try_new(name && *name ? name : "nd-tab",
                                      nd_tab_worker_thread, worker, &err);
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
nd_tab_worker_free(nd_tab_worker *worker)
{
    if (!worker) return;
    g_mutex_lock(&worker->lock);
    worker->stopping = TRUE;
    while (!g_queue_is_empty(&worker->jobs))
        nd_tab_job_free(g_queue_pop_head(&worker->jobs));
    g_cond_signal(&worker->cond);
    g_mutex_unlock(&worker->lock);
    g_thread_join(worker->thread);
    g_cond_clear(&worker->cond);
    g_mutex_clear(&worker->lock);
    g_free(worker);
}

static gboolean
nd_tab_worker_submit(nd_tab_worker *worker, nd_tab_job *job)
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
nd_tab_worker_load_response(nd_tab_worker *worker,
                            nd_response *resp,
                            gboolean parse_html,
                            nd_tab_load_ready_cb cb,
                            gpointer user_data,
                            GDestroyNotify user_data_destroy)
{
    nd_tab_job *job = g_new0(nd_tab_job, 1);
    job->type = ND_TAB_JOB_LOAD;
    job->resp = resp;
    job->parse_html = parse_html;
    job->cb = cb;
    job->user_data = user_data;
    job->user_data_destroy = user_data_destroy;
    if (nd_tab_worker_submit(worker, job)) return TRUE;
    job->resp = NULL;
    job->user_data = NULL;
    nd_tab_job_free(job);
    return FALSE;
}

gboolean
nd_tab_worker_decode_image_response(nd_tab_worker *worker,
                                    nd_response *resp,
                                    nd_tab_image_ready_cb cb,
                                    gpointer user_data,
                                    GDestroyNotify user_data_destroy)
{
    nd_tab_job *job = g_new0(nd_tab_job, 1);
    job->type = ND_TAB_JOB_IMAGE;
    job->resp = resp;
    job->cb = cb;
    job->user_data = user_data;
    job->user_data_destroy = user_data_destroy;
    if (nd_tab_worker_submit(worker, job)) return TRUE;
    job->resp = NULL;
    job->user_data = NULL;
    nd_tab_job_free(job);
    return FALSE;
}

gboolean
nd_tab_worker_parse_css_response(nd_tab_worker *worker,
                                 nd_response *resp,
                                 const char *scope_id,
                                 nd_tab_css_ready_cb cb,
                                 gpointer user_data,
                                 GDestroyNotify user_data_destroy)
{
    nd_tab_job *job = g_new0(nd_tab_job, 1);
    job->type = ND_TAB_JOB_CSS;
    job->resp = resp;
    job->scope_id = g_strdup(scope_id);
    job->cb = cb;
    job->user_data = user_data;
    job->user_data_destroy = user_data_destroy;
    if (nd_tab_worker_submit(worker, job)) return TRUE;
    job->resp = NULL;
    job->user_data = NULL;
    nd_tab_job_free(job);
    return FALSE;
}
