#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libretro.h"
#include "ui.h"
#include "lua_api.h"
#include "input.h"
#include "pointer.h"
#include "zip.h"
#include "palette.h"
#include "video.h"
#include "blit.h"

#define SAMPLE_RATE 44100
#define AUDIO_FRAMES (SAMPLE_RATE / 60)

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;

static uint16_t video_buffer[SCREEN_WIDTH * SCREEN_HEIGHT];
static int16_t audio_buffer[AUDIO_FRAMES * 2];

static char game_dir[512];
static char temp_game_dir[512];
static int extracted_to_tmp;
static int game_loaded;

static void log_msg(enum retro_log_level level, const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (log_cb) log_cb(level, "%s", buf);
    else fprintf(stderr, "%s", buf);
}

static int path_is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

static int path_ends_with(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) return 0;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

static const char *path_basename(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    return base;
}

static int parent_directory(const char *path, char *out, size_t out_size) {
    size_t len = strlen(path);
    while (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\')) len--;

    size_t slash = len;
    while (slash > 0 && path[slash - 1] != '/' && path[slash - 1] != '\\') slash--;

    if (slash == 0) {
        if (out_size < 2) return -1;
        memcpy(out, ".", 2);
        return 0;
    }

    size_t dir_len = slash - 1;
    if (dir_len == 0) {
        if (out_size < 2) return -1;
        out[0] = path[0];
        out[1] = '\0';
        return 0;
    }
    if (dir_len >= out_size) return -1;
    memcpy(out, path, dir_len);
    out[dir_len] = '\0';
    return 0;
}

static void poll_joypad(unsigned port) {
    static const struct {
        unsigned retro_id;
        int lupi_button;
    } map[] = {
        { RETRO_DEVICE_ID_JOYPAD_UP,     GAMEPAD_BUTTON_LEFT_FACE_UP },
        { RETRO_DEVICE_ID_JOYPAD_DOWN,   GAMEPAD_BUTTON_LEFT_FACE_DOWN },
        { RETRO_DEVICE_ID_JOYPAD_LEFT,   GAMEPAD_BUTTON_LEFT_FACE_LEFT },
        { RETRO_DEVICE_ID_JOYPAD_RIGHT,  GAMEPAD_BUTTON_LEFT_FACE_RIGHT },
        { RETRO_DEVICE_ID_JOYPAD_A,      GAMEPAD_BUTTON_RIGHT_FACE_RIGHT },
        { RETRO_DEVICE_ID_JOYPAD_B,      GAMEPAD_BUTTON_RIGHT_FACE_DOWN },
        { RETRO_DEVICE_ID_JOYPAD_X,      GAMEPAD_BUTTON_RIGHT_FACE_UP },
        { RETRO_DEVICE_ID_JOYPAD_Y,      GAMEPAD_BUTTON_RIGHT_FACE_LEFT },
        { RETRO_DEVICE_ID_JOYPAD_L,      GAMEPAD_BUTTON_LEFT_TRIGGER_1 },
        { RETRO_DEVICE_ID_JOYPAD_R,      GAMEPAD_BUTTON_RIGHT_TRIGGER_1 },
        { RETRO_DEVICE_ID_JOYPAD_L2,     GAMEPAD_BUTTON_LEFT_TRIGGER_2 },
        { RETRO_DEVICE_ID_JOYPAD_R2,     GAMEPAD_BUTTON_RIGHT_TRIGGER_2 },
        { RETRO_DEVICE_ID_JOYPAD_SELECT, GAMEPAD_BUTTON_MIDDLE_LEFT },
        { RETRO_DEVICE_ID_JOYPAD_START,  GAMEPAD_BUTTON_MIDDLE_RIGHT },
        { RETRO_DEVICE_ID_JOYPAD_L3,     GAMEPAD_BUTTON_LEFT_THUMB },
        { RETRO_DEVICE_ID_JOYPAD_R3,     GAMEPAD_BUTTON_RIGHT_THUMB },
    };

    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, map[i].retro_id)) {
            lupi_input_set((int)port, map[i].lupi_button, true);
        }
    }

    int16_t ax = input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
    int16_t ay = input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
    const int deadzone = 20000;
    if (ay < -deadzone) lupi_input_set((int)port, GAMEPAD_BUTTON_LEFT_FACE_UP, true);
    if (ay >  deadzone) lupi_input_set((int)port, GAMEPAD_BUTTON_LEFT_FACE_DOWN, true);
    if (ax < -deadzone) lupi_input_set((int)port, GAMEPAD_BUTTON_LEFT_FACE_LEFT, true);
    if (ax >  deadzone) lupi_input_set((int)port, GAMEPAD_BUTTON_LEFT_FACE_RIGHT, true);
}

static void clear_host_modules(void) {
    lua_getglobal(globalLuaState, "package");
    if (lua_istable(globalLuaState, -1)) {
        lua_pushliteral(globalLuaState, "");
        lua_setfield(globalLuaState, -2, "cpath");
    }
    lua_pop(globalLuaState, 1);
}

/* lua_api_setup_game reports a failed game.lua on stdout and still returns. */
static bool setup_game(const char *dir) {
    int saved = dup(fileno(stdout));
    FILE *log = tmpfile();
    if (saved < 0 || !log) {
        if (saved >= 0) close(saved);
        if (log) fclose(log);
        lua_api_setup_game(dir);
        return true;
    }

    fflush(stdout);
    dup2(fileno(log), fileno(stdout));
    lua_api_setup_game(dir);
    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);

    rewind(log);
    char *text = NULL;
    size_t len = 0;
    char chunk[1024];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), log)) > 0) {
        char *next = realloc(text, len + n + 1);
        if (!next) break;
        text = next;
        memcpy(text + len, chunk, n);
        len += n;
        text[len] = '\0';
    }
    fclose(log);

    bool failed = text && strstr(text, "Error loading game:") != NULL;
    if (text && len > 0) {
        if (failed) {
            if (log_cb) log_cb(RETRO_LOG_ERROR, "[Lupinho] %s", text);
            else fwrite(text, 1, len, stderr);
        } else {
            fwrite(text, 1, len, stdout);
        }
    }
    free(text);
    return !failed;
}

static bool load_game_at(const char *dir) {
    char game_path[512];

    lupi_bitmap_cache_clear();
    snprintf(game_dir, sizeof(game_dir), "%s", dir);
    lua_api_init();
    if (!globalLuaState) {
        log_msg(RETRO_LOG_ERROR, "[Lupinho] Failed to create Lua state\n");
        return false;
    }

    clear_host_modules();
    lupi_install_palette_loader(globalLuaState, game_dir);
    lupi_pointer_install(globalLuaState);
    reset_ui_state();

    snprintf(game_path, sizeof(game_path), "%s/game.lua", game_dir);
    if (luaL_loadfile(globalLuaState, game_path) != LUA_OK) {
        log_msg(RETRO_LOG_ERROR, "[Lupinho] Failed to load game.lua: %s\n",
                lua_tostring(globalLuaState, -1));
        lua_pop(globalLuaState, 1);
        lua_api_close();
        return false;
    }
    lua_pop(globalLuaState, 1);

    if (!setup_game(game_dir)) {
        lua_api_close();
        return false;
    }
    game_loaded = 1;
    return true;
}

static bool start_game(const char *path) {
    extracted_to_tmp = 0;
    game_dir[0] = '\0';
    temp_game_dir[0] = '\0';

    if (path_is_dir(path)) {
        return load_game_at(path);
    }

    /* RetroArch picks files. game.lua stands for the directory that contains it. */
    if (strcmp(path_basename(path), "game.lua") == 0) {
        char dir[512];
        if (parent_directory(path, dir, sizeof(dir)) != 0) {
            log_msg(RETRO_LOG_ERROR, "[Lupinho] Failed to resolve game directory for %s\n", path);
            return false;
        }
        return load_game_at(dir);
    }

    if (path_ends_with(path, ".lupi")) {
        const char *save_dir = NULL;
        /* Android cannot mkdir /tmp. RetroArch's save directory is writable. */
        if (!environ_cb || !environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir))
            save_dir = NULL;
        lupi_set_extract_root(save_dir);

        if (extract_lupi_to_tmp(path, temp_game_dir, sizeof(temp_game_dir)) != 0) {
            log_msg(RETRO_LOG_ERROR, "[Lupinho] Failed to extract %s: %s\n",
                    path, lupi_zip_error());
            return false;
        }
        extracted_to_tmp = 1;
        if (!load_game_at(temp_game_dir)) {
            cleanup_lupi_tmp(temp_game_dir);
            extracted_to_tmp = 0;
            temp_game_dir[0] = '\0';
            return false;
        }
        return true;
    }

    log_msg(RETRO_LOG_ERROR, "[Lupinho] Unsupported content: %s\n", path);
    return false;
}

static void stop_game(void) {
    if (game_loaded) {
        lua_api_close();
        game_loaded = 0;
    }
    lupi_bitmap_cache_clear();
    if (extracted_to_tmp) {
        cleanup_lupi_tmp(temp_game_dir);
        extracted_to_tmp = 0;
        temp_game_dir[0] = '\0';
    }
    game_dir[0] = '\0';
}

void retro_set_environment(retro_environment_t cb) {
    environ_cb = cb;
    bool no_content = false;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

    struct retro_log_callback logging;
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging)) {
        log_cb = logging.log;
    }
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_init(void) {
    memset(audio_buffer, 0, sizeof(audio_buffer));
}

void retro_deinit(void) {
    if (game_loaded || extracted_to_tmp) stop_game();
}

unsigned retro_api_version(void) {
    return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof(*info));
    info->library_name = "Lupinho";
    info->library_version = "0.1.0";
    info->valid_extensions = "lupi|lua";
    info->need_fullpath = true;
    info->block_extract = true;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
    memset(info, 0, sizeof(*info));
    info->geometry.base_width = SCREEN_WIDTH;
    info->geometry.base_height = SCREEN_HEIGHT;
    info->geometry.max_width = SCREEN_WIDTH;
    info->geometry.max_height = SCREEN_HEIGHT;
    info->geometry.aspect_ratio = (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT;
    info->timing.fps = 60.0;
    info->timing.sample_rate = SAMPLE_RATE;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
    (void)port;
    (void)device;
}

void retro_reset(void) {
    char saved[512];
    if (!game_loaded) return;
    snprintf(saved, sizeof(saved), "%s", game_dir);
    lua_api_close();
    game_loaded = 0;
    load_game_at(saved);
}

void retro_run(void) {
    lupi_input_begin_frame();
    if (input_poll_cb) input_poll_cb();
    if (input_state_cb) {
        poll_joypad(0);
        poll_joypad(1);
        lupi_pointer_poll(input_state_cb);
    }

    lua_api_call_update();
    present_frame_rgb565(video_buffer);
    if (video_cb) {
        video_cb(video_buffer, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH * sizeof(uint16_t));
    }
    if (audio_batch_cb) audio_batch_cb(audio_buffer, AUDIO_FRAMES);
    clear_frame_buffer();
}

size_t retro_serialize_size(void) { return 0; }

bool retro_serialize(void *data, size_t size) {
    (void)data;
    (void)size;
    return false;
}

bool retro_unserialize(const void *data, size_t size) {
    (void)data;
    (void)size;
    return false;
}

void retro_cheat_reset(void) {}

void retro_cheat_set(unsigned index, bool enabled, const char *code) {
    (void)index;
    (void)enabled;
    (void)code;
}

bool retro_load_game(const struct retro_game_info *game) {
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        log_msg(RETRO_LOG_ERROR, "[Lupinho] RGB565 is required\n");
        return false;
    }

    static const struct retro_input_descriptor desc[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "Up" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Down" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Left" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "Right" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "BTN_X" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "BTN_Z" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "BTN_Q" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "BTN_E" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "BTN_F" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "BTN_G" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Start" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
        { 0 },
    };
    environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void *)desc);

    if (!game || !game->path) {
        log_msg(RETRO_LOG_ERROR, "[Lupinho] A .lupi file or game directory is required\n");
        return false;
    }

    return start_game(game->path);
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) {
    (void)type;
    (void)info;
    (void)num;
    return false;
}

void retro_unload_game(void) {
    if (game_loaded || extracted_to_tmp) stop_game();
}

unsigned retro_get_region(void) {
    return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id) {
    (void)id;
    return NULL;
}

size_t retro_get_memory_size(unsigned id) {
    (void)id;
    return 0;
}
