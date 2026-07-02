/* Nordstjernen — authenticated secret sealing (PBKDF2-SHA256 + AES-256-GCM). */

#include "secretbox.h"

#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#define NS_SBX_PREFIX "nsbx1:"
#define NS_SBX_ITERS  600000
#define NS_SBX_ITERS_MAX  10000000
#define NS_SBX_SALT   16
#define NS_SBX_IV     12
#define NS_SBX_TAG    16
#define NS_SBX_KEY    32

gboolean
ns_secretbox_is_sealed(const char *s)
{
    return s && g_str_has_prefix(s, NS_SBX_PREFIX);
}

static gboolean
derive_key(const char *password, const guint8 *salt, gsize salt_len,
           int iters, guint8 *key)
{
    return PKCS5_PBKDF2_HMAC(password ? password : "", -1, salt, (int)salt_len,
                             iters, EVP_sha256(), NS_SBX_KEY, key) == 1;
}

char *
ns_secretbox_seal(const char *plaintext, const char *password)
{
    if (!plaintext) plaintext = "";
    guint8 salt[NS_SBX_SALT], iv[NS_SBX_IV], key[NS_SBX_KEY], tag[NS_SBX_TAG];
    if (RAND_bytes(salt, sizeof salt) != 1) return NULL;
    if (RAND_bytes(iv, sizeof iv) != 1) return NULL;
    if (!derive_key(password, salt, NS_SBX_SALT, NS_SBX_ITERS, key)) return NULL;

    gsize ptlen = strlen(plaintext);
    guint8 *ct = g_malloc(ptlen + EVP_MAX_BLOCK_LENGTH);
    int len = 0, ctlen = 0;
    gboolean ok = FALSE;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx &&
        EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, NS_SBX_IV, NULL) == 1 &&
        EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) == 1 &&
        EVP_EncryptUpdate(ctx, ct, &len, (const guint8 *)plaintext, (int)ptlen) == 1) {
        ctlen = len;
        if (EVP_EncryptFinal_ex(ctx, ct + ctlen, &len) == 1) {
            ctlen += len;
            if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, NS_SBX_TAG, tag) == 1)
                ok = TRUE;
        }
    }
    if (ctx) EVP_CIPHER_CTX_free(ctx);

    char *out = NULL;
    if (ok) {
        char *b_salt = g_base64_encode(salt, NS_SBX_SALT);
        char *b_iv   = g_base64_encode(iv, NS_SBX_IV);
        char *b_ct   = g_base64_encode(ct, (gsize)ctlen);
        char *b_tag  = g_base64_encode(tag, NS_SBX_TAG);
        out = g_strdup_printf("%s%d:%s:%s:%s:%s", NS_SBX_PREFIX, NS_SBX_ITERS,
                              b_salt, b_iv, b_ct, b_tag);
        g_free(b_salt);
        g_free(b_iv);
        g_free(b_ct);
        g_free(b_tag);
    }
    OPENSSL_cleanse(key, sizeof key);
    g_free(ct);
    return out;
}

char *
ns_secretbox_open(const char *blob, const char *password)
{
    if (!ns_secretbox_is_sealed(blob)) return NULL;
    char **f = g_strsplit(blob + strlen(NS_SBX_PREFIX), ":", -1);
    char *out = NULL;
    if (g_strv_length(f) == 5) {
        gint64 iters = g_ascii_strtoll(f[0], NULL, 10);
        gsize slen = 0, ivlen = 0, ctlen = 0, taglen = 0;
        guint8 *salt = g_base64_decode(f[1], &slen);
        guint8 *iv   = g_base64_decode(f[2], &ivlen);
        guint8 *ct   = g_base64_decode(f[3], &ctlen);
        guint8 *tag  = g_base64_decode(f[4], &taglen);
        if (iters > 0 && iters <= NS_SBX_ITERS_MAX && slen == NS_SBX_SALT &&
            ivlen == NS_SBX_IV && taglen == NS_SBX_TAG) {
            guint8 key[NS_SBX_KEY];
            if (derive_key(password, salt, slen, (int)iters, key)) {
                guint8 *pt = g_malloc(ctlen + 1);
                int len = 0, ptlen = 0;
                gboolean ok = FALSE;
                EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
                if (ctx &&
                    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
                    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, NS_SBX_IV, NULL) == 1 &&
                    EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) == 1 &&
                    EVP_DecryptUpdate(ctx, pt, &len, ct, (int)ctlen) == 1) {
                    ptlen = len;
                    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, NS_SBX_TAG, tag);
                    if (EVP_DecryptFinal_ex(ctx, pt + ptlen, &len) == 1) {
                        ptlen += len;
                        pt[ptlen] = '\0';
                        out = (char *)pt;
                        ok = TRUE;
                    }
                }
                if (!ok) g_free(pt);
                if (ctx) EVP_CIPHER_CTX_free(ctx);
                OPENSSL_cleanse(key, sizeof key);
            }
        }
        g_free(salt);
        g_free(iv);
        g_free(ct);
        g_free(tag);
    }
    g_strfreev(f);
    return out;
}
