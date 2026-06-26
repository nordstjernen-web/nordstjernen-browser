/* Nordstjernen — WebExtensions loader and content-script host.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "ext.h"

#include <string.h>

#include <glib/gstdio.h>

#include "config.h"

typedef struct {
    GPtrArray *matches;
    char      *all_js;
    gboolean   at_start;
} ns_ext_cs;

typedef struct {
    char      *id;
    char      *name;
    char      *version;
    char      *base_dir;
    char      *manifest_json;
    GPtrArray *content_scripts;
} ns_ext;

static GPtrArray *g_exts;

static gboolean
ns_ext_area_ok(const char *area)
{
    return area && (strcmp(area, "local") == 0 ||
                    strcmp(area, "sync") == 0 ||
                    strcmp(area, "managed") == 0);
}

static gboolean
ns_ext_wildcard(const char *pat, const char *str)
{
    while (*pat) {
        if (*pat == '*') {
            pat++;
            if (!*pat) return TRUE;
            for (; *str; str++)
                if (ns_ext_wildcard(pat, str)) return TRUE;
            return FALSE;
        }
        if (*pat != *str) return FALSE;
        pat++;
        str++;
    }
    return *str == 0;
}

static void
ns_ext_url_split(const char *url, char **scheme, char **host, char **path)
{
    *scheme = NULL;
    *host = NULL;
    *path = NULL;
    if (!url) return;
    const char *sep = strstr(url, "://");
    if (!sep) return;
    *scheme = g_ascii_strdown(url, sep - url);
    const char *h = sep + 3;
    const char *he = h;
    while (*he && *he != '/' && *he != '?' && *he != '#') he++;
    *host = g_ascii_strdown(h, he - h);
    if (*he == '/') {
        const char *pe = he;
        while (*pe && *pe != '?' && *pe != '#') pe++;
        *path = g_strndup(he, pe - he);
    } else {
        *path = g_strdup("/");
    }
}

static gboolean
ns_ext_scheme_generic(const char *scheme)
{
    return scheme && (strcmp(scheme, "http") == 0 ||
                      strcmp(scheme, "https") == 0 ||
                      strcmp(scheme, "ws") == 0 ||
                      strcmp(scheme, "wss") == 0);
}

static gboolean
ns_ext_host_match(const char *pat, const char *host)
{
    if (!pat || !host) return FALSE;
    if (strcmp(pat, "*") == 0) return TRUE;
    if (pat[0] == '*' && pat[1] == '.') {
        const char *suffix = pat + 2;
        if (strcmp(host, suffix) == 0) return TRUE;
        gsize hl = strlen(host), sl = strlen(suffix);
        return hl > sl && host[hl - sl - 1] == '.' &&
               strcmp(host + hl - sl, suffix) == 0;
    }
    return strcmp(pat, host) == 0;
}

static gboolean
ns_ext_pattern_match(const char *pattern, const char *url)
{
    if (!pattern || !url) return FALSE;
    g_autofree char *uscheme = NULL;
    g_autofree char *uhost = NULL;
    g_autofree char *upath = NULL;
    ns_ext_url_split(url, &uscheme, &uhost, &upath);
    if (!uscheme || !uhost || !upath) return FALSE;

    if (strcmp(pattern, "<all_urls>") == 0)
        return ns_ext_scheme_generic(uscheme) ||
               strcmp(uscheme, "ftp") == 0 ||
               strcmp(uscheme, "file") == 0 ||
               strcmp(uscheme, "data") == 0;

    g_autofree char *pscheme = NULL;
    g_autofree char *phost = NULL;
    g_autofree char *ppath = NULL;
    ns_ext_url_split(pattern, &pscheme, &phost, &ppath);
    if (!pscheme || !phost || !ppath) return FALSE;

    if (strcmp(pscheme, "*") == 0) {
        if (!ns_ext_scheme_generic(uscheme)) return FALSE;
    } else if (strcmp(pscheme, uscheme) != 0) {
        return FALSE;
    }
    if (!ns_ext_host_match(phost, uhost)) return FALSE;
    return ns_ext_wildcard(ppath, upath);
}

static ns_ext *
ns_ext_lookup(const char *id)
{
    if (!g_exts || !id) return NULL;
    for (guint i = 0; i < g_exts->len; i++) {
        ns_ext *e = g_ptr_array_index(g_exts, i);
        if (e->id && strcmp(e->id, id) == 0) return e;
    }
    return NULL;
}

static char *
ns_ext_storage_path(const char *id, const char *area)
{
    const ns_config *c = ns_config_get();
    if (c && c->private_mode) return NULL;
    if (!id || !ns_ext_area_ok(area)) return NULL;
    g_autofree char *hash =
        g_compute_checksum_for_string(G_CHECKSUM_SHA256, id, -1);
    g_autofree char *dir = g_build_filename(g_get_user_data_dir(),
                                            NS_APP_DIR_NAME,
                                            "ext-storage", hash, NULL);
    g_mkdir_with_parents(dir, 0700);
    g_chmod(dir, 0700);
    g_autofree char *file = g_strdup_printf("%s.json", area);
    return g_build_filename(dir, file, NULL);
}

static JSValue
ns_ext_js_manifest(JSContext *ctx, JSValueConst this_val,
                   int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_NewString(ctx, "{}");
    const char *id = JS_ToCString(ctx, argv[0]);
    ns_ext *e = ns_ext_lookup(id);
    JSValue r = JS_NewString(ctx, e && e->manifest_json ? e->manifest_json : "{}");
    JS_FreeCString(ctx, id);
    return r;
}

static JSValue
ns_ext_js_base(JSContext *ctx, JSValueConst this_val,
               int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_NewString(ctx, "");
    const char *id = JS_ToCString(ctx, argv[0]);
    ns_ext *e = ns_ext_lookup(id);
    JSValue r = JS_NewString(ctx, e && e->base_dir ? e->base_dir : "");
    JS_FreeCString(ctx, id);
    return r;
}

static JSValue
ns_ext_js_sread(JSContext *ctx, JSValueConst this_val,
                int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) return JS_NewString(ctx, "{}");
    const char *id = JS_ToCString(ctx, argv[0]);
    const char *area = JS_ToCString(ctx, argv[1]);
    char *out = NULL;
    g_autofree char *path = ns_ext_storage_path(id, area);
    if (path) {
        char *data = NULL;
        if (g_file_get_contents(path, &data, NULL, NULL)) out = data;
    }
    JSValue r = JS_NewString(ctx, out ? out : "{}");
    g_free(out);
    JS_FreeCString(ctx, id);
    JS_FreeCString(ctx, area);
    return r;
}

static JSValue
ns_ext_js_swrite(JSContext *ctx, JSValueConst this_val,
                 int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 3) return JS_FALSE;
    const char *id = JS_ToCString(ctx, argv[0]);
    const char *area = JS_ToCString(ctx, argv[1]);
    const char *json = JS_ToCString(ctx, argv[2]);
    gboolean ok = FALSE;
    g_autofree char *path = ns_ext_storage_path(id, area);
    if (path && json && g_file_set_contents(path, json, -1, NULL)) {
        g_chmod(path, 0600);
        ok = TRUE;
    }
    JS_FreeCString(ctx, id);
    JS_FreeCString(ctx, area);
    JS_FreeCString(ctx, json);
    return JS_NewBool(ctx, ok);
}

static JSValue
ns_ext_js_platform(JSContext *ctx, JSValueConst this_val,
                   int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
#if defined(G_OS_WIN32)
    return JS_NewString(ctx, "win");
#elif defined(__APPLE__)
    return JS_NewString(ctx, "mac");
#else
    return JS_NewString(ctx, "linux");
#endif
}

static JSValue
ns_ext_js_uilang(JSContext *ctx, JSValueConst this_val,
                 int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    const char * const *names = g_get_language_names();
    const char *src = names && names[0] ? names[0] : "en";
    GString *out = g_string_new(NULL);
    for (const char *p = src; *p && *p != '.' && *p != '@'; p++)
        g_string_append_c(out, *p == '_' ? '-' : *p);
    if (out->len == 0) g_string_append(out, "en");
    JSValue r = JS_NewString(ctx, out->str);
    g_string_free(out, TRUE);
    return r;
}

static const char ns_ext_shim[] =
"(function(){"
"var M=globalThis.__nd_ext_manifest,B=globalThis.__nd_ext_base,"
"SR=globalThis.__nd_ext_sread,SW=globalThis.__nd_ext_swrite,"
"PL=globalThis.__nd_ext_platform,UL=globalThis.__nd_ext_uilang;"
"function area(id,name){"
"function rd(){try{return JSON.parse(SR(id,name))||{};}catch(e){return{};}}"
"function wr(o){return SW(id,name,JSON.stringify(o));}"
"return {get:function(keys){return new Promise(function(res){var a=rd(),o={};"
"if(keys==null)o=a;"
"else if(typeof keys==='string'){if(keys in a)o[keys]=a[keys];}"
"else if(Array.isArray(keys)){keys.forEach(function(k){if(k in a)o[k]=a[k];});}"
"else if(typeof keys==='object'){Object.keys(keys).forEach(function(k){o[k]=(k in a)?a[k]:keys[k];});}"
"res(o);});},"
"set:function(items){return new Promise(function(res){var a=rd();"
"Object.keys(items||{}).forEach(function(k){a[k]=items[k];});wr(a);res();});},"
"remove:function(keys){return new Promise(function(res){var a=rd();"
"(Array.isArray(keys)?keys:[keys]).forEach(function(k){delete a[k];});wr(a);res();});},"
"clear:function(){return new Promise(function(res){wr({});res();});}};}"
"globalThis.__nd_ext_make_api=function(id){"
"var man=null;"
"function getManifest(){if(man===null){try{man=JSON.parse(M(id));}catch(e){man={};}}return man;}"
"function getURL(p){var b=B(id);p=String(p==null?'':p);"
"if(p.charAt(0)==='/')p=p.slice(1);return b?('file://'+b+'/'+p):p;}"
"var listeners=[];"
"var runtime={id:id,lastError:null,getManifest:getManifest,getURL:getURL,"
"getPlatformInfo:function(){return Promise.resolve({os:PL(),arch:'x86-64'});},"
"sendMessage:function(){var msg=arguments.length>1?arguments[1]:arguments[0];"
"return new Promise(function(res){var s={id:id},rep;"
"for(var i=0;i<listeners.length;i++){try{var r=listeners[i](msg,s,function(x){rep=x;});"
"if(r&&typeof r.then==='function'){r.then(res);return;}}catch(e){}}"
"res(rep);});},"
"onMessage:{addListener:function(f){if(typeof f==='function')listeners.push(f);},"
"removeListener:function(f){var i=listeners.indexOf(f);if(i>=0)listeners.splice(i,1);},"
"hasListener:function(f){return listeners.indexOf(f)>=0;}}};"
"var i18n={getMessage:function(k){return k==null?'':String(k);},"
"getUILanguage:function(){return UL();},"
"getAcceptLanguages:function(){return Promise.resolve([UL()]);}};"
"return {runtime:runtime,i18n:i18n,extension:{getURL:getURL},"
"storage:{local:area(id,'local'),sync:area(id,'sync'),managed:area(id,'managed')}};"
"};"
"delete globalThis.__nd_ext_manifest;delete globalThis.__nd_ext_base;"
"delete globalThis.__nd_ext_sread;delete globalThis.__nd_ext_swrite;"
"delete globalThis.__nd_ext_platform;delete globalThis.__nd_ext_uilang;"
"})();";

static char *
ns_ext_js_string(JSContext *ctx, JSValueConst obj, const char *key)
{
    JSValue v = JS_GetPropertyStr(ctx, obj, key);
    char *out = NULL;
    if (JS_IsString(v)) {
        const char *s = JS_ToCString(ctx, v);
        if (s) out = g_strdup(s);
        JS_FreeCString(ctx, s);
    }
    JS_FreeValue(ctx, v);
    return out;
}

static void
ns_ext_collect_strings(JSContext *ctx, JSValueConst arr, GPtrArray *out)
{
    if (!JS_IsObject(arr)) return;
    JSValue lenv = JS_GetPropertyStr(ctx, arr, "length");
    uint32_t n = 0;
    JS_ToUint32(ctx, &n, lenv);
    JS_FreeValue(ctx, lenv);
    for (uint32_t i = 0; i < n; i++) {
        JSValue v = JS_GetPropertyUint32(ctx, arr, i);
        const char *s = JS_ToCString(ctx, v);
        if (s) g_ptr_array_add(out, g_strdup(s));
        JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, v);
    }
}

static char *
ns_ext_read_js_files(const char *base_dir, JSContext *ctx, JSValueConst entry)
{
    JSValue jsv = JS_GetPropertyStr(ctx, entry, "js");
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
    ns_ext_collect_strings(ctx, jsv, files);
    JS_FreeValue(ctx, jsv);
    GString *src = g_string_new(NULL);
    for (guint i = 0; i < files->len; i++) {
        const char *rel = g_ptr_array_index(files, i);
        g_autofree char *path = g_build_filename(base_dir, rel, NULL);
        char *data = NULL;
        if (g_file_get_contents(path, &data, NULL, NULL)) {
            g_string_append(src, data);
            g_string_append_c(src, '\n');
            g_free(data);
        }
    }
    g_ptr_array_free(files, TRUE);
    if (src->len == 0) { g_string_free(src, TRUE); return NULL; }
    return g_string_free(src, FALSE);
}

static void
ns_ext_cs_free(gpointer p)
{
    ns_ext_cs *cs = p;
    if (cs->matches) g_ptr_array_free(cs->matches, TRUE);
    g_free(cs->all_js);
    g_free(cs);
}

static void
ns_ext_parse_content_scripts(ns_ext *e, JSContext *ctx, JSValueConst manifest)
{
    JSValue arr = JS_GetPropertyStr(ctx, manifest, "content_scripts");
    if (JS_IsObject(arr)) {
        JSValue lenv = JS_GetPropertyStr(ctx, arr, "length");
        uint32_t n = 0;
        JS_ToUint32(ctx, &n, lenv);
        JS_FreeValue(ctx, lenv);
        for (uint32_t i = 0; i < n; i++) {
            JSValue entry = JS_GetPropertyUint32(ctx, arr, i);
            if (JS_IsObject(entry)) {
                ns_ext_cs *cs = g_new0(ns_ext_cs, 1);
                cs->matches = g_ptr_array_new_with_free_func(g_free);
                JSValue m = JS_GetPropertyStr(ctx, entry, "matches");
                ns_ext_collect_strings(ctx, m, cs->matches);
                JS_FreeValue(ctx, m);
                g_autofree char *run_at =
                    ns_ext_js_string(ctx, entry, "run_at");
                cs->at_start = run_at && strcmp(run_at, "document_start") == 0;
                cs->all_js = ns_ext_read_js_files(e->base_dir, ctx, entry);
                if (cs->matches->len > 0 && cs->all_js)
                    g_ptr_array_add(e->content_scripts, cs);
                else
                    ns_ext_cs_free(cs);
            }
            JS_FreeValue(ctx, entry);
        }
    }
    JS_FreeValue(ctx, arr);
}

static char *
ns_ext_parse_id(JSContext *ctx, JSValueConst manifest, const char *dir)
{
    const char *keys[] = { "browser_specific_settings", "applications" };
    for (guint i = 0; i < G_N_ELEMENTS(keys); i++) {
        JSValue bss = JS_GetPropertyStr(ctx, manifest, keys[i]);
        if (JS_IsObject(bss)) {
            JSValue gecko = JS_GetPropertyStr(ctx, bss, "gecko");
            char *id = NULL;
            if (JS_IsObject(gecko))
                id = ns_ext_js_string(ctx, gecko, "id");
            JS_FreeValue(ctx, gecko);
            JS_FreeValue(ctx, bss);
            if (id) return id;
        } else {
            JS_FreeValue(ctx, bss);
        }
    }
    return g_path_get_basename(dir);
}

static void
ns_ext_load_one(const char *dir, JSContext *ctx)
{
    g_autofree char *mpath = g_build_filename(dir, "manifest.json", NULL);
    char *raw = NULL;
    gsize len = 0;
    if (!g_file_get_contents(mpath, &raw, &len, NULL)) return;
    JSValue obj = JS_ParseJSON(ctx, raw, len, mpath);
    if (JS_IsException(obj)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        g_free(raw);
        return;
    }
    ns_ext *e = g_new0(ns_ext, 1);
    e->base_dir = g_strdup(dir);
    e->manifest_json = raw;
    e->name = ns_ext_js_string(ctx, obj, "name");
    e->version = ns_ext_js_string(ctx, obj, "version");
    e->id = ns_ext_parse_id(ctx, obj, dir);
    e->content_scripts = g_ptr_array_new_with_free_func(ns_ext_cs_free);
    ns_ext_parse_content_scripts(e, ctx, obj);
    JS_FreeValue(ctx, obj);
    g_ptr_array_add(g_exts, e);
}

static void
ns_ext_scan_root(const char *root, JSContext *ctx)
{
    if (!root || !*root) return;
    g_autofree char *self = g_build_filename(root, "manifest.json", NULL);
    if (g_file_test(self, G_FILE_TEST_EXISTS)) {
        ns_ext_load_one(root, ctx);
        return;
    }
    GDir *d = g_dir_open(root, 0, NULL);
    if (!d) return;
    const char *name;
    while ((name = g_dir_read_name(d))) {
        g_autofree char *child = g_build_filename(root, name, NULL);
        if (g_file_test(child, G_FILE_TEST_IS_DIR))
            ns_ext_load_one(child, ctx);
    }
    g_dir_close(d);
}

static void
ns_ext_init(void)
{
    static gboolean done;
    if (done) return;
    done = TRUE;
    g_exts = g_ptr_array_new();

    JSRuntime *rt = JS_NewRuntime();
    if (!rt) return;
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return; }

    const char *envd = g_getenv("NS_EXTENSIONS_DIR");
    if (envd && *envd) {
        gchar **parts = g_strsplit(envd, G_SEARCHPATH_SEPARATOR_S, -1);
        for (gchar **p = parts; *p; p++)
            if (**p) ns_ext_scan_root(*p, ctx);
        g_strfreev(parts);
    }
    g_autofree char *def = g_build_filename(g_get_user_data_dir(),
                                            NS_APP_DIR_NAME,
                                            "extensions", NULL);
    ns_ext_scan_root(def, ctx);

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}

guint
ns_ext_count(void)
{
    ns_ext_init();
    return g_exts ? g_exts->len : 0;
}

void
ns_ext_install(JSContext *ctx, JSValueConst global)
{
    if (ns_ext_count() == 0) return;
    JS_SetPropertyStr(ctx, global, "__nd_ext_manifest",
                      JS_NewCFunction(ctx, ns_ext_js_manifest, "m", 1));
    JS_SetPropertyStr(ctx, global, "__nd_ext_base",
                      JS_NewCFunction(ctx, ns_ext_js_base, "b", 1));
    JS_SetPropertyStr(ctx, global, "__nd_ext_sread",
                      JS_NewCFunction(ctx, ns_ext_js_sread, "r", 2));
    JS_SetPropertyStr(ctx, global, "__nd_ext_swrite",
                      JS_NewCFunction(ctx, ns_ext_js_swrite, "w", 3));
    JS_SetPropertyStr(ctx, global, "__nd_ext_platform",
                      JS_NewCFunction(ctx, ns_ext_js_platform, "p", 0));
    JS_SetPropertyStr(ctx, global, "__nd_ext_uilang",
                      JS_NewCFunction(ctx, ns_ext_js_uilang, "l", 0));
    JSValue r = JS_Eval(ctx, ns_ext_shim, strlen(ns_ext_shim),
                        "<ext-shim>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) JS_FreeValue(ctx, JS_GetException(ctx));
    JS_FreeValue(ctx, r);
}

static void
ns_ext_append_id(GString *out, const char *id)
{
    for (const char *p = id; *p; p++) {
        if (*p == '\\' || *p == '"') g_string_append_c(out, '\\');
        g_string_append_c(out, *p);
    }
}

char *
ns_ext_content_scripts_for_url(const char *url, gboolean at_start)
{
    ns_ext_init();
    if (!url || !*url || g_exts->len == 0) return NULL;
    GString *out = g_string_new(NULL);
    for (guint i = 0; i < g_exts->len; i++) {
        ns_ext *e = g_ptr_array_index(g_exts, i);
        for (guint j = 0; j < e->content_scripts->len; j++) {
            ns_ext_cs *cs = g_ptr_array_index(e->content_scripts, j);
            if (cs->at_start != at_start) continue;
            gboolean hit = FALSE;
            for (guint k = 0; k < cs->matches->len && !hit; k++)
                hit = ns_ext_pattern_match(g_ptr_array_index(cs->matches, k),
                                           url);
            if (!hit) continue;
            g_string_append(out,
                ";(function(){try{var browser=__nd_ext_make_api(\"");
            ns_ext_append_id(out, e->id);
            g_string_append(out, "\");var chrome=browser;\n");
            g_string_append(out, cs->all_js);
            g_string_append(out,
                "\n}catch(e){try{console.error(\"[nordstjernen ext]\",e);}"
                "catch(_){}}})();\n");
        }
    }
    if (out->len == 0) { g_string_free(out, TRUE); return NULL; }
    return g_string_free(out, FALSE);
}
