/* Nordstjernen — IMAP/POP3/SMTP email client backend over libcurl. */

#include "mail.h"

#include <string.h>
#include <curl/curl.h>
#include <glib/gstdio.h>
#include <openssl/crypto.h>

#include "config.h"
#include "net.h"
#include "secretbox.h"

enum { ST_IDLE, ST_RUNNING, ST_DONE, ST_ERROR };

typedef struct {
    char *uid;
    char *from;
    char *to;
    char *subject;
    char *date;
    char *body;
} ns_mail_hdr;

typedef struct {
    char   *filename;
    char   *content_type;
    guint8 *data;
    gsize   len;
} ns_mail_attach;

#define NS_MAIL_ATT_MAX (20u * 1024u * 1024u)

typedef struct {
    char *in_proto;
    char *in_host;
    int   in_port;
    char *in_sec;
    char *in_user;
    char *in_pass;
    char *out_host;
    int   out_port;
    char *out_sec;
    char *out_user;
    char *out_pass;
    char *email;
    char *name;
    char *primary_check;
} ns_mail_account;

#define NS_MAIL_PRIMARY_TOKEN "nordstjernen-primary-v1"

#define NS_MAIL_LIST_MAX 25

typedef struct {
    const char *id;
    const char *domain;
    const char *label;
    const char *imap_host;
    int         imap_port;
    const char *smtp_host;
    int         smtp_port;
    const char *smtp_sec;
    gboolean    app_password;
    const char *help_url;
    const char *twofa_url;
} ns_mail_provider;

static const ns_mail_provider k_providers[] = {
    { "gmail", "gmail.com",      "Gmail",    "imap.gmail.com",      993, "smtp.gmail.com",      465, "ssl",      TRUE,
      "https://support.google.com/accounts/answer/185833",
      "https://myaccount.google.com/signinoptions/two-step-verification" },
    { "gmail", "googlemail.com", "Gmail",    "imap.gmail.com",      993, "smtp.gmail.com",      465, "ssl",      TRUE,
      "https://support.google.com/accounts/answer/185833",
      "https://myaccount.google.com/signinoptions/two-step-verification" },
    { "yahoo", "yahoo.com",      "Yahoo Mail", "imap.mail.yahoo.com", 993, "smtp.mail.yahoo.com", 465, "ssl",   TRUE,
      "https://help.yahoo.com/kb/SLN15241.html",
      "https://login.yahoo.com/account/security" },
    { "icloud", "icloud.com",    "iCloud",   "imap.mail.me.com",    993, "smtp.mail.me.com",    587, "starttls", TRUE,
      "https://support.apple.com/102654",
      "https://support.apple.com/102660" },
    { "icloud", "me.com",        "iCloud",   "imap.mail.me.com",    993, "smtp.mail.me.com",    587, "starttls", TRUE,
      "https://support.apple.com/102654",
      "https://support.apple.com/102660" },
    { "fastmail", "fastmail.com","Fastmail", "imap.fastmail.com",   993, "smtp.fastmail.com",   465, "ssl",      TRUE,
      "https://www.fastmail.help/hc/en-us/articles/1500000278342",
      "https://app.fastmail.com/settings/security/2fa" },
};

static const ns_mail_provider *
provider_for_email(const char *email)
{
    if (!email) return NULL;
    const char *at = strchr(email, '@');
    if (!at || !at[1]) return NULL;
    char *domain = g_ascii_strdown(at + 1, -1);
    const ns_mail_provider *match = NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(k_providers); i++) {
        if (g_str_equal(domain, k_providers[i].domain)) {
            match = &k_providers[i];
            break;
        }
    }
    g_free(domain);
    return match;
}

static const ns_mail_provider *
provider_by_id(const char *id)
{
    if (!id || !*id) return NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(k_providers); i++)
        if (g_str_equal(id, k_providers[i].id))
            return &k_providers[i];
    return NULL;
}

static GMutex g_acct_lock;
static ns_mail_account g_acct;
static gboolean g_acct_loaded;
static char *g_primary;
static char *g_pending_shell_key;

static GMutex g_inbox_lock;
static GPtrArray *g_inbox;
static int g_sync_state;
static char *g_sync_error;
static GThread *g_sync_thread;

static GMutex g_open_lock;
static char *g_open_uid;
static int g_open_state;
static char *g_open_error;
static ns_mail_hdr g_open_msg;
static GPtrArray *g_open_atts;
static GThread *g_open_thread;

static GMutex g_send_lock;
static int g_send_state;
static char *g_send_error;
static GThread *g_send_thread;

static const char *
state_name(int st)
{
    switch (st) {
    case ST_RUNNING: return "running";
    case ST_DONE:    return "done";
    case ST_ERROR:   return "error";
    default:         return "idle";
    }
}

static char *
ns_mail_json_escape(const char *s)
{
    GString *o = g_string_new(NULL);
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\') g_string_append_c(o, '\\');
        if ((guchar)*s < 0x20) {
            g_string_append_printf(o, "\\u%04x", *s);
            continue;
        }
        g_string_append_c(o, *s);
    }
    return g_string_free(o, FALSE);
}

static void
hdr_free(gpointer p)
{
    ns_mail_hdr *h = p;
    if (!h) return;
    g_free(h->uid);
    g_free(h->from);
    g_free(h->to);
    g_free(h->subject);
    g_free(h->date);
    g_free(h->body);
    g_free(h);
}

static void
attach_free(gpointer p)
{
    ns_mail_attach *a = p;
    if (!a) return;
    g_free(a->filename);
    g_free(a->content_type);
    g_free(a->data);
    g_free(a);
}

static void
account_free(ns_mail_account *a)
{
    if (!a) return;
    g_free(a->in_proto);
    g_free(a->in_host);
    g_free(a->in_sec);
    g_free(a->in_user);
    g_free(a->in_pass);
    g_free(a->out_host);
    g_free(a->out_sec);
    g_free(a->out_user);
    g_free(a->out_pass);
    g_free(a->email);
    g_free(a->name);
    g_free(a->primary_check);
    g_free(a);
}

static char *
mail_pass_plain(const char *raw)
{
    if (!raw || !*raw) return NULL;
    if (ns_secretbox_is_sealed(raw))
        return g_primary ? ns_secretbox_open(raw, g_primary) : NULL;
    return g_strdup(raw);
}

static ns_mail_account *
account_dup(const ns_mail_account *a)
{
    ns_mail_account *d = g_new0(ns_mail_account, 1);
    d->in_proto = g_strdup(a->in_proto ? a->in_proto : "imap");
    d->in_host  = g_strdup(a->in_host);
    d->in_port  = a->in_port;
    d->in_sec   = g_strdup(a->in_sec ? a->in_sec : "ssl");
    d->in_user  = g_strdup(a->in_user);
    d->in_pass  = g_strdup(a->in_pass);
    d->out_host = g_strdup(a->out_host);
    d->out_port = a->out_port;
    d->out_sec  = g_strdup(a->out_sec ? a->out_sec : "ssl");
    d->out_user = g_strdup(a->out_user);
    d->out_pass = g_strdup(a->out_pass);
    d->email    = g_strdup(a->email);
    d->name     = g_strdup(a->name);
    return d;
}

static char *
account_path(void)
{
    return g_build_filename(g_get_user_config_dir(), NS_APP_DIR_NAME,
                            "email.conf", NULL);
}

static void
account_load_locked(void)
{
    if (g_acct_loaded) return;
    g_acct_loaded = TRUE;
    g_acct.in_proto = g_strdup("imap");
    g_acct.in_sec   = g_strdup("ssl");
    g_acct.out_sec  = g_strdup("ssl");

    char *path = account_path();
    char *text = NULL;
    if (g_file_get_contents(path, &text, NULL, NULL)) {
        char **lines = g_strsplit(text, "\n", -1);
        for (int i = 0; lines[i]; i++) {
            char *eq = strchr(lines[i], '=');
            if (!eq) continue;
            *eq = '\0';
            char *key = g_strstrip(lines[i]);
            char *val = g_strstrip(eq + 1);
            if (g_str_equal(key, "in_proto")) { g_free(g_acct.in_proto); g_acct.in_proto = g_strdup(val); }
            else if (g_str_equal(key, "in_host")) { g_free(g_acct.in_host); g_acct.in_host = g_strdup(val); }
            else if (g_str_equal(key, "in_port")) g_acct.in_port = atoi(val);
            else if (g_str_equal(key, "in_sec")) { g_free(g_acct.in_sec); g_acct.in_sec = g_strdup(val); }
            else if (g_str_equal(key, "in_user")) { g_free(g_acct.in_user); g_acct.in_user = g_strdup(val); }
            else if (g_str_equal(key, "in_pass")) { g_free(g_acct.in_pass); g_acct.in_pass = g_strdup(val); }
            else if (g_str_equal(key, "out_host")) { g_free(g_acct.out_host); g_acct.out_host = g_strdup(val); }
            else if (g_str_equal(key, "out_port")) g_acct.out_port = atoi(val);
            else if (g_str_equal(key, "out_sec")) { g_free(g_acct.out_sec); g_acct.out_sec = g_strdup(val); }
            else if (g_str_equal(key, "out_user")) { g_free(g_acct.out_user); g_acct.out_user = g_strdup(val); }
            else if (g_str_equal(key, "out_pass")) { g_free(g_acct.out_pass); g_acct.out_pass = g_strdup(val); }
            else if (g_str_equal(key, "email")) { g_free(g_acct.email); g_acct.email = g_strdup(val); }
            else if (g_str_equal(key, "name")) { g_free(g_acct.name); g_acct.name = g_strdup(val); }
            else if (g_str_equal(key, "primary_check")) { g_free(g_acct.primary_check); g_acct.primary_check = g_strdup(val); }
        }
        g_strfreev(lines);
        g_free(text);
    }
    g_free(path);
}

static void
account_save_locked(void)
{
    char *path = account_path();
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    GString *s = g_string_new("# nordstjernen email account\n");
    g_string_append_printf(s, "in_proto = %s\n", g_acct.in_proto ? g_acct.in_proto : "imap");
    g_string_append_printf(s, "in_host = %s\n", g_acct.in_host ? g_acct.in_host : "");
    g_string_append_printf(s, "in_port = %d\n", g_acct.in_port);
    g_string_append_printf(s, "in_sec = %s\n", g_acct.in_sec ? g_acct.in_sec : "ssl");
    g_string_append_printf(s, "in_user = %s\n", g_acct.in_user ? g_acct.in_user : "");
    g_string_append_printf(s, "in_pass = %s\n", g_acct.in_pass ? g_acct.in_pass : "");
    g_string_append_printf(s, "out_host = %s\n", g_acct.out_host ? g_acct.out_host : "");
    g_string_append_printf(s, "out_port = %d\n", g_acct.out_port);
    g_string_append_printf(s, "out_sec = %s\n", g_acct.out_sec ? g_acct.out_sec : "ssl");
    g_string_append_printf(s, "out_user = %s\n", g_acct.out_user ? g_acct.out_user : "");
    g_string_append_printf(s, "out_pass = %s\n", g_acct.out_pass ? g_acct.out_pass : "");
    g_string_append_printf(s, "email = %s\n", g_acct.email ? g_acct.email : "");
    g_string_append_printf(s, "name = %s\n", g_acct.name ? g_acct.name : "");
    if (g_acct.primary_check && *g_acct.primary_check)
        g_string_append_printf(s, "primary_check = %s\n", g_acct.primary_check);

    if (g_file_set_contents(path, s->str, (gssize)s->len, NULL))
        g_chmod(path, 0600);
    g_string_free(s, TRUE);
    g_free(path);
}

static ns_mail_account *
account_snapshot(gboolean *out_locked)
{
    g_mutex_lock(&g_acct_lock);
    account_load_locked();
    if (out_locked)
        *out_locked = g_acct.primary_check && *g_acct.primary_check && !g_primary;
    ns_mail_account *snap = account_dup(&g_acct);
    char *ip = mail_pass_plain(snap->in_pass);
    g_free(snap->in_pass);
    snap->in_pass = ip;
    char *op = mail_pass_plain(snap->out_pass);
    g_free(snap->out_pass);
    snap->out_pass = op;
    g_mutex_unlock(&g_acct_lock);
    return snap;
}

static gboolean
account_complete(const ns_mail_account *a)
{
    return a && a->in_host && *a->in_host && a->in_user && *a->in_user &&
           a->email && *a->email;
}

gboolean
ns_mail_is_configured(void)
{
    ns_mail_account *a = account_snapshot(NULL);
    gboolean ok = account_complete(a);
    account_free(a);
    return ok;
}

char *
ns_mail_account_json(void)
{
    ns_mail_account *a = account_snapshot(NULL);
    GString *o = g_string_new("{");
    char *email = ns_mail_json_escape(a->email ? a->email : "");
    char *name = ns_mail_json_escape(a->name ? a->name : "");
    g_string_append_printf(o, "\"configured\":%s,",
                           account_complete(a) ? "true" : "false");
    g_string_append_printf(o, "\"email\":\"%s\",\"name\":\"%s\",", email, name);
    g_free(email);
    g_free(name);

    char *ih = ns_mail_json_escape(a->in_host ? a->in_host : "");
    char *iu = ns_mail_json_escape(a->in_user ? a->in_user : "");
    g_string_append_printf(o,
        "\"incoming\":{\"protocol\":\"%s\",\"host\":\"%s\",\"port\":%d,"
        "\"security\":\"%s\",\"user\":\"%s\",\"has_pass\":%s},",
        a->in_proto ? a->in_proto : "imap", ih, a->in_port,
        a->in_sec ? a->in_sec : "ssl", iu,
        (a->in_pass && *a->in_pass) ? "true" : "false");
    g_free(ih);
    g_free(iu);

    char *oh = ns_mail_json_escape(a->out_host ? a->out_host : "");
    char *ou = ns_mail_json_escape(a->out_user ? a->out_user : "");
    g_string_append_printf(o,
        "\"outgoing\":{\"host\":\"%s\",\"port\":%d,\"security\":\"%s\","
        "\"user\":\"%s\",\"has_pass\":%s}}",
        oh, a->out_port, a->out_sec ? a->out_sec : "ssl", ou,
        (a->out_pass && *a->out_pass) ? "true" : "false");
    g_free(oh);
    g_free(ou);

    account_free(a);
    return g_string_free(o, FALSE);
}

char *
ns_mail_autoconfig_json(const char *provider_id, const char *email)
{
    const ns_mail_provider *p = provider_by_id(provider_id);
    if (!p) p = provider_for_email(email);
    if (!p) return g_strdup("{\"matched\":false}");
    char *user = ns_mail_json_escape(email ? email : "");
    char *o = g_strdup_printf(
        "{\"matched\":true,\"id\":\"%s\",\"provider\":\"%s\",\"app_password\":%s,"
        "\"help_url\":\"%s\",\"twofa_url\":\"%s\","
        "\"incoming\":{\"protocol\":\"imap\",\"host\":\"%s\",\"port\":%d,"
        "\"security\":\"ssl\",\"user\":\"%s\"},"
        "\"outgoing\":{\"host\":\"%s\",\"port\":%d,\"security\":\"%s\","
        "\"user\":\"%s\"}}",
        p->id, p->label, p->app_password ? "true" : "false",
        p->help_url, p->twofa_url,
        p->imap_host, p->imap_port, user,
        p->smtp_host, p->smtp_port, p->smtp_sec, user);
    g_free(user);
    return o;
}

static char *
form_get(GHashTable *q, const char *key)
{
    const char *v = q ? g_hash_table_lookup(q, key) : NULL;
    return v ? g_strdup(v) : NULL;
}

static void
form_set_field(char **slot, GHashTable *q, const char *key)
{
    char *v = form_get(q, key);
    if (v) {
        g_free(*slot);
        *slot = v;
    }
}

gboolean
ns_mail_account_save(const char *form)
{
    GHashTable *q = form && *form
        ? g_uri_parse_params(form, -1, "&", G_URI_PARAMS_WWW_FORM, NULL)
        : NULL;

    g_mutex_lock(&g_acct_lock);
    account_load_locked();
    if (g_acct.primary_check && *g_acct.primary_check && !g_primary) {
        g_mutex_unlock(&g_acct_lock);
        if (q) g_hash_table_destroy(q);
        return FALSE;
    }
    form_set_field(&g_acct.in_proto, q, "in_proto");
    form_set_field(&g_acct.in_host, q, "in_host");
    form_set_field(&g_acct.in_sec, q, "in_sec");
    form_set_field(&g_acct.in_user, q, "in_user");
    form_set_field(&g_acct.out_host, q, "out_host");
    form_set_field(&g_acct.out_sec, q, "out_sec");
    form_set_field(&g_acct.out_user, q, "out_user");
    form_set_field(&g_acct.email, q, "email");
    form_set_field(&g_acct.name, q, "name");

    char *ip = form_get(q, "in_port");
    if (ip) { g_acct.in_port = atoi(ip); g_free(ip); }
    char *op = form_get(q, "out_port");
    if (op) { g_acct.out_port = atoi(op); g_free(op); }

    char *inpass = form_get(q, "in_pass");
    if (inpass && *inpass) {
        g_free(g_acct.in_pass);
        g_acct.in_pass = g_primary ? ns_secretbox_seal(inpass, g_primary)
                                   : g_strdup(inpass);
    }
    g_free(inpass);
    char *outpass = form_get(q, "out_pass");
    if (outpass && *outpass) {
        g_free(g_acct.out_pass);
        g_acct.out_pass = g_primary ? ns_secretbox_seal(outpass, g_primary)
                                    : g_strdup(outpass);
    }
    g_free(outpass);

    if (g_acct.email && *g_acct.email) {
        if (!g_acct.in_user || !*g_acct.in_user) {
            g_free(g_acct.in_user);
            g_acct.in_user = g_strdup(g_acct.email);
        }
        if (!g_acct.out_user || !*g_acct.out_user) {
            g_free(g_acct.out_user);
            g_acct.out_user = g_strdup(g_acct.email);
        }
    }
    char *vendor = form_get(q, "vendor");
    const ns_mail_provider *p = provider_by_id(vendor);
    if (!p) p = provider_for_email(g_acct.email);
    g_free(vendor);
    if (p) {
        if (!g_acct.in_proto || !*g_acct.in_proto) {
            g_free(g_acct.in_proto);
            g_acct.in_proto = g_strdup("imap");
        }
        if (!g_acct.in_host || !*g_acct.in_host) {
            g_free(g_acct.in_host);
            g_acct.in_host = g_strdup(p->imap_host);
            if (g_acct.in_port <= 0) g_acct.in_port = p->imap_port;
            g_free(g_acct.in_sec);
            g_acct.in_sec = g_strdup("ssl");
        }
        if (!g_acct.out_host || !*g_acct.out_host) {
            g_free(g_acct.out_host);
            g_acct.out_host = g_strdup(p->smtp_host);
            if (g_acct.out_port <= 0) g_acct.out_port = p->smtp_port;
            g_free(g_acct.out_sec);
            g_acct.out_sec = g_strdup(p->smtp_sec);
        }
    }

    account_save_locked();
    g_mutex_unlock(&g_acct_lock);

    if (q) g_hash_table_destroy(q);

    g_mutex_lock(&g_inbox_lock);
    if (g_inbox) { g_ptr_array_free(g_inbox, TRUE); g_inbox = NULL; }
    g_sync_state = ST_IDLE;
    g_clear_pointer(&g_sync_error, g_free);
    g_mutex_unlock(&g_inbox_lock);
    return TRUE;
}

static char *
mail_security_state(void)
{
    g_mutex_lock(&g_acct_lock);
    account_load_locked();
    const char *s = (g_acct.primary_check && *g_acct.primary_check)
        ? (g_primary ? "unlocked" : "locked")
        : "none";
    g_mutex_unlock(&g_acct_lock);
    return g_strdup(s);
}

gboolean
ns_mail_unlock(const char *form)
{
    GHashTable *q = form && *form
        ? g_uri_parse_params(form, -1, "&", G_URI_PARAMS_WWW_FORM, NULL) : NULL;
    char *pw = form_get(q, "password");
    if (q) g_hash_table_destroy(q);

    gboolean ok = FALSE;
    g_mutex_lock(&g_acct_lock);
    account_load_locked();
    if (pw && g_acct.primary_check && *g_acct.primary_check) {
        char *token = ns_secretbox_open(g_acct.primary_check, pw);
        if (token && g_str_equal(token, NS_MAIL_PRIMARY_TOKEN)) {
            g_free(g_primary);
            g_primary = g_strdup(pw);
            g_free(g_pending_shell_key);
            g_pending_shell_key = g_strdup(pw);
            ok = TRUE;
        }
        g_free(token);
    }
    g_mutex_unlock(&g_acct_lock);
    g_free(pw);
    return ok;
}

char *
ns_mail_take_pending_shell_key(void)
{
    char *b64 = NULL;
    g_mutex_lock(&g_acct_lock);
    if (g_pending_shell_key) {
        b64 = g_base64_encode((const guchar *)g_pending_shell_key,
                              strlen(g_pending_shell_key));
        OPENSSL_cleanse(g_pending_shell_key, strlen(g_pending_shell_key));
        g_clear_pointer(&g_pending_shell_key, g_free);
    }
    g_mutex_unlock(&g_acct_lock);
    return b64;
}

void
ns_mail_set_session_key(const char *b64)
{
    if (!b64 || !*b64)
        return;
    gsize n = 0;
    guchar *raw = g_base64_decode(b64, &n);
    if (!raw)
        return;
    char *pw = g_strndup((const char *)raw, n);
    OPENSSL_cleanse(raw, n);
    g_free(raw);
    g_mutex_lock(&g_acct_lock);
    if (!g_primary || !g_str_equal(g_primary, pw)) {
        g_free(g_primary);
        g_primary = g_strdup(pw);
    }
    g_mutex_unlock(&g_acct_lock);
    OPENSSL_cleanse(pw, strlen(pw));
    g_free(pw);
}

gboolean
ns_mail_set_primary(const char *form)
{
    GHashTable *q = form && *form
        ? g_uri_parse_params(form, -1, "&", G_URI_PARAMS_WWW_FORM, NULL) : NULL;
    char *pw = form_get(q, "password");
    if (q) g_hash_table_destroy(q);
    if (!pw || !*pw) { g_free(pw); return FALSE; }

    gboolean ok = FALSE;
    g_mutex_lock(&g_acct_lock);
    account_load_locked();
    gboolean had = g_acct.primary_check && *g_acct.primary_check;
    if (!had || g_primary) {
        char *check = ns_secretbox_seal(NS_MAIL_PRIMARY_TOKEN, pw);
        if (check) {
            char *ip = mail_pass_plain(g_acct.in_pass);
            char *op = mail_pass_plain(g_acct.out_pass);
            g_free(g_primary);
            g_primary = g_strdup(pw);
            g_free(g_pending_shell_key);
            g_pending_shell_key = g_strdup(pw);
            char *new_in  = (ip && *ip) ? ns_secretbox_seal(ip, g_primary) : g_strdup("");
            char *new_out = (op && *op) ? ns_secretbox_seal(op, g_primary) : g_strdup("");
            g_free(g_acct.in_pass);  g_acct.in_pass  = new_in;
            g_free(g_acct.out_pass); g_acct.out_pass = new_out;
            g_free(g_acct.primary_check); g_acct.primary_check = check;
            account_save_locked();
            if (ip) OPENSSL_cleanse(ip, strlen(ip));
            if (op) OPENSSL_cleanse(op, strlen(op));
            g_free(ip);
            g_free(op);
            ok = TRUE;
        }
    }
    g_mutex_unlock(&g_acct_lock);
    g_free(pw);
    return ok;
}

static size_t
mail_write_cb(char *data, size_t size, size_t nmemb, void *userdata)
{
    GString *buf = userdata;
    size_t bytes = size * nmemb;
    if (!buf) return bytes;
    if (buf->len + bytes > 24u * 1024u * 1024u)
        return 0;
    g_string_append_len(buf, data, bytes);
    return bytes;
}

static CURL *
mail_curl_new(const ns_mail_account *a, gboolean outgoing, char *errbuf,
              GString *buf)
{
    CURL *c = curl_easy_init();
    if (!c) return NULL;
    const char *user = outgoing ? a->out_user : a->in_user;
    const char *pass = outgoing ? a->out_pass : a->in_pass;
    const char *sec  = outgoing ? a->out_sec  : a->in_sec;
    if (user && *user) curl_easy_setopt(c, CURLOPT_USERNAME, user);
    if (pass && *pass) curl_easy_setopt(c, CURLOPT_PASSWORD, pass);
    ns_net_apply_curl_tls(c);
    if (sec && g_str_equal(sec, "starttls"))
        curl_easy_setopt(c, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    if (errbuf) { errbuf[0] = '\0'; curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf); }
#ifdef CURLOPT_PROTOCOLS_STR
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR,
                     "imap,imaps,pop3,pop3s,smtp,smtps");
#endif
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mail_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, buf);
    return c;
}

static char *
incoming_base_url(const ns_mail_account *a, gboolean *is_pop)
{
    gboolean pop = a->in_proto && g_str_equal(a->in_proto, "pop3");
    gboolean ssl = a->in_sec && g_str_equal(a->in_sec, "ssl");
    int port = a->in_port > 0 ? a->in_port
                              : (pop ? (ssl ? 995 : 110) : (ssl ? 993 : 143));
    if (is_pop) *is_pop = pop;
    return g_strdup_printf("%s://%s:%d",
                           pop ? (ssl ? "pop3s" : "pop3")
                               : (ssl ? "imaps" : "imap"),
                           a->in_host ? a->in_host : "", port);
}

static char *
outgoing_url(const ns_mail_account *a)
{
    gboolean ssl = a->out_sec && g_str_equal(a->out_sec, "ssl");
    int port = a->out_port > 0 ? a->out_port : (ssl ? 465 : 587);
    return g_strdup_printf("%s://%s:%d", ssl ? "smtps" : "smtp",
                           a->out_host ? a->out_host : "", port);
}

static char *
qp_decode(const char *s, gsize len, gsize *out_len)
{
    GByteArray *o = g_byte_array_new();
    for (gsize i = 0; i < len; i++) {
        if (s[i] == '=' && i + 2 < len &&
            g_ascii_isxdigit(s[i + 1]) && g_ascii_isxdigit(s[i + 2])) {
            char h[3] = { s[i + 1], s[i + 2], 0 };
            guint8 b = (guint8)strtol(h, NULL, 16);
            g_byte_array_append(o, &b, 1);
            i += 2;
        } else if (s[i] == '=' && i + 2 < len &&
                   s[i + 1] == '\r' && s[i + 2] == '\n') {
            i += 2;
        } else if (s[i] == '=' && i + 1 < len && s[i + 1] == '\n') {
            i += 1;
        } else {
            g_byte_array_append(o, (const guint8 *)&s[i], 1);
        }
    }
    *out_len = o->len;
    guint8 nul = 0;
    g_byte_array_append(o, &nul, 1);
    return (char *)g_byte_array_free(o, FALSE);
}

static char *
to_utf8(const char *data, gssize len, const char *charset)
{
    if (!data) return g_strdup("");
    if (len < 0) len = (gssize)strlen(data);
    gboolean unicode = !charset || !*charset ||
        g_ascii_strcasecmp(charset, "utf-8") == 0 ||
        g_ascii_strcasecmp(charset, "utf8") == 0 ||
        g_ascii_strcasecmp(charset, "us-ascii") == 0 ||
        g_ascii_strcasecmp(charset, "ascii") == 0;
    if (!unicode) {
        char *out = g_convert(data, len, "UTF-8", charset, NULL, NULL, NULL);
        if (out) return out;
    }
    if (g_utf8_validate(data, len, NULL))
        return g_strndup(data, len);
    return g_utf8_make_valid(data, len);
}

static char *
rfc2047_decode(const char *s)
{
    if (!s) return g_strdup("");
    GString *o = g_string_new(NULL);
    const char *p = s;
    while (*p) {
        const char *start = strstr(p, "=?");
        if (!start) { g_string_append(o, p); break; }
        g_string_append_len(o, p, start - p);
        const char *c1 = strchr(start + 2, '?');
        if (!c1) { g_string_append(o, start); break; }
        const char *c2 = strchr(c1 + 1, '?');
        if (!c2 || c2 - c1 != 2) { g_string_append_len(o, start, 2); p = start + 2; continue; }
        const char *end = strstr(c2 + 1, "?=");
        if (!end) { g_string_append_len(o, start, 2); p = start + 2; continue; }
        char *charset = g_strndup(start + 2, c1 - (start + 2));
        char enc = g_ascii_toupper(c1[1]);
        char *enctext = g_strndup(c2 + 1, end - (c2 + 1));
        char *raw = NULL;
        gsize raw_len = 0;
        if (enc == 'B') {
            raw = (char *)g_base64_decode(enctext, &raw_len);
        } else if (enc == 'Q') {
            for (char *q = enctext; *q; q++) if (*q == '_') *q = ' ';
            raw = qp_decode(enctext, strlen(enctext), &raw_len);
        }
        if (raw) {
            char *u = to_utf8(raw, (gssize)raw_len, charset);
            g_string_append(o, u);
            g_free(u);
            g_free(raw);
        } else {
            g_string_append_len(o, start, (end + 2) - start);
        }
        g_free(charset);
        g_free(enctext);
        p = end + 2;
        while (*p == ' ' || *p == '\t') {
            const char *n = p;
            while (*n == ' ' || *n == '\t') n++;
            if (g_str_has_prefix(n, "=?")) { p = n; break; }
            break;
        }
    }
    return g_string_free(o, FALSE);
}

static char *
unfold_headers(const char *raw, gsize len)
{
    const char *split = NULL;
    for (gsize i = 0; i + 1 < len; i++) {
        if (raw[i] == '\n' && raw[i + 1] == '\n') { split = raw + i; break; }
        if (i + 3 < len && raw[i] == '\r' && raw[i + 1] == '\n' &&
            raw[i + 2] == '\r' && raw[i + 3] == '\n') { split = raw + i; break; }
    }
    gsize hlen = split ? (gsize)(split - raw) : len;
    return g_strndup(raw, hlen);
}

static char *
header_value(const char *headers, const char *name)
{
    gsize nlen = strlen(name);
    char **lines = g_strsplit(headers, "\n", -1);
    GString *acc = NULL;
    char *result = NULL;
    for (int i = 0; lines[i]; i++) {
        char *line = lines[i];
        gsize ll = strlen(line);
        if (ll && line[ll - 1] == '\r') line[ll - 1] = '\0';
        if (acc) {
            if (line[0] == ' ' || line[0] == '\t') {
                g_string_append_c(acc, ' ');
                g_string_append(acc, g_strchug(line));
                continue;
            }
            break;
        }
        if (g_ascii_strncasecmp(line, name, nlen) == 0 && line[nlen] == ':') {
            acc = g_string_new(g_strchug(line + nlen + 1));
        }
    }
    g_strfreev(lines);
    if (acc) result = rfc2047_decode(acc->str);
    if (acc) g_string_free(acc, TRUE);
    return result ? result : g_strdup("");
}

static char *
ctype_param(const char *value, const char *param)
{
    if (!value) return NULL;
    char *low = g_ascii_strdown(value, -1);
    char *plow = g_ascii_strdown(param, -1);
    char *needle = g_strdup_printf("%s=", plow);
    char *at = strstr(low, needle);
    char *out = NULL;
    if (at) {
        const char *src = value + (at - low) + strlen(needle);
        while (*src == ' ') src++;
        if (*src == '"') {
            src++;
            const char *e = strchr(src, '"');
            if (e) out = g_strndup(src, e - src);
        } else {
            const char *e = src;
            while (*e && *e != ';' && *e != ' ' && *e != '\r' && *e != '\n') e++;
            out = g_strndup(src, e - src);
        }
    }
    g_free(low);
    g_free(plow);
    g_free(needle);
    return out;
}

static const char *
ci_find(const char *haystack, const char *needle)
{
    gsize nl = strlen(needle);
    for (const char *h = haystack; *h; h++)
        if (g_ascii_strncasecmp(h, needle, nl) == 0)
            return h;
    return NULL;
}

static char *
html_to_text(const char *html)
{
    if (!html) return g_strdup("");
    GString *o = g_string_new(NULL);
    const char *p = html;
    while (*p) {
        if (*p == '<') {
            if (g_ascii_strncasecmp(p, "<script", 7) == 0 ||
                g_ascii_strncasecmp(p, "<style", 6) == 0) {
                const char *close = g_ascii_strncasecmp(p, "<script", 7) == 0
                    ? "</script" : "</style";
                const char *e = ci_find(p, close);
                p = e ? e : p + strlen(p);
                while (*p && *p != '>') p++;
                if (*p) p++;
                continue;
            }
            if (g_ascii_strncasecmp(p, "<br", 3) == 0 ||
                g_ascii_strncasecmp(p, "</p", 3) == 0 ||
                g_ascii_strncasecmp(p, "</div", 5) == 0 ||
                g_ascii_strncasecmp(p, "</tr", 4) == 0 ||
                g_ascii_strncasecmp(p, "</li", 4) == 0 ||
                g_ascii_strncasecmp(p, "</h", 3) == 0)
                g_string_append_c(o, '\n');
            while (*p && *p != '>') p++;
            if (*p) p++;
            continue;
        }
        if (*p == '&') {
            if (g_ascii_strncasecmp(p, "&amp;", 5) == 0) { g_string_append_c(o, '&'); p += 5; continue; }
            if (g_ascii_strncasecmp(p, "&lt;", 4) == 0) { g_string_append_c(o, '<'); p += 4; continue; }
            if (g_ascii_strncasecmp(p, "&gt;", 4) == 0) { g_string_append_c(o, '>'); p += 4; continue; }
            if (g_ascii_strncasecmp(p, "&quot;", 6) == 0) { g_string_append_c(o, '"'); p += 6; continue; }
            if (g_ascii_strncasecmp(p, "&#39;", 5) == 0) { g_string_append_c(o, '\''); p += 5; continue; }
            if (g_ascii_strncasecmp(p, "&nbsp;", 6) == 0) { g_string_append_c(o, ' '); p += 6; continue; }
        }
        g_string_append_c(o, *p);
        p++;
    }
    char **lines = g_strsplit(o->str, "\n", -1);
    GString *clean = g_string_new(NULL);
    int blanks = 0;
    for (int i = 0; lines[i]; i++) {
        char *t = g_strchomp(lines[i]);
        if (!*t) { if (++blanks <= 1) g_string_append_c(clean, '\n'); continue; }
        blanks = 0;
        g_string_append(clean, t);
        g_string_append_c(clean, '\n');
    }
    g_strfreev(lines);
    g_string_free(o, TRUE);
    return g_string_free(clean, FALSE);
}

typedef struct {
    char      *best_plain;
    char      *best_html;
    GPtrArray *atts;
} mime_acc;

static void mime_collect(const char *data, gsize len, int depth, mime_acc *acc);

static guint8 *
cte_decode_raw(const char *body, gsize body_len, const char *cte, gsize *out_len)
{
    char *c = cte ? g_strstrip(g_strdup(cte)) : NULL;
    guint8 *out;
    gsize n;
    if (c && g_ascii_strcasecmp(c, "base64") == 0) {
        char *b = g_strndup(body, body_len);
        out = g_base64_decode(b, &n);
        g_free(b);
    } else if (c && g_ascii_strcasecmp(c, "quoted-printable") == 0) {
        out = (guint8 *)qp_decode(body, body_len, &n);
    } else {
        out = g_malloc(body_len ? body_len : 1);
        memcpy(out, body, body_len);
        n = body_len;
    }
    g_free(c);
    *out_len = n;
    return out;
}

static char *
mime_extract_part_body(const char *headers, const char *body, gsize body_len,
                       gboolean *is_html)
{
    char *ctype = header_value(headers, "Content-Type");
    char *cte = header_value(headers, "Content-Transfer-Encoding");
    char *charset = ctype_param(ctype, "charset");
    gboolean html = ctype && g_ascii_strncasecmp(ctype, "text/html", 9) == 0;

    gsize decoded_len = 0;
    char *decoded = (char *)cte_decode_raw(body, body_len, cte, &decoded_len);
    char *text = to_utf8(decoded, (gssize)decoded_len, charset);
    g_free(decoded);
    if (html) {
        char *stripped = html_to_text(text);
        g_free(text);
        text = stripped;
    }
    if (is_html) *is_html = html;
    g_free(ctype);
    g_free(cte);
    g_free(charset);
    return text;
}

static char *
mime_type_only(const char *ctype)
{
    if (!ctype) return g_strdup("application/octet-stream");
    char *t = g_strstrip(g_strdup(ctype));
    char *semi = strchr(t, ';');
    if (semi) *semi = '\0';
    g_strchomp(t);
    if (!*t) { g_free(t); return g_strdup("application/octet-stream"); }
    return t;
}

static void
mime_collect_leaf(const char *headers, const char *body, gsize body_len,
                  mime_acc *acc)
{
    char *ctype = header_value(headers, "Content-Type");
    char *cdisp = header_value(headers, "Content-Disposition");
    char *cte = header_value(headers, "Content-Transfer-Encoding");
    char *filename = ctype_param(cdisp, "filename");
    if (!filename) filename = ctype_param(ctype, "name");

    gboolean is_text = ctype == NULL ||
        g_ascii_strncasecmp(ctype, "text/", 5) == 0;
    gboolean attached = (cdisp &&
                         g_ascii_strncasecmp(cdisp, "attachment", 10) == 0) ||
                        filename != NULL || !is_text;

    if (attached && acc->atts) {
        gsize dlen = 0;
        guint8 *raw = cte_decode_raw(body, body_len, cte, &dlen);
        if (raw && dlen > 0 && dlen <= NS_MAIL_ATT_MAX) {
            ns_mail_attach *a = g_new0(ns_mail_attach, 1);
            a->filename = filename && *g_strstrip(filename)
                ? g_strdup(filename) : g_strdup("attachment");
            a->content_type = mime_type_only(ctype);
            a->data = raw;
            a->len = dlen;
            g_ptr_array_add(acc->atts, a);
            raw = NULL;
        }
        g_free(raw);
    } else if (ctype && g_ascii_strncasecmp(ctype, "text/html", 9) == 0) {
        if (!acc->best_html)
            acc->best_html = mime_extract_part_body(headers, body, body_len, NULL);
    } else {
        if (!acc->best_plain)
            acc->best_plain = mime_extract_part_body(headers, body, body_len, NULL);
    }
    g_free(ctype);
    g_free(cdisp);
    g_free(cte);
    g_free(filename);
}

static void
mime_collect(const char *data, gsize len, int depth, mime_acc *acc)
{
    char *headers = unfold_headers(data, len);
    const char *body = data;
    for (gsize i = 0; i + 1 < len; i++) {
        if (data[i] == '\n' && data[i + 1] == '\n') { body = data + i + 2; break; }
        if (i + 3 < len && data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n') { body = data + i + 4; break; }
    }
    gsize body_len = (data + len) - body;
    if (body < data || body > data + len) { body = data; body_len = len; }

    char *ctype = header_value(headers, "Content-Type");
    gboolean multipart = ctype &&
        g_ascii_strncasecmp(ctype, "multipart/", 10) == 0;

    if (multipart && depth < 8) {
        char *boundary = ctype_param(ctype, "boundary");
        if (boundary && *boundary) {
            char *delim = g_strdup_printf("--%s", boundary);
            gsize dlen = strlen(delim);
            const char *scan = body;
            const char *bend = body + body_len;
            while (scan < bend) {
                const char *hit = g_strstr_len(scan, bend - scan, delim);
                if (!hit) break;
                const char *part_start = hit + dlen;
                if (part_start + 2 <= bend && part_start[0] == '-' && part_start[1] == '-')
                    break;
                while (part_start < bend && (*part_start == '\r' || *part_start == '\n'))
                    part_start++;
                const char *next = g_strstr_len(part_start, bend - part_start, delim);
                const char *part_end = next ? next : bend;
                mime_collect(part_start, part_end - part_start, depth + 1, acc);
                if (!next) break;
                scan = next;
            }
            g_free(delim);
            g_free(boundary);
            g_free(ctype);
            g_free(headers);
            return;
        }
        g_free(boundary);
    }

    g_free(ctype);
    mime_collect_leaf(headers, body, body_len, acc);
    g_free(headers);
}

static GArray *
imap_search_uids(CURL *c, const char *base, char *errbuf, GError **error)
{
    GString *buf = g_string_new(NULL);
    char *url = g_strdup_printf("%s/INBOX", base);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "UID SEARCH ALL");
    curl_easy_setopt(c, CURLOPT_WRITEDATA, buf);
    CURLcode rc = curl_easy_perform(c);
    g_free(url);
    if (rc != CURLE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
                    errbuf[0] ? errbuf : curl_easy_strerror(rc));
        g_string_free(buf, TRUE);
        return NULL;
    }
    GArray *uids = g_array_new(FALSE, FALSE, sizeof(guint));
    char **tok = g_strsplit_set(buf->str, " \r\n", -1);
    gboolean after = FALSE;
    for (int i = 0; tok[i]; i++) {
        if (!*tok[i]) continue;
        if (g_ascii_strcasecmp(tok[i], "SEARCH") == 0) { after = TRUE; continue; }
        if (after) {
            char *endp = NULL;
            guint64 v = g_ascii_strtoull(tok[i], &endp, 10);
            if (endp && *endp == '\0' && v > 0) {
                guint u = (guint)v;
                g_array_append_val(uids, u);
            }
        }
    }
    g_strfreev(tok);
    g_string_free(buf, TRUE);
    return uids;
}

static char *
imap_literal_body(const char *resp, gsize len, gsize *out_len)
{
    *out_len = 0;
    const char *brace = NULL;
    for (gsize i = 0; i < len; i++)
        if (resp[i] == '{') { brace = resp + i; break; }
    if (!brace) return NULL;
    const char *p = brace + 1;
    const char *end = resp + len;
    gsize n = 0;
    gboolean any = FALSE;
    while (p < end && *p >= '0' && *p <= '9') {
        n = n * 10 + (gsize)(*p - '0');
        p++;
        any = TRUE;
    }
    if (!any || p >= end || *p != '}') return NULL;
    p++;
    if (p + 1 < end && p[0] == '\r' && p[1] == '\n') p += 2;
    else if (p < end && p[0] == '\n') p += 1;
    else return NULL;
    if ((gsize)(end - p) < n) n = (gsize)(end - p);
    *out_len = n;
    return g_strndup(p, n);
}

static ns_mail_hdr *
imap_fetch_header(CURL *c, const char *base, guint uid, char *errbuf)
{
    GString *buf = g_string_new(NULL);
    char *url = g_strdup_printf("%s/INBOX", base);
    char *cmd = g_strdup_printf("UID FETCH %u BODY.PEEK[HEADER]", uid);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, cmd);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, buf);
    CURLcode rc = curl_easy_perform(c);
    g_free(url);
    g_free(cmd);
    ns_mail_hdr *h = NULL;
    if (rc == CURLE_OK) {
        gsize blen = 0;
        char *block = imap_literal_body(buf->str, buf->len, &blen);
        char *headers = block ? unfold_headers(block, blen)
                              : unfold_headers(buf->str, buf->len);
        h = g_new0(ns_mail_hdr, 1);
        h->uid = g_strdup_printf("%u", uid);
        h->from = header_value(headers, "From");
        h->subject = header_value(headers, "Subject");
        h->date = header_value(headers, "Date");
        g_free(headers);
        g_free(block);
    } else if (errbuf[0]) {
        g_debug("imap header fetch: %s", errbuf);
    }
    g_string_free(buf, TRUE);
    return h;
}

static GPtrArray *
imap_list(const ns_mail_account *a, GError **error)
{
    char errbuf[CURL_ERROR_SIZE] = {0};
    char *base = incoming_base_url(a, NULL);
    CURL *c = mail_curl_new(a, FALSE, errbuf, NULL);
    if (!c) { g_free(base); g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "curl init failed"); return NULL; }

    GArray *uids = imap_search_uids(c, base, errbuf, error);
    if (!uids) { curl_easy_cleanup(c); g_free(base); return NULL; }

    GPtrArray *out = g_ptr_array_new_with_free_func(hdr_free);
    guint total = uids->len;
    guint start = total > NS_MAIL_LIST_MAX ? total - NS_MAIL_LIST_MAX : 0;
    for (guint i = total; i > start; i--) {
        guint uid = g_array_index(uids, guint, i - 1);
        ns_mail_hdr *h = imap_fetch_header(c, base, uid, errbuf);
        if (h) g_ptr_array_add(out, h);
    }
    g_array_free(uids, TRUE);
    curl_easy_cleanup(c);
    g_free(base);
    return out;
}

static GPtrArray *
pop3_list(const ns_mail_account *a, GError **error)
{
    char errbuf[CURL_ERROR_SIZE] = {0};
    char *base = incoming_base_url(a, NULL);
    GString *buf = g_string_new(NULL);
    CURL *c = mail_curl_new(a, FALSE, errbuf, buf);
    if (!c) { g_free(base); g_string_free(buf, TRUE); g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "curl init failed"); return NULL; }

    curl_easy_setopt(c, CURLOPT_URL, base);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "LIST");
    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
                    errbuf[0] ? errbuf : curl_easy_strerror(rc));
        curl_easy_cleanup(c);
        g_string_free(buf, TRUE);
        g_free(base);
        return NULL;
    }
    GArray *nums = g_array_new(FALSE, FALSE, sizeof(guint));
    char **lines = g_strsplit(buf->str, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *t = g_strstrip(lines[i]);
        if (!*t || *t == '.') continue;
        char *endp = NULL;
        guint64 v = g_ascii_strtoull(t, &endp, 10);
        if (v > 0 && endp && (*endp == ' ' || *endp == '\0')) {
            guint u = (guint)v;
            g_array_append_val(nums, u);
        }
    }
    g_strfreev(lines);
    g_string_free(buf, TRUE);

    GPtrArray *out = g_ptr_array_new_with_free_func(hdr_free);
    guint total = nums->len;
    guint start = total > NS_MAIL_LIST_MAX ? total - NS_MAIL_LIST_MAX : 0;
    for (guint i = total; i > start; i--) {
        guint num = g_array_index(nums, guint, i - 1);
        GString *hb = g_string_new(NULL);
        char *top = g_strdup_printf("TOP %u 0", num);
        curl_easy_setopt(c, CURLOPT_URL, base);
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, top);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, hb);
        CURLcode hrc = curl_easy_perform(c);
        g_free(top);
        if (hrc == CURLE_OK) {
            char *headers = unfold_headers(hb->str, hb->len);
            ns_mail_hdr *h = g_new0(ns_mail_hdr, 1);
            h->uid = g_strdup_printf("%u", num);
            h->from = header_value(headers, "From");
            h->subject = header_value(headers, "Subject");
            h->date = header_value(headers, "Date");
            g_free(headers);
            g_ptr_array_add(out, h);
        }
        g_string_free(hb, TRUE);
    }
    g_array_free(nums, TRUE);
    curl_easy_cleanup(c);
    g_free(base);
    return out;
}

static gpointer
sync_thread(gpointer data)
{
    ns_mail_account *a = data;
    GError *error = NULL;
    gboolean pop = a->in_proto && g_str_equal(a->in_proto, "pop3");
    GPtrArray *list = pop ? pop3_list(a, &error) : imap_list(a, &error);

    g_mutex_lock(&g_inbox_lock);
    if (list) {
        if (g_inbox) g_ptr_array_free(g_inbox, TRUE);
        g_inbox = list;
        g_sync_state = ST_DONE;
        g_clear_pointer(&g_sync_error, g_free);
    } else {
        g_sync_state = ST_ERROR;
        g_free(g_sync_error);
        g_sync_error = g_strdup(error ? error->message : "Sync failed");
    }
    g_mutex_unlock(&g_inbox_lock);
    g_clear_error(&error);
    account_free(a);
    return NULL;
}

void
ns_mail_refresh(void)
{
    gboolean locked = FALSE;
    ns_mail_account *snap = account_snapshot(&locked);
    if (!account_complete(snap)) { account_free(snap); return; }
    if (locked) {
        account_free(snap);
        g_mutex_lock(&g_inbox_lock);
        g_sync_state = ST_ERROR;
        g_free(g_sync_error);
        g_sync_error = g_strdup("Enter your primary password to unlock mail.");
        g_mutex_unlock(&g_inbox_lock);
        return;
    }

    g_mutex_lock(&g_inbox_lock);
    if (g_sync_state == ST_RUNNING) { g_mutex_unlock(&g_inbox_lock); account_free(snap); return; }
    GThread *old = g_sync_thread;
    g_sync_thread = NULL;
    g_sync_state = ST_RUNNING;
    g_clear_pointer(&g_sync_error, g_free);
    g_mutex_unlock(&g_inbox_lock);

    if (old) g_thread_join(old);

    g_mutex_lock(&g_inbox_lock);
    g_sync_thread = g_thread_new("ns-mail-sync", sync_thread, snap);
    g_mutex_unlock(&g_inbox_lock);
}

int
ns_mail_poll_unseen(void)
{
    gboolean locked = FALSE;
    ns_mail_account *a = account_snapshot(&locked);
    if (locked || !account_complete(a) ||
        (a->in_proto && g_str_equal(a->in_proto, "pop3"))) {
        account_free(a);
        return -1;
    }
    char errbuf[CURL_ERROR_SIZE] = {0};
    char *base = incoming_base_url(a, NULL);
    GString *buf = g_string_new(NULL);
    CURL *c = mail_curl_new(a, FALSE, errbuf, buf);
    int count = -1;
    if (c) {
        char *url = g_strdup_printf("%s/INBOX", base);
        curl_easy_setopt(c, CURLOPT_URL, url);
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "UID SEARCH UNSEEN");
        CURLcode rc = curl_easy_perform(c);
        g_free(url);
        if (rc == CURLE_OK) {
            int n = 0;
            char **tok = g_strsplit_set(buf->str, " \r\n", -1);
            gboolean after = FALSE;
            for (int i = 0; tok[i]; i++) {
                if (!*tok[i]) continue;
                if (g_ascii_strcasecmp(tok[i], "SEARCH") == 0) { after = TRUE; continue; }
                if (after) {
                    char *endp = NULL;
                    guint64 v = g_ascii_strtoull(tok[i], &endp, 10);
                    if (endp && *endp == '\0' && v > 0) n++;
                }
            }
            g_strfreev(tok);
            count = n;
        }
        curl_easy_cleanup(c);
    }
    g_string_free(buf, TRUE);
    g_free(base);
    account_free(a);
    return count;
}

char *
ns_mail_status_json(void)
{
    ns_mail_account *a = account_snapshot(NULL);
    gboolean configured = account_complete(a);
    char *sec = mail_security_state();

    g_mutex_lock(&g_inbox_lock);
    int st = g_sync_state;
    char *err = ns_mail_json_escape(g_sync_error ? g_sync_error : "");
    GString *msgs = g_string_new("[");
    if (g_inbox) {
        for (guint i = 0; i < g_inbox->len; i++) {
            ns_mail_hdr *h = g_ptr_array_index(g_inbox, i);
            char *from = ns_mail_json_escape(h->from ? h->from : "");
            char *subj = ns_mail_json_escape(h->subject ? h->subject : "");
            char *date = ns_mail_json_escape(h->date ? h->date : "");
            char *uid = ns_mail_json_escape(h->uid ? h->uid : "");
            g_string_append_printf(msgs,
                "%s{\"uid\":\"%s\",\"from\":\"%s\",\"subject\":\"%s\",\"date\":\"%s\"}",
                i ? "," : "", uid, from, subj, date);
            g_free(from);
            g_free(subj);
            g_free(date);
            g_free(uid);
        }
    }
    g_string_append_c(msgs, ']');
    g_mutex_unlock(&g_inbox_lock);

    char *email = ns_mail_json_escape(a->email ? a->email : "");
    char *name = ns_mail_json_escape(a->name ? a->name : "");
    GString *o = g_string_new("{");
    g_string_append_printf(o, "\"configured\":%s,", configured ? "true" : "false");
    g_string_append_printf(o, "\"email\":\"%s\",\"name\":\"%s\",", email, name);
    g_string_append_printf(o, "\"protocol\":\"%s\",",
                           a->in_proto ? a->in_proto : "imap");
    g_string_append_printf(o, "\"security\":\"%s\",", sec);
    g_string_append_printf(o, "\"sync\":{\"state\":\"%s\",\"error\":\"%s\"},",
                           state_name(st), err);
    g_string_append_printf(o, "\"messages\":%s}", msgs->str);
    g_free(email);
    g_free(name);
    g_free(sec);
    g_free(err);
    g_string_free(msgs, TRUE);
    account_free(a);
    return g_string_free(o, FALSE);
}

typedef struct { ns_mail_account *a; char *uid; } open_job;

static gpointer
open_thread(gpointer data)
{
    open_job *j = data;
    ns_mail_account *a = j->a;
    char errbuf[CURL_ERROR_SIZE] = {0};
    char *base = incoming_base_url(a, NULL);
    gboolean pop = a->in_proto && g_str_equal(a->in_proto, "pop3");
    GString *buf = g_string_new(NULL);
    CURL *c = mail_curl_new(a, FALSE, errbuf, buf);
    char *body = NULL, *from = NULL, *to = NULL, *subject = NULL, *date = NULL;
    GPtrArray *atts = NULL;
    gboolean ok = FALSE;

    if (c) {
        char *url = pop ? g_strdup_printf("%s/%s", base, j->uid)
                        : g_strdup_printf("%s/INBOX;UID=%s", base, j->uid);
        curl_easy_setopt(c, CURLOPT_URL, url);
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, NULL);
        CURLcode rc = curl_easy_perform(c);
        g_free(url);
        if (rc == CURLE_OK) {
            char *headers = unfold_headers(buf->str, buf->len);
            from = header_value(headers, "From");
            to = header_value(headers, "To");
            subject = header_value(headers, "Subject");
            date = header_value(headers, "Date");
            g_free(headers);
            mime_acc acc = { NULL, NULL, g_ptr_array_new_with_free_func(attach_free) };
            mime_collect(buf->str, buf->len, 0, &acc);
            body = acc.best_plain ? acc.best_plain : acc.best_html;
            if (acc.best_plain && acc.best_html) g_free(acc.best_html);
            if (!body) body = g_strdup("");
            atts = acc.atts;
            ok = TRUE;
        }
        curl_easy_cleanup(c);
    }
    g_string_free(buf, TRUE);
    g_free(base);

    g_mutex_lock(&g_open_lock);
    g_free(g_open_msg.uid); g_open_msg.uid = g_strdup(j->uid);
    g_free(g_open_msg.from); g_open_msg.from = from;
    g_free(g_open_msg.to); g_open_msg.to = to;
    g_free(g_open_msg.subject); g_open_msg.subject = subject;
    g_free(g_open_msg.date); g_open_msg.date = date;
    g_free(g_open_msg.body); g_open_msg.body = body;
    if (ok) {
        if (g_open_atts) g_ptr_array_free(g_open_atts, TRUE);
        g_open_atts = atts;
    } else if (atts) {
        g_ptr_array_free(atts, TRUE);
    }
    if (ok) {
        g_open_state = ST_DONE;
        g_clear_pointer(&g_open_error, g_free);
    } else {
        g_open_state = ST_ERROR;
        g_free(g_open_error);
        g_open_error = g_strdup(errbuf[0] ? errbuf : "Could not open message");
    }
    g_mutex_unlock(&g_open_lock);

    account_free(a);
    g_free(j->uid);
    g_free(j);
    return NULL;
}

void
ns_mail_open(const char *uid)
{
    if (!uid || !*uid) return;
    gboolean locked = FALSE;
    ns_mail_account *snap = account_snapshot(&locked);
    if (!account_complete(snap)) { account_free(snap); return; }
    if (locked) {
        account_free(snap);
        g_mutex_lock(&g_open_lock);
        g_open_state = ST_ERROR;
        g_free(g_open_error);
        g_open_error = g_strdup("Enter your primary password to unlock mail.");
        g_mutex_unlock(&g_open_lock);
        return;
    }

    g_mutex_lock(&g_open_lock);
    if (g_open_state == ST_RUNNING) { g_mutex_unlock(&g_open_lock); account_free(snap); return; }
    GThread *old = g_open_thread;
    g_open_thread = NULL;
    g_open_state = ST_RUNNING;
    g_free(g_open_uid);
    g_open_uid = g_strdup(uid);
    g_clear_pointer(&g_open_error, g_free);
    g_mutex_unlock(&g_open_lock);

    if (old) g_thread_join(old);

    open_job *j = g_new0(open_job, 1);
    j->a = snap;
    j->uid = g_strdup(uid);
    g_mutex_lock(&g_open_lock);
    g_open_thread = g_thread_new("ns-mail-open", open_thread, j);
    g_mutex_unlock(&g_open_lock);
}

char *
ns_mail_message_json(void)
{
    g_mutex_lock(&g_open_lock);
    int st = g_open_state;
    char *err = ns_mail_json_escape(g_open_error ? g_open_error : "");
    char *uid = ns_mail_json_escape(g_open_msg.uid ? g_open_msg.uid : "");
    char *from = ns_mail_json_escape(g_open_msg.from ? g_open_msg.from : "");
    char *to = ns_mail_json_escape(g_open_msg.to ? g_open_msg.to : "");
    char *subj = ns_mail_json_escape(g_open_msg.subject ? g_open_msg.subject : "");
    char *date = ns_mail_json_escape(g_open_msg.date ? g_open_msg.date : "");
    char *body = ns_mail_json_escape(g_open_msg.body ? g_open_msg.body : "");
    GString *atts = g_string_new("[");
    if (g_open_atts) {
        for (guint i = 0; i < g_open_atts->len; i++) {
            ns_mail_attach *at = g_ptr_array_index(g_open_atts, i);
            char *fn = ns_mail_json_escape(at->filename ? at->filename : "");
            char *ct = ns_mail_json_escape(at->content_type ? at->content_type : "");
            g_string_append_printf(atts,
                "%s{\"index\":%u,\"filename\":\"%s\",\"type\":\"%s\",\"size\":%" G_GSIZE_FORMAT "}",
                i ? "," : "", i, fn, ct, at->len);
            g_free(fn);
            g_free(ct);
        }
    }
    g_string_append_c(atts, ']');
    g_mutex_unlock(&g_open_lock);

    GString *o = g_string_new("{");
    g_string_append_printf(o, "\"state\":\"%s\",\"error\":\"%s\",", state_name(st), err);
    g_string_append_printf(o,
        "\"message\":{\"uid\":\"%s\",\"from\":\"%s\",\"to\":\"%s\","
        "\"subject\":\"%s\",\"date\":\"%s\",\"body\":\"%s\",\"attachments\":%s}}",
        uid, from, to, subj, date, body, atts->str);
    g_free(err); g_free(uid); g_free(from); g_free(to);
    g_free(subj); g_free(date); g_free(body);
    g_string_free(atts, TRUE);
    return g_string_free(o, FALSE);
}

gboolean
ns_mail_attachment(const char *uid, int index, char **out_ctype, char **out_name,
                   guint8 **out_data, gsize *out_len)
{
    gboolean ok = FALSE;
    g_mutex_lock(&g_open_lock);
    gboolean uid_ok = uid && *uid && g_open_msg.uid &&
                      g_str_equal(uid, g_open_msg.uid);
    if (uid_ok && g_open_atts && index >= 0 &&
        (guint)index < g_open_atts->len) {
        ns_mail_attach *a = g_ptr_array_index(g_open_atts, index);
        *out_ctype = g_strdup(a->content_type ? a->content_type
                                              : "application/octet-stream");
        *out_name = g_strdup(a->filename ? a->filename : "attachment");
        *out_data = g_memdup2(a->data, a->len);
        *out_len = a->len;
        ok = TRUE;
    }
    g_mutex_unlock(&g_open_lock);
    return ok;
}

typedef struct { const char *data; gsize len; gsize off; } upload_ctx;

static size_t
mail_read_cb(char *buffer, size_t size, size_t nitems, void *p)
{
    upload_ctx *u = p;
    size_t room = size * nitems;
    size_t n = u->len - u->off;
    if (n > room) n = room;
    if (n) { memcpy(buffer, u->data + u->off, n); u->off += n; }
    return n;
}

static char *
rfc2047_encode_if_needed(const char *s)
{
    if (!s) return g_strdup("");
    if (g_str_is_ascii(s)) return g_strdup(s);
    char *b64 = g_base64_encode((const guchar *)s, strlen(s));
    char *out = g_strdup_printf("=?UTF-8?B?%s?=", b64);
    g_free(b64);
    return out;
}

static char *
addr_angle(const char *raw)
{
    char *t = g_strstrip(g_strdup(raw));
    char *out;
    if (strchr(t, '<')) out = g_strdup(t);
    else out = g_strdup_printf("<%s>", t);
    g_free(t);
    return out;
}

static char *
build_message(const ns_mail_account *a, const char *to, const char *cc,
              const char *subject, const char *body)
{
    GDateTime *now = g_date_time_new_now_local();
    char *date = g_date_time_format(now, "%a, %d %b %Y %H:%M:%S %z");
    g_date_time_unref(now);

    char *enc_subject = rfc2047_encode_if_needed(subject ? subject : "");
    char *from_disp;
    if (a->name && *a->name) {
        char *enc_name = rfc2047_encode_if_needed(a->name);
        from_disp = g_strdup_printf("\"%s\" <%s>", enc_name, a->email);
        g_free(enc_name);
    } else {
        from_disp = g_strdup_printf("<%s>", a->email);
    }
    char *mid = g_uuid_string_random();
    const char *host = a->out_host ? a->out_host : "localhost";

    GString *m = g_string_new(NULL);
    g_string_append_printf(m, "Date: %s\r\n", date);
    g_string_append_printf(m, "From: %s\r\n", from_disp);
    g_string_append_printf(m, "To: %s\r\n", to ? to : "");
    if (cc && *cc) g_string_append_printf(m, "Cc: %s\r\n", cc);
    g_string_append_printf(m, "Subject: %s\r\n", enc_subject);
    g_string_append_printf(m, "Message-ID: <%s@%s>\r\n", mid, host);
    g_string_append(m, "MIME-Version: 1.0\r\n");
    g_string_append(m, "Content-Type: text/plain; charset=utf-8\r\n");
    g_string_append(m, "Content-Transfer-Encoding: 8bit\r\n");
    g_string_append(m, "\r\n");

    char **blines = g_strsplit(body ? body : "", "\n", -1);
    for (int i = 0; blines[i]; i++) {
        char *line = blines[i];
        gsize ll = strlen(line);
        if (ll && line[ll - 1] == '\r') line[ll - 1] = '\0';
        if (line[0] == '.') g_string_append_c(m, '.');
        g_string_append(m, line);
        g_string_append(m, "\r\n");
    }
    g_strfreev(blines);

    g_free(date);
    g_free(enc_subject);
    g_free(from_disp);
    g_free(mid);
    return g_string_free(m, FALSE);
}

typedef struct { ns_mail_account *a; char *to; char *cc; char *subject; char *body; } send_job;

static void
send_job_free(send_job *j)
{
    if (!j) return;
    account_free(j->a);
    g_free(j->to);
    g_free(j->cc);
    g_free(j->subject);
    g_free(j->body);
    g_free(j);
}

static struct curl_slist *
recipients_from(const char *to, const char *cc)
{
    struct curl_slist *r = NULL;
    char *all = g_strdup_printf("%s,%s", to ? to : "", cc ? cc : "");
    char **parts = g_strsplit(all, ",", -1);
    for (int i = 0; parts[i]; i++) {
        char *t = g_strstrip(parts[i]);
        if (*t) { char *ang = addr_angle(t); r = curl_slist_append(r, ang); g_free(ang); }
    }
    g_strfreev(parts);
    g_free(all);
    return r;
}

static gpointer
send_thread(gpointer data)
{
    send_job *j = data;
    ns_mail_account *a = j->a;
    char errbuf[CURL_ERROR_SIZE] = {0};
    char *url = outgoing_url(a);
    CURL *c = mail_curl_new(a, TRUE, errbuf, NULL);
    gboolean ok = FALSE;
    char *failmsg = NULL;

    if (c) {
        char *msg = build_message(a, j->to, j->cc, j->subject, j->body);
        upload_ctx up = { msg, strlen(msg), 0 };
        char *from = addr_angle(a->email);
        struct curl_slist *rcpt = recipients_from(j->to, j->cc);

        curl_easy_setopt(c, CURLOPT_URL, url);
        curl_easy_setopt(c, CURLOPT_MAIL_FROM, from);
        curl_easy_setopt(c, CURLOPT_MAIL_RCPT, rcpt);
        curl_easy_setopt(c, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(c, CURLOPT_READFUNCTION, mail_read_cb);
        curl_easy_setopt(c, CURLOPT_READDATA, &up);
        curl_easy_setopt(c, CURLOPT_INFILESIZE_LARGE, (curl_off_t)up.len);

        CURLcode rc = curl_easy_perform(c);
        if (rc == CURLE_OK) ok = TRUE;
        else failmsg = g_strdup(errbuf[0] ? errbuf : curl_easy_strerror(rc));

        curl_slist_free_all(rcpt);
        g_free(from);
        g_free(msg);
        curl_easy_cleanup(c);
    } else {
        failmsg = g_strdup("curl init failed");
    }
    g_free(url);

    g_mutex_lock(&g_send_lock);
    if (ok) {
        g_send_state = ST_DONE;
        g_clear_pointer(&g_send_error, g_free);
    } else {
        g_send_state = ST_ERROR;
        g_free(g_send_error);
        g_send_error = failmsg ? failmsg : g_strdup("Send failed");
        failmsg = NULL;
    }
    g_mutex_unlock(&g_send_lock);
    g_free(failmsg);

    send_job_free(j);
    return NULL;
}

void
ns_mail_send(const char *form)
{
    GHashTable *q = form && *form
        ? g_uri_parse_params(form, -1, "&", G_URI_PARAMS_WWW_FORM, NULL)
        : NULL;
    gboolean locked = FALSE;
    ns_mail_account *snap = account_snapshot(&locked);
    if (!account_complete(snap) || !snap->out_host || !*snap->out_host) {
        account_free(snap);
        if (q) g_hash_table_destroy(q);
        g_mutex_lock(&g_send_lock);
        g_send_state = ST_ERROR;
        g_free(g_send_error);
        g_send_error = g_strdup("Outgoing server not configured");
        g_mutex_unlock(&g_send_lock);
        return;
    }
    if (locked) {
        account_free(snap);
        if (q) g_hash_table_destroy(q);
        g_mutex_lock(&g_send_lock);
        g_send_state = ST_ERROR;
        g_free(g_send_error);
        g_send_error = g_strdup("Enter your primary password to unlock mail.");
        g_mutex_unlock(&g_send_lock);
        return;
    }

    send_job *j = g_new0(send_job, 1);
    j->a = snap;
    j->to = form_get(q, "to");
    j->cc = form_get(q, "cc");
    j->subject = form_get(q, "subject");
    j->body = form_get(q, "body");
    if (q) g_hash_table_destroy(q);

    g_mutex_lock(&g_send_lock);
    if (g_send_state == ST_RUNNING) { g_mutex_unlock(&g_send_lock); send_job_free(j); return; }
    GThread *old = g_send_thread;
    g_send_thread = NULL;
    g_send_state = ST_RUNNING;
    g_clear_pointer(&g_send_error, g_free);
    g_mutex_unlock(&g_send_lock);

    if (old) g_thread_join(old);

    g_mutex_lock(&g_send_lock);
    g_send_thread = g_thread_new("ns-mail-send", send_thread, j);
    g_mutex_unlock(&g_send_lock);
}

char *
ns_mail_send_status_json(void)
{
    g_mutex_lock(&g_send_lock);
    int st = g_send_state;
    char *err = ns_mail_json_escape(g_send_error ? g_send_error : "");
    g_mutex_unlock(&g_send_lock);
    char *o = g_strdup_printf("{\"state\":\"%s\",\"error\":\"%s\"}",
                              state_name(st), err);
    g_free(err);
    return o;
}

void
ns_mail_shutdown(void)
{
    g_mutex_lock(&g_inbox_lock);
    GThread *st = g_sync_thread; g_sync_thread = NULL;
    g_mutex_unlock(&g_inbox_lock);
    if (st) g_thread_join(st);

    g_mutex_lock(&g_open_lock);
    GThread *ot = g_open_thread; g_open_thread = NULL;
    g_mutex_unlock(&g_open_lock);
    if (ot) g_thread_join(ot);

    g_mutex_lock(&g_send_lock);
    GThread *sd = g_send_thread; g_send_thread = NULL;
    g_mutex_unlock(&g_send_lock);
    if (sd) g_thread_join(sd);
}
