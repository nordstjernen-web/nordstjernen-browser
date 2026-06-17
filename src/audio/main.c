/* nordstjernen-audio: isolated MP3 playback helper driven over stdin/stdout. */
#include "miniaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>

#include <curl/curl.h>

#define NS_AUDIO_MAX_PLAYERS 16

typedef struct {
    char     token[64];
    int      used;
    int      loaded;
    int      playing;
    ma_sound sound;
    char    *tmp_path;
} ns_audio_player;

static ma_engine       g_engine;
static int             g_engine_ok;
static ns_audio_player g_players[NS_AUDIO_MAX_PLAYERS];

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
    if (p->loaded) ma_sound_uninit(&p->sound);
    if (p->tmp_path) {
        remove(p->tmp_path);
        free(p->tmp_path);
    }
    memset(p, 0, sizeof *p);
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
    player_release(p);
    p = player_alloc(token);

    if (!g_engine_ok) { emit("error %s no-audio-device", token); return; }

    char *tmp = NULL;
    const char *path = local_path_for(url, &tmp);
    if (!path) { emit("error %s fetch-failed", token); return; }
    p->tmp_path = tmp;

    ma_result r = ma_sound_init_from_file(&g_engine, path,
        MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
        NULL, NULL, &p->sound);
    if (r != MA_SUCCESS) { emit("error %s decode-failed", token); player_release(p); return; }
    p->loaded = 1;

    float len = 0.0f;
    ma_sound_get_length_in_seconds(&p->sound, &len);
    emit("meta %s %.3f", token, (double)len);
}

static void
cmd_play(const char *token)
{
    ns_audio_player *p = player_find(token);
    if (!p || !p->loaded) return;
    ma_sound_start(&p->sound);
    p->playing = 1;
    emit("playing %s", token);
}

static void
cmd_pause(const char *token)
{
    ns_audio_player *p = player_find(token);
    if (!p || !p->loaded) return;
    ma_sound_stop(&p->sound);
    p->playing = 0;
    emit("paused %s", token);
}

static void
cmd_seek(const char *token, double seconds)
{
    ns_audio_player *p = player_find(token);
    if (!p || !p->loaded) return;
    ma_uint32 rate = ma_engine_get_sample_rate(&g_engine);
    if (seconds < 0) seconds = 0;
    ma_sound_seek_to_pcm_frame(&p->sound, (ma_uint64)(seconds * rate));
}

static void
cmd_volume(const char *token, double vol)
{
    ns_audio_player *p = player_find(token);
    if (!p || !p->loaded) return;
    if (vol < 0) vol = 0;
    if (vol > 1) vol = 1;
    ma_sound_set_volume(&p->sound, (float)vol);
}

static void
cmd_stop(const char *token)
{
    ns_audio_player *p = player_find(token);
    if (p) player_release(p);
}

static void
poll_players(void)
{
    for (int i = 0; i < NS_AUDIO_MAX_PLAYERS; i++) {
        ns_audio_player *p = &g_players[i];
        if (!p->used || !p->loaded) continue;
        if (p->playing) {
            float cur = 0.0f;
            ma_sound_get_cursor_in_seconds(&p->sound, &cur);
            emit("pos %s %.3f", p->token, (double)cur);
            if (ma_sound_at_end(&p->sound)) {
                p->playing = 0;
                emit("ended %s", p->token);
            }
        }
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

    if (ma_engine_init(NULL, &g_engine) == MA_SUCCESS)
        g_engine_ok = 1;
    emit("ready %s", g_engine_ok ? "1" : "0");

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
        } else if (strcmp(op, "stop") == 0) {
            cmd_stop(token);
        }
    }

    for (int i = 0; i < NS_AUDIO_MAX_PLAYERS; i++)
        if (g_players[i].used) player_release(&g_players[i]);
    if (g_engine_ok) ma_engine_uninit(&g_engine);
    curl_global_cleanup();
    return 0;
}
