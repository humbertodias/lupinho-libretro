#include "music.h"
#include "music_render.h"

#include "music/fur_player.h"

#include <lauxlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char game_root[512] = "";
static char active_path[512] = "";
static int song_open = 0;
static int fading = 0;
static volatile float volume = 0.0f;
static float fade_from = 0.0f;
static float fade_to = 0.0f;
static float fade_dur = 0.0f;
static float fade_t = 0.0f;

static float *mix = NULL;
static unsigned mix_frames = 0;

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

static int resolve_fur(const char *rel, char *out, size_t out_len) {
    const char *dot = strrchr(rel, '.');
    int has_ext = dot != NULL && strcmp(dot, ".fur") == 0;
    char candidate[1024];

    if (game_root[0] != '\0') {
        if (!has_ext) {
            snprintf(candidate, sizeof(candidate), "%s/%s.fur", game_root, rel);
            if (file_exists(candidate)) {
                snprintf(out, out_len, "%s", candidate);
                return 1;
            }
        }
        snprintf(candidate, sizeof(candidate), "%s/%s", game_root, rel);
        if (file_exists(candidate)) {
            snprintf(out, out_len, "%s", candidate);
            return 1;
        }
    }
    if (!has_ext) {
        snprintf(candidate, sizeof(candidate), "%s.fur", rel);
        if (file_exists(candidate)) {
            snprintf(out, out_len, "%s", candidate);
            return 1;
        }
    }
    if (file_exists(rel)) {
        snprintf(out, out_len, "%s", rel);
        return 1;
    }
    return 0;
}

static unsigned char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    long size;
    unsigned char *data;
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    data = (unsigned char *)malloc((size_t)size);
    if (data == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)size;
    return data;
}

static void begin_fade(float from, float to, float seconds) {
    if (seconds <= 0.001f) {
        volume = to;
        fading = 0;
        return;
    }
    fade_from = from;
    fade_to = to;
    fade_dur = seconds;
    fade_t = 0.0f;
    volume = from;
    fading = 1;
}

static int start_song(const char *path, float fade) {
    char resolved[1024];
    unsigned char *data;
    size_t len = 0;
    if (!resolve_fur(path, resolved, sizeof(resolved))) {
        fprintf(stderr, "music: file not found: %s\n", path);
        return 0;
    }
    data = read_file(resolved, &len);
    if (data == NULL) {
        fprintf(stderr, "music: could not read %s\n", resolved);
        return 0;
    }
    if (!fur_player_load(data, len, "song.fur")) {
        free(data);
        return 0;
    }
    free(data);
    snprintf(active_path, sizeof(active_path), "%s", path);
    song_open = 1;
    begin_fade(0.0f, 1.0f, fade);
    return 1;
}

static int lua_music_loop(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    float fade = 0.0f;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) fade = (float)luaL_checknumber(L, 2);
    if (fade < 0.0f) fade = 0.0f;
    if (song_open && strcmp(active_path, path) == 0 && !(fading && fade_to <= 0.001f)) {
        return 0;
    }
    start_song(path, fade);
    return 0;
}

static int lua_music_stop(lua_State *L) {
    float fade = 0.0f;
    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) fade = (float)luaL_checknumber(L, 1);
    if (fade < 0.0f) fade = 0.0f;
    if (!song_open) return 0;
    if (fade <= 0.001f) {
        fur_player_stop();
        song_open = 0;
        active_path[0] = '\0';
        volume = 0.0f;
        fading = 0;
        return 0;
    }
    begin_fade(volume, 0.0f, fade);
    return 0;
}

void music_bind(lua_State *L) {
    lua_newtable(L);
    lua_pushcfunction(L, lua_music_loop);
    lua_setfield(L, -2, "loop");
    lua_pushcfunction(L, lua_music_stop);
    lua_setfield(L, -2, "stop");
    lua_setglobal(L, "music");
}

void music_set_root(const char *dir) {
    if (dir == NULL) dir = "";
    snprintf(game_root, sizeof(game_root), "%s", dir);
}

void music_update(float dt) {
    if (!fading) return;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    fade_t += dt;
    if (fade_t >= fade_dur) {
        volume = fade_to;
        fading = 0;
        if (fade_to <= 0.001f) {
            fur_player_stop();
            song_open = 0;
            active_path[0] = '\0';
        }
        return;
    }
    volume = fade_from + (fade_to - fade_from) * (fade_t / fade_dur);
}

static int16_t sample_to_s16(float s) {
    if (s > 1.0f) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    return (int16_t)(s * 32767.0f);
}

void music_render(int16_t *dst, unsigned frames) {
    unsigned i;
    float vol;
    if (dst == NULL || frames == 0) return;
    if (!song_open) {
        memset(dst, 0, (size_t)frames * 2 * sizeof(int16_t));
        return;
    }
    if (frames > mix_frames) {
        float *next = (float *)realloc(mix, (size_t)frames * 2 * sizeof(float));
        if (next == NULL) {
            memset(dst, 0, (size_t)frames * 2 * sizeof(int16_t));
            return;
        }
        mix = next;
        mix_frames = frames;
    }
    fur_player_render(mix, frames);
    vol = volume;
    for (i = 0; i < frames * 2; i++) dst[i] = sample_to_s16(mix[i] * vol);
}

void music_shutdown(void) {
    fur_player_shutdown();
    free(mix);
    mix = NULL;
    mix_frames = 0;
    song_open = 0;
    fading = 0;
    volume = 0.0f;
    active_path[0] = '\0';
}
