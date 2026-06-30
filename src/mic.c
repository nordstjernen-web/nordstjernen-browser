/* Nordstjernen — microphone capture (SDL) for getUserMedia audio + Web Audio. */

#include "mic.h"

#include <math.h>
#include <string.h>

#if defined(NS_HAVE_SDL)

#include <SDL.h>

#define NS_MIC_RING 16384

static SDL_AudioDeviceID g_mic_dev;
static int g_mic_refs;
static float g_mic_ring[NS_MIC_RING];
static volatile guint g_mic_head;
static SDL_AudioSpec g_mic_spec;

static void
ns_mic_cb(void *user, Uint8 *stream, int len)
{
    (void)user;
    int channels = g_mic_spec.channels > 0 ? g_mic_spec.channels : 1;
    int frames = (int)((gsize)len / sizeof(float)) / channels;
    guint head = g_mic_head;
    for (int i = 0; i < frames; i++) {
        float acc = 0.0f;
        for (int c = 0; c < channels; c++) {
            float sample;
            memcpy(&sample, stream + (gsize)(i * channels + c) * sizeof(float),
                   sizeof sample);
            acc += sample;
        }
        g_mic_ring[head % NS_MIC_RING] = acc / (float)channels;
        head++;
    }
    g_mic_head = head;
}

gboolean
ns_mic_acquire(void)
{
    if (g_mic_dev) { g_mic_refs++; return TRUE; }
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return FALSE;
    SDL_AudioSpec want;
    SDL_memset(&want, 0, sizeof want);
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = ns_mic_cb;
    g_mic_dev = SDL_OpenAudioDevice(NULL, 1, &want, &g_mic_spec,
                                    SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
                                    SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    if (!g_mic_dev)
        return FALSE;
    g_mic_head = 0;
    memset(g_mic_ring, 0, sizeof g_mic_ring);
    SDL_PauseAudioDevice(g_mic_dev, 0);
    g_mic_refs++;
    return TRUE;
}

void
ns_mic_release(void)
{
    if (g_mic_refs > 0 && --g_mic_refs == 0 && g_mic_dev) {
        SDL_CloseAudioDevice(g_mic_dev);
        g_mic_dev = 0;
    }
}

gboolean
ns_mic_active(void)
{
    return g_mic_dev != 0;
}

void
ns_mic_fill_time_domain(guint8 *out, int n)
{
    if (!out || n <= 0) return;
    if (!g_mic_dev) {
        memset(out, 128, (gsize)n);
        return;
    }
    SDL_LockAudioDevice(g_mic_dev);
    guint head = g_mic_head;
    for (int i = 0; i < n; i++) {
        int idx = (int)head - n + i;
        float s = 0.0f;
        if (idx >= 0)
            s = g_mic_ring[((guint)idx) % NS_MIC_RING];
        int v = 128 + (int)(s * 128.0f);
        out[i] = (guint8)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
    SDL_UnlockAudioDevice(g_mic_dev);
}

void
ns_mic_fill_frequency(guint8 *out, int n)
{
    if (!out || n <= 0) return;
    if (!g_mic_dev) {
        memset(out, 0, (gsize)n);
        return;
    }
    int win = n * 2;
    if (win > NS_MIC_RING) win = NS_MIC_RING;
    float snap[NS_MIC_RING];
    SDL_LockAudioDevice(g_mic_dev);
    guint head = g_mic_head;
    for (int t = 0; t < win; t++) {
        int idx = (int)head - win + t;
        snap[t] = idx >= 0 ? g_mic_ring[((guint)idx) % NS_MIC_RING] : 0.0f;
    }
    SDL_UnlockAudioDevice(g_mic_dev);
    for (int k = 0; k < n; k++) {
        double freq = (double)(k + 1) * M_PI / (double)n;
        double re = 0.0, im = 0.0;
        for (int t = 0; t < win; t++) {
            re += snap[t] * cos(freq * t);
            im -= snap[t] * sin(freq * t);
        }
        double mag = sqrt(re * re + im * im) / (double)win;
        int db = (int)(mag * 4.0 * 255.0);
        out[k] = (guint8)(db < 0 ? 0 : db > 255 ? 255 : db);
    }
}

#else /* !NS_HAVE_SDL */

gboolean ns_mic_acquire(void) { return FALSE; }
void     ns_mic_release(void) {}
gboolean ns_mic_active(void) { return FALSE; }
void ns_mic_fill_time_domain(guint8 *out, int n) {
    if (out && n > 0) memset(out, 128, (gsize)n);
}
void ns_mic_fill_frequency(guint8 *out, int n) {
    if (out && n > 0) memset(out, 0, (gsize)n);
}

#endif
