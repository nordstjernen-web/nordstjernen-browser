/* Nordstjernen — local on-CPU chat inference over llama.cpp.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "ai.h"
#include "config.h"

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

#ifndef NS_HAVE_AI

gboolean
ns_ai_available(void)
{
    return FALSE;
}

void
ns_ai_select_download(const char *model_id)
{
    (void)model_id;
}

char *
ns_ai_status_json(void)
{
    return g_strdup("{\"state\":\"disabled\",\"models\":[]}");
}

char *
ns_ai_chat(const char *user_msg)
{
    (void)user_msg;
    return g_strdup("This build of Nordstjernen was compiled without the "
                    "local AI feature.");
}

#else

#include <stdio.h>
#include <curl/curl.h>

#include "llama.h"

#define NS_AI_N_CTX        2048
#define NS_AI_MAX_REPLY    320
#define NS_AI_SYSTEM_PROMPT \
    "You are the assistant built into the Nordstjernen web browser. " \
    "Answer the user concisely and helpfully."

typedef struct {
    const char *id;
    const char *label;
    const char *file;
    const char *url;
    int         mb;
} ns_ai_model;

static const ns_ai_model k_models[] = {
    {
        "fast", "Qwen2.5 0.5B", "qwen2.5-0.5b-instruct-q4_k_m.gguf",
        "https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/"
        "qwen2.5-0.5b-instruct-q4_k_m.gguf?download=true", 470,
    },
    {
        "balanced", "Qwen2.5 1.5B", "qwen2.5-1.5b-instruct-q4_k_m.gguf",
        "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/"
        "qwen2.5-1.5b-instruct-q4_k_m.gguf?download=true", 1010,
    },
    {
        "quality", "Qwen2.5 3B", "qwen2.5-3b-instruct-q4_k_m.gguf",
        "https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/main/"
        "qwen2.5-3b-instruct-q4_k_m.gguf?download=true", 1930,
    },
};

static struct llama_model       *g_model;
static struct llama_context     *g_ctx;
static const struct llama_vocab *g_vocab;
static char                     *g_loaded_path;
static GMutex                    g_model_lock;

static char    *g_active_id;
static gboolean g_downloading;
static char    *g_dl_id;
static char    *g_dl_error;
static gint64   g_dl_now;
static gint64   g_dl_total;
static GMutex   g_dl_lock;

static const ns_ai_model *
ns_ai_model_by_id(const char *id)
{
    if (!id) return NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(k_models); i++)
        if (g_str_equal(k_models[i].id, id))
            return &k_models[i];
    return NULL;
}

static char *
ns_ai_models_dir(void)
{
    return g_build_filename(g_get_user_data_dir(), NS_APP_DIR_NAME, "models",
                            NULL);
}

static char *
ns_ai_model_path(const ns_ai_model *m)
{
    char *dir = ns_ai_models_dir();
    char *path = g_build_filename(dir, m->file, NULL);
    g_free(dir);
    return path;
}

static gboolean
ns_ai_model_installed(const ns_ai_model *m)
{
    char *path = ns_ai_model_path(m);
    gboolean ok = g_file_test(path, G_FILE_TEST_IS_REGULAR);
    g_free(path);
    return ok;
}

static const ns_ai_model *
ns_ai_first_installed(void)
{
    for (gsize i = 0; i < G_N_ELEMENTS(k_models); i++)
        if (ns_ai_model_installed(&k_models[i]))
            return &k_models[i];
    return NULL;
}

static const ns_ai_model *
ns_ai_active_model_locked(void)
{
    const ns_ai_model *m = ns_ai_model_by_id(g_active_id);
    if (m && ns_ai_model_installed(m))
        return m;
    return ns_ai_first_installed();
}

static char *
ns_ai_active_path(void)
{
    const char *env = g_getenv("NORDSTJERNEN_AI_MODEL");
    if (env && *env && g_file_test(env, G_FILE_TEST_IS_REGULAR))
        return g_strdup(env);

    g_mutex_lock(&g_dl_lock);
    const ns_ai_model *m = ns_ai_active_model_locked();
    char *path = m ? ns_ai_model_path(m) : NULL;
    g_mutex_unlock(&g_dl_lock);
    return path;
}

gboolean
ns_ai_available(void)
{
    char *path = ns_ai_active_path();
    gboolean ok = path != NULL;
    g_free(path);
    return ok;
}

static int
ns_ai_xfer_cb(void *ud, curl_off_t dltotal, curl_off_t dlnow,
              curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ud; (void)ultotal; (void)ulnow;
    g_mutex_lock(&g_dl_lock);
    g_dl_now = (gint64)dlnow;
    g_dl_total = (gint64)dltotal;
    g_mutex_unlock(&g_dl_lock);
    return 0;
}

typedef struct {
    char *url;
    char *path;
} ns_ai_dl_job;

static gpointer
ns_ai_download_thread(gpointer data)
{
    ns_ai_dl_job *job = data;
    char *dir = ns_ai_models_dir();
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    char *part = g_strconcat(job->path, ".part", NULL);

    char *err = NULL;
    FILE *f = g_fopen(part, "wb");
    if (!f) {
        err = g_strdup("could not open destination file");
    } else {
        CURL *c = curl_easy_init();
        if (!c) {
            err = g_strdup("could not initialise libcurl");
        } else {
            curl_easy_setopt(c, CURLOPT_URL, job->url);
            curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, fwrite);
            curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
            curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
            curl_easy_setopt(c, CURLOPT_USERAGENT, "Nordstjernen-AI/1.0");
            curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, ns_ai_xfer_cb);
            curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
            CURLcode rc = curl_easy_perform(c);
            if (rc != CURLE_OK)
                err = g_strdup(curl_easy_strerror(rc));
            curl_easy_cleanup(c);
        }
        fclose(f);
    }

    if (!err) {
        if (g_rename(part, job->path) != 0)
            err = g_strdup("could not move downloaded model into place");
    } else {
        g_remove(part);
    }

    g_mutex_lock(&g_dl_lock);
    g_free(g_dl_error);
    g_dl_error = err;
    g_downloading = FALSE;
    g_free(g_dl_id);
    g_dl_id = NULL;
    g_mutex_unlock(&g_dl_lock);

    g_free(part);
    g_free(job->url);
    g_free(job->path);
    g_free(job);
    return NULL;
}

void
ns_ai_select_download(const char *model_id)
{
    const ns_ai_model *m = ns_ai_model_by_id(model_id);
    if (!m) return;

    ns_ai_dl_job *job = NULL;
    g_mutex_lock(&g_dl_lock);
    g_free(g_active_id);
    g_active_id = g_strdup(m->id);

    if (!ns_ai_model_installed(m) && !g_downloading) {
        const char *env = g_getenv("NORDSTJERNEN_AI_MODEL_URL");
        job = g_new0(ns_ai_dl_job, 1);
        job->url = g_strdup(env && *env ? env : m->url);
        job->path = ns_ai_model_path(m);
        g_downloading = TRUE;
        g_free(g_dl_id);
        g_dl_id = g_strdup(m->id);
        g_free(g_dl_error);
        g_dl_error = NULL;
        g_dl_now = 0;
        g_dl_total = 0;
    }
    g_mutex_unlock(&g_dl_lock);

    if (job) {
        GThread *t = g_thread_new("ns-ai-download", ns_ai_download_thread, job);
        if (t) g_thread_unref(t);
    }
}

static char *
ns_ai_json_escape(const char *s)
{
    GString *o = g_string_new(NULL);
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\') g_string_append_c(o, '\\');
        if ((guchar)*s < 0x20) { g_string_append_printf(o, "\\u%04x", *s); continue; }
        g_string_append_c(o, *s);
    }
    return g_string_free(o, FALSE);
}

char *
ns_ai_status_json(void)
{
    g_mutex_lock(&g_dl_lock);
    gboolean downloading = g_downloading;
    char *dl_id = g_strdup(g_dl_id);
    char *err = g_strdup(g_dl_error);
    gint64 now = g_dl_now, total = g_dl_total;
    const ns_ai_model *active = ns_ai_active_model_locked();
    char *active_id = g_strdup(active ? active->id : NULL);
    g_mutex_unlock(&g_dl_lock);

    GString *models = g_string_new("[");
    for (gsize i = 0; i < G_N_ELEMENTS(k_models); i++) {
        const ns_ai_model *m = &k_models[i];
        g_string_append_printf(models,
            "%s{\"id\":\"%s\",\"label\":\"%s\",\"mb\":%d,\"installed\":%s}",
            i ? "," : "", m->id, m->label, m->mb,
            ns_ai_model_installed(m) ? "true" : "false");
    }
    g_string_append_c(models, ']');

    const char *state = downloading ? "downloading"
                      : active_id   ? "ready"
                      : err         ? "error" : "idle";

    GString *out = g_string_new(NULL);
    g_string_append_printf(out, "{\"state\":\"%s\",\"models\":%s",
                           state, models->str);
    if (active_id)
        g_string_append_printf(out, ",\"active\":\"%s\"", active_id);
    if (downloading) {
        int pct = total > 0 ? (int)((now * 100) / total) : 0;
        g_string_append_printf(out,
            ",\"downloading\":\"%s\",\"percent\":%d,"
            "\"received\":%" G_GINT64_FORMAT ",\"total\":%" G_GINT64_FORMAT,
            dl_id ? dl_id : "", pct, now, total);
    }
    if (err && !downloading && !active_id) {
        char *esc = ns_ai_json_escape(err);
        g_string_append_printf(out, ",\"message\":\"%s\"", esc);
        g_free(esc);
    }
    g_string_append_c(out, '}');

    g_string_free(models, TRUE);
    g_free(dl_id);
    g_free(err);
    g_free(active_id);
    return g_string_free(out, FALSE);
}

static void
ns_ai_log_sink(enum ggml_log_level level, const char *text, void *ud)
{
    (void)ud;
    if (level >= GGML_LOG_LEVEL_ERROR && text)
        g_printerr("%s", text);
}

static void
ns_ai_unload_locked(void)
{
    if (g_ctx) { llama_free(g_ctx); g_ctx = NULL; }
    if (g_model) { llama_model_free(g_model); g_model = NULL; }
    g_vocab = NULL;
    g_free(g_loaded_path);
    g_loaded_path = NULL;
}

static gboolean
ns_ai_ensure_loaded_locked(void)
{
    char *path = ns_ai_active_path();
    if (!path) return FALSE;

    if (g_loaded_path && g_str_equal(g_loaded_path, path)) {
        g_free(path);
        return TRUE;
    }
    ns_ai_unload_locked();

    llama_log_set(ns_ai_log_sink, NULL);
    llama_backend_init();

    struct llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 0;
    g_model = llama_model_load_from_file(path, mparams);
    if (!g_model) { g_free(path); return FALSE; }

    struct llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx           = NS_AI_N_CTX;
    cparams.n_batch         = NS_AI_N_CTX;
    cparams.n_threads       = (int32_t)g_get_num_processors();
    cparams.n_threads_batch = (int32_t)g_get_num_processors();
    g_ctx = llama_init_from_model(g_model, cparams);
    if (!g_ctx) {
        llama_model_free(g_model);
        g_model = NULL;
        g_free(path);
        return FALSE;
    }

    g_vocab = llama_model_get_vocab(g_model);
    g_loaded_path = path;
    return TRUE;
}

static char *
ns_ai_build_prompt(const char *user_msg)
{
    const char *tmpl = llama_model_chat_template(g_model, NULL);
    struct llama_chat_message msgs[2] = {
        { "system", NS_AI_SYSTEM_PROMPT },
        { "user",   user_msg },
    };

    if (tmpl) {
        int need = llama_chat_apply_template(tmpl, msgs, 2, true, NULL, 0);
        if (need > 0) {
            char *buf = g_malloc((gsize)need + 1);
            int n = llama_chat_apply_template(tmpl, msgs, 2, true, buf, need + 1);
            if (n > 0) {
                buf[n] = '\0';
                return buf;
            }
            g_free(buf);
        }
    }

    return g_strdup_printf(
        "<|im_start|>system\n%s<|im_end|>\n"
        "<|im_start|>user\n%s<|im_end|>\n"
        "<|im_start|>assistant\n",
        NS_AI_SYSTEM_PROMPT, user_msg);
}

static char *
ns_ai_run_locked(const char *user_msg)
{
    char *prompt = ns_ai_build_prompt(user_msg);

    int n_max = (int)strlen(prompt) + 8;
    llama_token *toks = g_malloc(sizeof(llama_token) * (gsize)n_max);
    int n_prompt = llama_tokenize(g_vocab, prompt, (int)strlen(prompt),
                                  toks, n_max, true, true);
    g_free(prompt);
    if (n_prompt <= 0) {
        g_free(toks);
        return NULL;
    }

    llama_memory_clear(llama_get_memory(g_ctx), true);

    struct llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    struct llama_sampler *smpl = llama_sampler_chain_init(sp);
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(0.95f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.7f));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    GString *out = g_string_new(NULL);
    struct llama_batch batch = llama_batch_get_one(toks, n_prompt);
    int n_decoded = 0;
    gboolean failed = FALSE;

    while (n_decoded < NS_AI_MAX_REPLY) {
        if (llama_decode(g_ctx, batch) != 0) {
            failed = out->len == 0;
            break;
        }
        llama_token id = llama_sampler_sample(smpl, g_ctx, -1);
        if (llama_vocab_is_eog(g_vocab, id))
            break;

        char piece[256];
        int pn = llama_token_to_piece(g_vocab, id, piece, sizeof piece, 0, false);
        if (pn > 0)
            g_string_append_len(out, piece, pn);

        batch = llama_batch_get_one(&id, 1);
        n_decoded++;
    }

    llama_sampler_free(smpl);
    g_free(toks);

    if (failed) {
        g_string_free(out, TRUE);
        return NULL;
    }
    return g_string_free(out, FALSE);
}

char *
ns_ai_chat(const char *user_msg)
{
    if (!user_msg || !*user_msg)
        return g_strdup("Please type a message.");

    g_mutex_lock(&g_model_lock);
    char *reply = NULL;
    if (ns_ai_ensure_loaded_locked())
        reply = ns_ai_run_locked(user_msg);
    g_mutex_unlock(&g_model_lock);

    if (!reply)
        reply = g_strdup("The local AI model is not ready yet. Pick and "
                         "download a model from the start page first.");
    return reply;
}

#endif
