/* Nordstjernen — local on-CPU chat inference over llama.cpp.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "ai.h"

#include <stdio.h>
#include <string.h>
#include <glib.h>

#include "llama.h"
#include "config.h"

#define NS_AI_N_CTX        2048
#define NS_AI_MAX_REPLY    320
#define NS_AI_SYSTEM_PROMPT \
    "You are the assistant built into the Nordstjernen web browser. " \
    "Answer the user concisely and helpfully."

typedef struct {
    struct llama_model        *model;
    struct llama_context      *ctx;
    const struct llama_vocab  *vocab;
    char                      *name;
    gboolean                   tried;
    gboolean                   ready;
} ns_ai_state;

static ns_ai_state g_ai;
static GMutex      g_ai_lock;

static void
ns_ai_log_sink(enum ggml_log_level level, const char *text, void *ud)
{
    (void)ud;
    if (level >= GGML_LOG_LEVEL_ERROR && text)
        g_printerr("%s", text);
}

static char *
ns_ai_locate_model(void)
{
    const char *env = g_getenv("NORDSTJERNEN_AI_MODEL");
    if (env && *env && g_file_test(env, G_FILE_TEST_IS_REGULAR))
        return g_strdup(env);

    GPtrArray *dirs = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(dirs, g_build_filename(g_get_user_data_dir(),
                                           NS_APP_DIR_NAME, "models", NULL));
    g_ptr_array_add(dirs, g_build_filename(g_get_current_dir(), "models", NULL));
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    if (exe) {
        char *exedir = g_path_get_dirname(exe);
        g_ptr_array_add(dirs, g_build_filename(exedir, "..", "models", NULL));
        g_ptr_array_add(dirs, g_build_filename(exedir, "models", NULL));
        g_free(exedir);
        g_free(exe);
    }

    char *found = NULL;
    for (guint i = 0; i < dirs->len && !found; i++) {
        const char *dir = g_ptr_array_index(dirs, i);
        GDir *d = g_dir_open(dir, 0, NULL);
        if (!d) continue;
        const char *ent;
        while ((ent = g_dir_read_name(d))) {
            if (g_str_has_suffix(ent, ".gguf")) {
                found = g_build_filename(dir, ent, NULL);
                break;
            }
        }
        g_dir_close(d);
    }
    g_ptr_array_free(dirs, TRUE);
    return found;
}

static void
ns_ai_ensure_locked(void)
{
    if (g_ai.tried) return;
    g_ai.tried = TRUE;

    char *path = ns_ai_locate_model();
    if (!path) return;

    llama_log_set(ns_ai_log_sink, NULL);
    llama_backend_init();

    struct llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 0;
    g_ai.model = llama_model_load_from_file(path, mparams);
    if (!g_ai.model) {
        g_printerr("nordstjernen: failed to load AI model %s\n", path);
        g_free(path);
        return;
    }

    struct llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx       = NS_AI_N_CTX;
    cparams.n_batch     = NS_AI_N_CTX;
    cparams.n_threads       = (int32_t)g_get_num_processors();
    cparams.n_threads_batch = (int32_t)g_get_num_processors();
    g_ai.ctx = llama_init_from_model(g_ai.model, cparams);
    if (!g_ai.ctx) {
        llama_model_free(g_ai.model);
        g_ai.model = NULL;
        g_free(path);
        return;
    }

    g_ai.vocab = llama_model_get_vocab(g_ai.model);
    g_ai.name  = g_path_get_basename(path);
    g_ai.ready = TRUE;
    g_free(path);
}

gboolean
ns_ai_available(void)
{
    char *path = ns_ai_locate_model();
    gboolean ok = path != NULL;
    g_free(path);
    return ok;
}

const char *
ns_ai_model_name(void)
{
    return g_ai.name;
}

static char *
ns_ai_build_prompt(const char *user_msg)
{
    const char *tmpl = llama_model_chat_template(g_ai.model, NULL);
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
ns_ai_run(const char *user_msg)
{
    char *prompt = ns_ai_build_prompt(user_msg);

    int n_max = (int)strlen(prompt) + 8;
    llama_token *toks = g_malloc(sizeof(llama_token) * (gsize)n_max);
    int n_prompt = llama_tokenize(g_ai.vocab, prompt, (int)strlen(prompt),
                                  toks, n_max, true, true);
    g_free(prompt);
    if (n_prompt <= 0) {
        g_free(toks);
        return NULL;
    }

    llama_memory_clear(llama_get_memory(g_ai.ctx), true);

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
        if (llama_decode(g_ai.ctx, batch) != 0) {
            failed = out->len == 0;
            break;
        }
        llama_token id = llama_sampler_sample(smpl, g_ai.ctx, -1);
        if (llama_vocab_is_eog(g_ai.vocab, id))
            break;

        char piece[256];
        int pn = llama_token_to_piece(g_ai.vocab, id, piece, sizeof piece, 0,
                                      false);
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

    g_mutex_lock(&g_ai_lock);
    ns_ai_ensure_locked();
    char *reply = NULL;
    if (g_ai.ready)
        reply = ns_ai_run(user_msg);
    g_mutex_unlock(&g_ai_lock);

    if (!reply)
        reply = g_strdup("The local AI model is unavailable. Place a .gguf "
                         "model under the browser's models directory.");
    return reply;
}
