/* nordstjernen-audio: isolated MP3 / MPEG-1 audio playback helper driven over stdin/stdout. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#ifdef main
#undef main
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>
#include <unistd.h>

#include <curl/curl.h>

#include "pl_mpeg.h"

#ifndef MINIMP3_FLOAT_OUTPUT
#define MINIMP3_FLOAT_OUTPUT
#endif
#include "minimp3.h"

#define NS_AUDIO_MAX_PLAYERS 16
#define NS_AUDIO_MAX_SECONDS 1800
#define NS_AUDIO_DEVICE_RATE 44100
#define NS_AUDIO_MAX_BYTES   (256u * 1024u * 1024u)

typedef struct {
    char    token[64];
    int     used;
    int     playing;
    int     loop;
    float  *pcm;
    size_t  frames;
    size_t  cursor;
    float   volume;
    int     reached_end;
    char   *tmp_path;
} ns_audio_player;

static SDL_AudioDeviceID g_dev;
static int               g_dev_ok;
static ns_audio_player   g_players[NS_AUDIO_MAX_PLAYERS];

#if defined(__GNUC__)
#define NS_AUDIO_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define NS_AUDIO_PRINTF(a, b)
#endif

static void emit(const char *fmt, ...) NS_AUDIO_PRINTF(1, 2);

static void
emit(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

static void
audio_lock(void)
{
    if (g_dev_ok) SDL_LockAudioDevice(g_dev);
}

static void
audio_unlock(void)
{
    if (g_dev_ok) SDL_UnlockAudioDevice(g_dev);
}

static ns_audio_player *
player_find(const char *token)
{
    for (int i = 0; i < NS_AUDIO_MAX_PLAYERS; i++)
        if (g_players[i].used && strcmp(g_players[i].token, token) == 0)
            return &g_players[i];
    return NULL;
}

static ns_audio_player *
player_alloc(const char *token)
{
    ns_audio_player *p = player_find(token);
    if (p) return p;
    for (int i = 0; i < NS_AUDIO_MAX_PLAYERS; i++) {
        if (!g_players[i].used) {
            memset(&g_players[i], 0, sizeof g_players[i]);
            g_players[i].used = 1;
            g_players[i].volume = 1.0f;
            snprintf(g_players[i].token, sizeof g_players[i].token, "%s", token);
            return &g_players[i];
        }
    }
    return NULL;
}

static void
player_release(ns_audio_player *p)
{
    if (!p || !p->used) return;
    char *tmp = p->tmp_path;
    free(p->pcm);
    memset(p, 0, sizeof *p);
    if (tmp) {
        remove(tmp);
        free(tmp);
    }
}

static void
audio_cb(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    float *out = (float *)(void *)stream;
    int frame_bytes = (int)(2 * sizeof(float));
    int nframes = len / frame_bytes;
    SDL_memset(stream, 0, (size_t)len);

    for (int i = 0; i < NS_AUDIO_MAX_PLAYERS; i++) {
        ns_audio_player *p = &g_players[i];
        if (!p->used || !p->playing || !p->pcm) continue;
        for (int f = 0; f < nframes; f++) {
            if (p->cursor >= p->frames) {
                if (p->loop && p->frames > 0) {
                    p->cursor = 0;
                } else {
                    p->playing = 0;
                    p->reached_end = 1;
                    break;
                }
            }
            out[f * 2 + 0] += p->pcm[p->cursor * 2 + 0] * p->volume;
            out[f * 2 + 1] += p->pcm[p->cursor * 2 + 1] * p->volume;
            p->cursor++;
        }
    }

    int total = nframes * 2;
    for (int i = 0; i < total; i++) {
        if (out[i] > 1.0f) out[i] = 1.0f;
        else if (out[i] < -1.0f) out[i] = -1.0f;
    }
}

static unsigned char *
read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || (size_t)n > NS_AUDIO_MAX_BYTES) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    unsigned char *bytes = malloc((size_t)n);
    if (!bytes) { fclose(f); return NULL; }
    if (fread(bytes, 1, (size_t)n, f) != (size_t)n) {
        free(bytes); fclose(f); return NULL;
    }
    fclose(f);
    *out_len = (size_t)n;
    return bytes;
}

static int
bytes_are_mpeg1(const unsigned char *b, size_t n)
{
    return n >= 4 && b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x01 &&
           (b[3] == 0xBA || b[3] == 0xB3);
}

static int
decode_mpeg(const unsigned char *bytes, size_t n,
            float **out_pcm, size_t *out_frames, int *out_rate, int *out_ch)
{
    plm_t *plm = plm_create_with_memory((uint8_t *)bytes, n, 0);
    if (!plm) return 0;
    plm_set_video_enabled(plm, 0);
    plm_set_audio_enabled(plm, 1);
    int rate = plm_get_samplerate(plm);
    if (plm_get_num_audio_streams(plm) < 1 || rate <= 0) {
        plm_destroy(plm);
        return 0;
    }

    size_t cap = (size_t)rate * 2u * 8u;
    size_t len = 0;
    float *pcm = malloc(cap * sizeof(float));
    if (!pcm) { plm_destroy(plm); return 0; }
    size_t max_floats = (size_t)rate * 2u * NS_AUDIO_MAX_SECONDS;
    plm_samples_t *s;
    while ((s = plm_decode_audio(plm)) != NULL) {
        size_t add = (size_t)s->count * 2u;
        if (len + add > cap) {
            while (len + add > cap) {
                if (cap > SIZE_MAX / (2u * sizeof(float))) {
                    free(pcm); plm_destroy(plm); return 0;
                }
                cap *= 2;
            }
            float *grown = realloc(pcm, cap * sizeof(float));
            if (!grown) { free(pcm); plm_destroy(plm); return 0; }
            pcm = grown;
        }
        memcpy(pcm + len, s->interleaved, add * sizeof(float));
        len += add;
        if (len >= max_floats) break;
    }
    plm_destroy(plm);
    if (len == 0) { free(pcm); return 0; }

    *out_pcm = pcm;
    *out_frames = len / 2;
    *out_rate = rate;
    *out_ch = 2;
    return 1;
}

static int
decode_mp3(const unsigned char *bytes, size_t n,
           float **out_pcm, size_t *out_frames, int *out_rate, int *out_ch)
{
    if (n > (size_t)INT_MAX) return 0;

    mp3dec_t dec;
    mp3dec_init(&dec);
    mp3dec_frame_info_t info;
    mp3d_sample_t frame[MINIMP3_MAX_SAMPLES_PER_FRAME];

    const uint8_t *p = bytes;
    int rem = (int)n;
    int rate = 0, ch = 0;
    size_t cap = 0, len = 0;
    float *pcm = NULL;
    size_t max_floats = 0;

    while (rem > 0) {
        int samples = mp3dec_decode_frame(&dec, p, rem, frame, &info);
        if (info.frame_bytes <= 0) break;
        if (samples > 0) {
            if (rate == 0) {
                rate = info.hz;
                ch = info.channels;
                if (rate <= 0 || ch < 1) { free(pcm); return 0; }
                max_floats = (size_t)rate * (size_t)ch * NS_AUDIO_MAX_SECONDS;
            }
            if (info.channels == ch && info.hz == rate) {
                size_t add = (size_t)samples * (size_t)ch;
                if (len + add > cap) {
                    size_t want = cap ? cap : (size_t)rate * (size_t)ch;
                    while (len + add > want) {
                        if (want > SIZE_MAX / (2u * sizeof(float))) {
                            free(pcm); return 0;
                        }
                        want *= 2;
                    }
                    float *grown = realloc(pcm, want * sizeof(float));
                    if (!grown) { free(pcm); return 0; }
                    pcm = grown;
                    cap = want;
                }
                memcpy(pcm + len, frame, add * sizeof(float));
                len += add;
                if (len >= max_floats) break;
            }
        }
        p += info.frame_bytes;
        rem -= info.frame_bytes;
    }

    if (rate == 0 || len == 0) { free(pcm); return 0; }

    *out_pcm = pcm;
    *out_frames = len / (size_t)ch;
    *out_rate = rate;
    *out_ch = ch;
    return 1;
}

static int
resample_to_device(const float *src, size_t src_frames, int src_rate, int src_ch,
                   float **out_pcm, size_t *out_frames)
{
    if (src_ch < 1 || src_rate <= 0 || src_frames == 0) return 0;
    size_t src_bytes = src_frames * (size_t)src_ch * sizeof(float);
    if (src_bytes > (size_t)INT_MAX) return 0;

    SDL_AudioStream *st = SDL_NewAudioStream(
        AUDIO_F32SYS, (Uint8)src_ch, src_rate,
        AUDIO_F32SYS, 2, NS_AUDIO_DEVICE_RATE);
    if (!st) return 0;
    if (SDL_AudioStreamPut(st, src, (int)src_bytes) < 0) {
        SDL_FreeAudioStream(st); return 0;
    }
    SDL_AudioStreamFlush(st);
    int avail = SDL_AudioStreamAvailable(st);
    if (avail <= 0) { SDL_FreeAudioStream(st); return 0; }
    float *buf = malloc((size_t)avail);
    if (!buf) { SDL_FreeAudioStream(st); return 0; }
    int got = SDL_AudioStreamGet(st, buf, avail);
    SDL_FreeAudioStream(st);
    if (got <= 0) { free(buf); return 0; }

    *out_pcm = buf;
    *out_frames = (size_t)got / (2 * sizeof(float));
    return 1;
}

static int
load_audio(ns_audio_player *p, const char *path)
{
    size_t n = 0;
    unsigned char *bytes = read_file(path, &n);
    if (!bytes) return 0;

    float *src = NULL;
    size_t src_frames = 0;
    int src_rate = 0, src_ch = 0;
    int ok = bytes_are_mpeg1(bytes, n)
        ? decode_mpeg(bytes, n, &src, &src_frames, &src_rate, &src_ch)
        : decode_mp3(bytes, n, &src, &src_frames, &src_rate, &src_ch);
    free(bytes);
    if (!ok) return 0;

    float *dev = NULL;
    size_t dev_frames = 0;
    ok = resample_to_device(src, src_frames, src_rate, src_ch, &dev, &dev_frames);
    free(src);
    if (!ok) return 0;

    audio_lock();
    p->pcm = dev;
    p->frames = dev_frames;
    p->cursor = 0;
    p->reached_end = 0;
    p->playing = 0;
    audio_unlock();
    return 1;
}

static size_t
curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    FILE *f = userdata;
    return fwrite(ptr, size, nmemb, f);
}

static char *
write_temp_from_url(const char *url)
{
    char tmpl[] = "/tmp/nsaudio-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, "wb");
    if (!f) { close(fd); remove(tmpl); return NULL; }

    int ok = 0;
    CURL *c = curl_easy_init();
    if (c) {
        curl_easy_setopt(c, CURLOPT_URL, url);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(c, CURLOPT_USERAGENT, "Nordstjernen-Audio");
        ok = curl_easy_perform(c) == CURLE_OK;
        curl_easy_cleanup(c);
    }
    fclose(f);
    if (!ok) { remove(tmpl); return NULL; }
    return strdup(tmpl);
}

static const char *
local_path_for(const char *url, char **tmp_out)
{
    *tmp_out = NULL;
    if (strncmp(url, "file://", 7) == 0) {
        const char *p = url + 7;
        if (strncmp(p, "localhost", 9) == 0) p += 9;
        return p;
    }
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0 ||
        strncmp(url, "data:", 5) == 0) {
        char *t = write_temp_from_url(url);
        if (!t) return NULL;
        *tmp_out = t;
        return t;
    }
    return url;
}

static void
cmd_open(const char *token, const char *url)
{
    ns_audio_player *p = player_alloc(token);
    if (!p) { emit("error %s too-many-players", token); return; }
    audio_lock();
    player_release(p);
    audio_unlock();
    p = player_alloc(token);

    if (!g_dev_ok) { emit("error %s no-audio-device", token); return; }

    char *tmp = NULL;
    const char *path = local_path_for(url, &tmp);
    if (!path) { emit("error %s fetch-failed", token); return; }
    p->tmp_path = tmp;

    if (!load_audio(p, path)) {
        emit("error %s decode-failed", token);
        audio_lock();
        player_release(p);
        audio_unlock();
        return;
    }

    double len = (double)p->frames / NS_AUDIO_DEVICE_RATE;
    emit("meta %s %.3f", token, len);
}

static void
cmd_play(const char *token)
{
    ns_audio_player *p = player_find(token);
    if (!p || !p->pcm) return;
    audio_lock();
    if (p->cursor >= p->frames) p->cursor = 0;
    p->reached_end = 0;
    p->playing = 1;
    audio_unlock();
    emit("playing %s", token);
}

static void
cmd_pause(const char *token)
{
    ns_audio_player *p = player_find(token);
    if (!p || !p->pcm) return;
    audio_lock();
    p->playing = 0;
    audio_unlock();
    emit("paused %s", token);
}

static void
cmd_seek(const char *token, double seconds)
{
    ns_audio_player *p = player_find(token);
    if (!p || !p->pcm) return;
    if (seconds < 0) seconds = 0;
    size_t frame = (size_t)(seconds * NS_AUDIO_DEVICE_RATE);
    audio_lock();
    if (frame > p->frames) frame = p->frames;
    p->cursor = frame;
    p->reached_end = 0;
    audio_unlock();
}

static void
cmd_volume(const char *token, double vol)
{
    ns_audio_player *p = player_find(token);
    if (!p || !p->pcm) return;
    if (vol < 0) vol = 0;
    if (vol > 1) vol = 1;
    audio_lock();
    p->volume = (float)vol;
    audio_unlock();
}

static void
cmd_loop(const char *token, int on)
{
    ns_audio_player *p = player_find(token);
    if (!p) return;
    audio_lock();
    p->loop = on ? 1 : 0;
    audio_unlock();
}

static void
cmd_stop(const char *token)
{
    ns_audio_player *p = player_find(token);
    if (!p) return;
    audio_lock();
    player_release(p);
    audio_unlock();
}

static void
poll_players(void)
{
    struct { char token[64]; double pos; int ended; int active; }
        snap[NS_AUDIO_MAX_PLAYERS];
    int m = 0;
    audio_lock();
    for (int i = 0; i < NS_AUDIO_MAX_PLAYERS; i++) {
        ns_audio_player *p = &g_players[i];
        if (!p->used || !p->pcm) continue;
        if (p->playing || p->reached_end) {
            memcpy(snap[m].token, p->token, sizeof snap[m].token);
            snap[m].pos = (double)p->cursor / NS_AUDIO_DEVICE_RATE;
            snap[m].ended = p->reached_end;
            snap[m].active = p->playing;
            p->reached_end = 0;
            m++;
        }
    }
    audio_unlock();

    for (int i = 0; i < m; i++) {
        if (snap[i].active) emit("pos %s %.3f", snap[i].token, snap[i].pos);
        if (snap[i].ended) emit("ended %s", snap[i].token);
    }
}

static char *
next_token(char **cursor)
{
    char *s = *cursor;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0') { *cursor = s; return NULL; }
    char *start = s;
    while (*s && *s != ' ' && *s != '\t') s++;
    if (*s) { *s = '\0'; s++; }
    *cursor = s;
    return start;
}

int
main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want, have;
        SDL_memset(&want, 0, sizeof want);
        want.freq = NS_AUDIO_DEVICE_RATE;
        want.format = AUDIO_F32SYS;
        want.channels = 2;
        want.samples = 1024;
        want.callback = audio_cb;
        g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (g_dev) {
            g_dev_ok = 1;
            SDL_PauseAudioDevice(g_dev, 0);
        }
    }
    emit("ready %s", g_dev_ok ? "1" : "0");

    char line[4096];
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        char *cur = line;
        char *op = next_token(&cur);
        if (!op) { poll_players(); continue; }

        if (strcmp(op, "quit") == 0) break;
        if (strcmp(op, "poll") == 0) { poll_players(); continue; }

        char *token = next_token(&cur);
        if (!token) continue;

        if (strcmp(op, "open") == 0) {
            while (*cur == ' ') cur++;
            cmd_open(token, cur);
        } else if (strcmp(op, "play") == 0) {
            cmd_play(token);
        } else if (strcmp(op, "pause") == 0) {
            cmd_pause(token);
        } else if (strcmp(op, "seek") == 0) {
            char *v = next_token(&cur);
            cmd_seek(token, v ? atof(v) : 0.0);
        } else if (strcmp(op, "volume") == 0) {
            char *v = next_token(&cur);
            cmd_volume(token, v ? atof(v) : 1.0);
        } else if (strcmp(op, "loop") == 0) {
            char *v = next_token(&cur);
            cmd_loop(token, v ? atoi(v) : 0);
        } else if (strcmp(op, "stop") == 0) {
            cmd_stop(token);
        }
    }

    if (g_dev_ok) {
        SDL_CloseAudioDevice(g_dev);
        g_dev_ok = 0;
    }
    for (int i = 0; i < NS_AUDIO_MAX_PLAYERS; i++)
        if (g_players[i].used) player_release(&g_players[i]);
    SDL_Quit();
    curl_global_cleanup();
    return 0;
}
