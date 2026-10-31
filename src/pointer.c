#include "pointer.h"

#include "ui.h"

#include <lauxlib.h>

/*
 * The retropad still aims with the d-pad. The mouse is reported separately so
 * a click or a movement can place the crosshair without taking the arrows
 * away. ui.pointer() returns x, y, just_pressed, absolute. With no mouse
 * activity this frame it returns nil.
 */

static int sample_x;
static int sample_y;
static int sample_valid;
static int sample_pressed;
static int sample_absolute;

static int prev_left;
static int16_t prev_px;
static int16_t prev_py;
static int16_t prev_lx;
static int16_t prev_ly;
static int pointer_live;
static int lightgun_live;

static int axis_to_pixel(int value, int size) {
    if (value < -32767) value = -32767;
    if (value > 32767) value = 32767;
    return (int)(((long)value + 32767) * (size - 1) / 65534);
}

static int in_view(int16_t x, int16_t y, int16_t offscreen) {
    if (offscreen) return 0;
    if (x == (int16_t)-32768 || y == (int16_t)-32768) return 0;
    return 1;
}

static int lua_pointer(lua_State *L) {
    if (!sample_valid) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, sample_x);
    lua_pushinteger(L, sample_y);
    lua_pushboolean(L, sample_pressed);
    lua_pushboolean(L, sample_absolute);
    return 4;
}

void lupi_pointer_install(lua_State *L) {
    lua_getglobal(L, "ui");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_pushcfunction(L, lua_pointer);
    lua_setfield(L, -2, "pointer");
    lua_pop(L, 1);
}

void lupi_pointer_poll(retro_input_state_t cb) {
    int16_t dx, dy, left;
    int16_t px, py, poff;
    int16_t lx, ly, loff;
    int moved;

    sample_valid = 0;
    sample_pressed = 0;
    sample_absolute = 0;
    if (!cb) return;

    dx = cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
    dy = cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
    left = cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT);
    px = cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
    py = cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
    /*
     * POINTER_IS_OFFSCREEN is id 15. Some frontends (BizHawk 2.11) only
     * accept X/Y/PRESSED/COUNT and abort on anything else. Off-screen is
     * already reported as X/Y = -32768.
     */
    poff = 0;
    lx = cb(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
    ly = cb(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);
    loff = cb(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN);

    if (!pointer_live && in_view(px, py, poff) && (px != prev_px || py != prev_py))
        pointer_live = 1;
    if (!lightgun_live && in_view(lx, ly, loff) && (lx != prev_lx || ly != prev_ly))
        lightgun_live = 1;
    prev_px = px;
    prev_py = py;
    prev_lx = lx;
    prev_ly = ly;

    moved = dx != 0 || dy != 0;
    sample_pressed = left && !prev_left;
    prev_left = left != 0;

    if (!moved && !left) return;

    if (pointer_live && in_view(px, py, poff)) {
        sample_x = axis_to_pixel(px, SCREEN_WIDTH);
        sample_y = axis_to_pixel(py, SCREEN_HEIGHT);
        sample_absolute = 1;
        sample_valid = 1;
        return;
    }
    if (lightgun_live && in_view(lx, ly, loff)) {
        sample_x = axis_to_pixel(lx, SCREEN_WIDTH);
        sample_y = axis_to_pixel(ly, SCREEN_HEIGHT);
        sample_absolute = 1;
        sample_valid = 1;
        return;
    }

    sample_x = dx;
    sample_y = dy;
    sample_absolute = 0;
    sample_valid = 1;
}
