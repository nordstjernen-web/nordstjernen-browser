/* Nordstjernen — SubtleCrypto primitives implemented over OpenSSL libcrypto. */

#ifndef ND_WEBCRYPTO_H
#define ND_WEBCRYPTO_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    ND_CK_SECRET,
    ND_CK_PUBLIC,
    ND_CK_PRIVATE,
} nd_ck_type;

enum {
    ND_USAGE_ENCRYPT     = 1u << 0,
    ND_USAGE_DECRYPT     = 1u << 1,
    ND_USAGE_SIGN        = 1u << 2,
    ND_USAGE_VERIFY      = 1u << 3,
    ND_USAGE_DERIVE_KEY  = 1u << 4,
    ND_USAGE_DERIVE_BITS = 1u << 5,
    ND_USAGE_WRAP        = 1u << 6,
    ND_USAGE_UNWRAP      = 1u << 7,
};

typedef struct nd_crypto_key {
    nd_ck_type type;
    char      *algo;
    char      *hash;
    char      *curve;
    int        bits;
    gboolean   extractable;
    guint32    usages;
    guint8    *raw;
    gsize      raw_len;
    void      *pkey;
    int        refcount;
} nd_crypto_key;

typedef struct {
    const guint8 *iv;
    gsize         iv_len;
    const guint8 *aad;
    gsize         aad_len;
    int           tag_bits;
    const guint8 *label;
    gsize         label_len;
    const guint8 *salt;
    gsize         salt_len;
    const guint8 *info;
    gsize         info_len;
    int           iterations;
    const char   *kdf_hash;
    const char   *sign_hash;
    nd_crypto_key *peer;
    int           counter_bits;
    int           pss_salt_len;
} nd_crypto_params;

nd_crypto_key *nd_crypto_key_ref(nd_crypto_key *k);
void           nd_crypto_key_unref(nd_crypto_key *k);

nd_crypto_key *nd_crypto_generate_secret(const char *algo, const char *hash,
                                         int length_bits, gboolean extractable,
                                         guint32 usages, char **err);

gboolean nd_crypto_generate_keypair(const char *algo, const char *hash,
                                    const char *curve, int modulus_bits,
                                    guint32 pubexp, gboolean extractable,
                                    guint32 usages, nd_crypto_key **pub,
                                    nd_crypto_key **priv, char **err);

nd_crypto_key *nd_crypto_import_raw(const char *format, const guint8 *data,
                                    gsize len, const char *algo, const char *hash,
                                    const char *curve, gboolean extractable,
                                    guint32 usages, char **err);

nd_crypto_key *nd_crypto_import_rsa_jwk(const guint8 *n, gsize n_len,
                                        const guint8 *e, gsize e_len,
                                        const guint8 *d, gsize d_len,
                                        const guint8 *p, gsize p_len,
                                        const guint8 *q, gsize q_len,
                                        const guint8 *dp, gsize dp_len,
                                        const guint8 *dq, gsize dq_len,
                                        const guint8 *qi, gsize qi_len,
                                        const char *algo, const char *hash,
                                        gboolean extractable, guint32 usages,
                                        char **err);

nd_crypto_key *nd_crypto_import_ec_jwk(const char *curve, const guint8 *x,
                                       gsize x_len, const guint8 *y, gsize y_len,
                                       const guint8 *d, gsize d_len,
                                       const char *algo, gboolean extractable,
                                       guint32 usages, char **err);

guint8 *nd_crypto_export_raw(const char *format, const nd_crypto_key *k,
                             gsize *out_len, char **err);

gboolean nd_crypto_export_rsa_jwk(const nd_crypto_key *k, guint8 **n, gsize *n_len,
                                  guint8 **e, gsize *e_len, guint8 **d, gsize *d_len,
                                  guint8 **p, gsize *p_len, guint8 **q, gsize *q_len,
                                  guint8 **dp, gsize *dp_len, guint8 **dq, gsize *dq_len,
                                  guint8 **qi, gsize *qi_len, char **err);

gboolean nd_crypto_export_ec_jwk(const nd_crypto_key *k, guint8 **x, gsize *x_len,
                                 guint8 **y, gsize *y_len, guint8 **d, gsize *d_len,
                                 char **err);

guint8 *nd_crypto_sign(const nd_crypto_key *k, const nd_crypto_params *p,
                       const guint8 *data, gsize len, gsize *out_len, char **err);

int nd_crypto_verify(const nd_crypto_key *k, const nd_crypto_params *p,
                     const guint8 *sig, gsize sig_len, const guint8 *data,
                     gsize len, char **err);

guint8 *nd_crypto_encrypt(const nd_crypto_key *k, const nd_crypto_params *p,
                          const guint8 *data, gsize len, gsize *out_len, char **err);

guint8 *nd_crypto_decrypt(const nd_crypto_key *k, const nd_crypto_params *p,
                          const guint8 *data, gsize len, gsize *out_len, char **err);

guint8 *nd_crypto_derive_bits(const nd_crypto_key *k, const nd_crypto_params *p,
                              int length_bits, gsize *out_len, char **err);

G_END_DECLS

#endif
