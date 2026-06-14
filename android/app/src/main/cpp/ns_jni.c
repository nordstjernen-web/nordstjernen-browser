/* Nordstjernen — JNI bridge from the Android host app to renderer IPC. */

#include <jni.h>
#include <android/bitmap.h>
#include <android/log.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ipc_http.h"
#include "libnordstjernen.h"
#include "renderer_serve.h"
#include "rproc_http.h"

#define LOG_TAG "nordstjernen"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define NS_ANDROID_MAX_W 2560
#define NS_ANDROID_MAX_H 4096

typedef struct {
    int                  ctrl_r;
    int                  ctrl_w;
    unsigned char       *fb;
    ns_renderer_session *session;
} AndroidRenderer;

typedef struct {
    ns_rproc_http *renderer;
    int            page_width;
    int            page_height;
    char          *title;
    char          *url;
} AndroidPage;

static pthread_mutex_t g_init_lock = PTHREAD_MUTEX_INITIALIZER;
static int             g_engine_inited;

static char *
jstr_dup(JNIEnv *env, jstring s)
{
    if (!s) return NULL;
    const char *c = (*env)->GetStringUTFChars(env, s, NULL);
    char *out = c ? strdup(c) : NULL;
    if (c) (*env)->ReleaseStringUTFChars(env, s, c);
    return out;
}

static void
close_pair(int ctrl_r, int ctrl_w)
{
    if (ctrl_r >= 0)
        close(ctrl_r);
    if (ctrl_w >= 0 && ctrl_w != ctrl_r)
        close(ctrl_w);
}

static void *
renderer_thread_main(void *data)
{
    AndroidRenderer *r = data;
    http_conn c;
    http_conn_init(&c, r->ctrl_r);

    for (;;) {
        http_head head;
        if (http_read_head(&c, &head) != 0 ||
            head.content_length > NS_HTTP_MAX_BODY)
            break;

        char *body = NULL;
        if (head.content_length > 0) {
            body = malloc((size_t)head.content_length + 1);
            if (!body ||
                http_read_body(&c, head.content_length, body) != 0) {
                free(body);
                break;
            }
            body[head.content_length] = '\0';
        }

        int quit = ns_renderer_session_handle(r->session, &head, body);
        free(body);
        if (quit)
            break;
    }

    ns_renderer_session_free(r->session);
    close_pair(r->ctrl_r, r->ctrl_w);
    free(r->fb);
    free(r);
    return NULL;
}

static int
android_inproc_attach(int ctrl_r, int ctrl_w, unsigned char *fb,
                      int max_w, int max_h)
{
    AndroidRenderer *r = calloc(1, sizeof *r);
    if (!r)
        return -1;
    r->ctrl_r = ctrl_r;
    r->ctrl_w = ctrl_w;
    r->fb = fb;
    r->session = ns_renderer_session_new(ctrl_w, fb, max_w, max_h, 1);
    if (!r->session) {
        free(r);
        return -1;
    }

    pthread_t thread;
    if (pthread_create(&thread, NULL, renderer_thread_main, r) != 0) {
        ns_renderer_session_free(r->session);
        free(r);
        return -1;
    }
    pthread_detach(thread);
    return 0;
}

static void
frame_clear(ns_rproc_http_frame *frame)
{
    free(frame->nav);
    free(frame->webgl);
    free(frame->download);
}

static AndroidPage *
page_from_handle(jlong handle)
{
    return (AndroidPage *)(intptr_t)handle;
}

JNIEXPORT jboolean JNICALL
Java_com_nordstjernen_browser_NativeBrowser_nativeEngineAvailable(JNIEnv *env,
                                                                  jclass clazz)
{
    (void)env; (void)clazz;
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_com_nordstjernen_browser_NativeBrowser_nativeInit(JNIEnv *env, jclass clazz,
                                                       jstring data_dir,
                                                       jstring ca_bundle)
{
    (void)clazz;
    pthread_mutex_lock(&g_init_lock);
    if (g_engine_inited) {
        pthread_mutex_unlock(&g_init_lock);
        return 0;
    }

    char *dir = jstr_dup(env, data_dir);
    char *ca = jstr_dup(env, ca_bundle);
    if (dir && *dir) {
        setenv("HOME", dir, 1);
        setenv("XDG_CONFIG_HOME", dir, 1);
        setenv("XDG_CACHE_HOME", dir, 1);
        setenv("XDG_DATA_HOME", dir, 1);
    }
    if (ca && *ca)
        setenv("CURL_CA_BUNDLE", ca, 1);
    free(dir);
    free(ca);

    int rc = ns_browser_init();
    if (rc == 0) {
        ns_rproc_http_set_inproc(android_inproc_attach);
        g_engine_inited = 1;
    }
    pthread_mutex_unlock(&g_init_lock);
    LOGI("ns_browser_init rc=%d", rc);
    return rc;
}

JNIEXPORT jlong JNICALL
Java_com_nordstjernen_browser_NativeBrowser_nativeOpen(JNIEnv *env, jclass clazz,
                                                       jstring url,
                                                       jint viewport_width,
                                                       jint viewport_height,
                                                       jint settle_ms)
{
    (void)clazz;
    if (!g_engine_inited)
        return 0;

    char *u = jstr_dup(env, url);
    if (!u)
        return 0;

    ns_rproc_http *renderer =
        ns_rproc_http_spawn(NULL, NS_ANDROID_MAX_W, NS_ANDROID_MAX_H);
    if (!renderer) {
        free(u);
        return 0;
    }

    int vw = viewport_width > 0 ? viewport_width : 360;
    int vh = viewport_height > 0 ? viewport_height : (vw * 3) / 4;
    ns_rproc_http_page opened;
    if (ns_rproc_http_open(renderer, u, vw, vh, settle_ms, &opened) != 0 ||
        !opened.ok) {
        ns_rproc_http_page_clear(&opened);
        ns_rproc_http_close(renderer);
        free(u);
        return 0;
    }
    free(u);

    AndroidPage *page = calloc(1, sizeof *page);
    if (!page) {
        ns_rproc_http_page_clear(&opened);
        ns_rproc_http_close(renderer);
        return 0;
    }
    page->renderer = renderer;
    page->page_width = opened.page_width;
    page->page_height = opened.page_height;
    page->title = opened.title;
    page->url = opened.url;
    opened.title = NULL;
    opened.url = NULL;
    ns_rproc_http_page_clear(&opened);
    return (jlong)(intptr_t)page;
}

JNIEXPORT jintArray JNICALL
Java_com_nordstjernen_browser_NativeBrowser_nativePageSize(JNIEnv *env,
                                                           jclass clazz,
                                                           jlong handle)
{
    (void)clazz;
    AndroidPage *page = page_from_handle(handle);
    if (!page)
        return NULL;
    jintArray arr = (*env)->NewIntArray(env, 2);
    if (!arr)
        return NULL;
    jint vals[2] = { page->page_width, page->page_height };
    (*env)->SetIntArrayRegion(env, arr, 0, 2, vals);
    return arr;
}

JNIEXPORT jboolean JNICALL
Java_com_nordstjernen_browser_NativeBrowser_nativeRender(JNIEnv *env,
                                                         jclass clazz,
                                                         jlong handle,
                                                         jint scroll_x,
                                                         jint scroll_y,
                                                         jdouble scale,
                                                         jobject bitmap)
{
    (void)clazz;
    AndroidPage *page = page_from_handle(handle);
    if (!page || !page->renderer || !bitmap)
        return JNI_FALSE;

    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
        info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        LOGE("nativeRender: bad bitmap format");
        return JNI_FALSE;
    }

    ns_rproc_http_frame frame;
    if (ns_rproc_http_render(page->renderer, (int)info.width, (int)info.height,
                             scroll_x, scroll_y, scale, &frame) != 0 ||
        !frame.ok) {
        frame_clear(&frame);
        return JNI_FALSE;
    }
    if (frame.unchanged) {
        frame_clear(&frame);
        return JNI_TRUE;
    }

    void *pixels = NULL;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS) {
        frame_clear(&frame);
        return JNI_FALSE;
    }

    int rows = frame.height < (int)info.height ? frame.height : (int)info.height;
    int cols = frame.width < (int)info.width ? frame.width : (int)info.width;
    for (int y = 0; y < rows; y++) {
        const unsigned char *src = frame.pixels + (size_t)y * frame.stride;
        unsigned char *dst = (unsigned char *)pixels + (size_t)y * info.stride;
        for (int x = 0; x < cols; x++) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }

    AndroidBitmap_unlockPixels(env, bitmap);
    frame_clear(&frame);
    return JNI_TRUE;
}

JNIEXPORT jstring JNICALL
Java_com_nordstjernen_browser_NativeBrowser_nativeTitle(JNIEnv *env,
                                                        jclass clazz,
                                                        jlong handle)
{
    (void)clazz;
    AndroidPage *page = page_from_handle(handle);
    if (!page || !page->title)
        return NULL;
    return (*env)->NewStringUTF(env, page->title);
}

JNIEXPORT jstring JNICALL
Java_com_nordstjernen_browser_NativeBrowser_nativeRenderText(JNIEnv *env,
                                                             jclass clazz,
                                                             jlong handle)
{
    (void)clazz;
    AndroidPage *page = page_from_handle(handle);
    char *text = page && page->renderer ? ns_rproc_http_dump(page->renderer,
                                                             "text")
                                        : NULL;
    if (!text)
        return NULL;
    jstring s = (*env)->NewStringUTF(env, text);
    free(text);
    return s;
}

JNIEXPORT jstring JNICALL
Java_com_nordstjernen_browser_NativeBrowser_nativeLinkAt(JNIEnv *env,
                                                         jclass clazz,
                                                         jlong handle,
                                                         jint x, jint y)
{
    (void)clazz;
    AndroidPage *page = page_from_handle(handle);
    char *url = page && page->renderer ? ns_rproc_http_link_at(page->renderer,
                                                               x, y)
                                      : NULL;
    if (!url || !*url) {
        free(url);
        return NULL;
    }
    jstring s = (*env)->NewStringUTF(env, url);
    free(url);
    return s;
}

JNIEXPORT void JNICALL
Java_com_nordstjernen_browser_NativeBrowser_nativeClose(JNIEnv *env,
                                                        jclass clazz,
                                                        jlong handle)
{
    (void)env; (void)clazz;
    AndroidPage *page = page_from_handle(handle);
    if (!page)
        return;
    ns_rproc_http_close(page->renderer);
    free(page->title);
    free(page->url);
    free(page);
}

JNIEXPORT void JNICALL
Java_com_nordstjernen_browser_NativeBrowser_nativeShutdown(JNIEnv *env,
                                                          jclass clazz)
{
    (void)env; (void)clazz;
    pthread_mutex_lock(&g_init_lock);
    if (g_engine_inited) {
        ns_browser_shutdown();
        g_engine_inited = 0;
    }
    pthread_mutex_unlock(&g_init_lock);
}
