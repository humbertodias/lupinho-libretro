#include "blit.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * draw_tile, draw_spr and draw_map_layer in the simulator open, allocate and
 * read the bitmap on every call. A scene full of tiles does that hundreds of
 * times per frame and lands around 20 FPS. These replacements keep each file
 * in memory and write palette indexes straight into the framebuffer.
 *
 * ui.c is compiled with the original names redirected, so the link uses these.
 */

extern int clip_x;
extern int clip_y;
extern int clip_w;
extern int clip_h;
extern bool clip_enabled;

typedef struct {
    char *path;
    uint8_t *data;
    long size;
} Bitmap;

static Bitmap *bitmaps;
static int bitmap_count;
static int bitmap_cap;

void lupi_bitmap_cache_clear(void) {
    for (int i = 0; i < bitmap_count; i++) {
        free(bitmaps[i].path);
        free(bitmaps[i].data);
    }
    free(bitmaps);
    bitmaps = NULL;
    bitmap_count = 0;
    bitmap_cap = 0;
}

static Bitmap *bitmap_get(const char *path) {
    if (!path) return NULL;

    for (int i = 0; i < bitmap_count; i++) {
        if (strcmp(bitmaps[i].path, path) == 0) return &bitmaps[i];
    }

    if (bitmap_count == bitmap_cap) {
        int cap = bitmap_cap ? bitmap_cap * 2 : 16;
        Bitmap *next = (Bitmap *)realloc(bitmaps, (size_t)cap * sizeof(Bitmap));
        if (!next) return NULL;
        bitmaps = next;
        bitmap_cap = cap;
    }

    Bitmap *bitmap = &bitmaps[bitmap_count];
    memset(bitmap, 0, sizeof(*bitmap));
    bitmap->size = -1;

    size_t path_len = strlen(path);
    bitmap->path = (char *)malloc(path_len + 1);
    if (!bitmap->path) return NULL;
    memcpy(bitmap->path, path, path_len + 1);

    FILE *file = fopen(path, "rb");
    if (file) {
        if (fseek(file, 0, SEEK_END) == 0) {
            long size = ftell(file);
            if (size >= 0 && fseek(file, 0, SEEK_SET) == 0) {
                uint8_t *data = NULL;
                if (size == 0 || (data = (uint8_t *)malloc((size_t)size)) != NULL) {
                    if (size == 0 || fread(data, 1, (size_t)size, file) == (size_t)size) {
                        bitmap->data = data;
                        bitmap->size = size;
                    } else {
                        free(data);
                    }
                }
            }
        }
        fclose(file);
    }

    bitmap_count++;
    if (bitmap->size < 0) {
        printf("Warning: Could not open bitmap %s\n", path);
    }
    return bitmap;
}

static void blit_indexed(const uint8_t *src, int sw, int sh, int dx, int dy, int flip_x, int flip_y) {
    if (sw <= 0 || sh <= 0) return;

    int x0 = 0;
    int y0 = 0;
    int x1 = sw;
    int y1 = sh;

    if (dx < 0) x0 = -dx;
    if (dy < 0) y0 = -dy;
    if (dx + sw > SCREEN_WIDTH) x1 = SCREEN_WIDTH - dx;
    if (dy + sh > SCREEN_HEIGHT) y1 = SCREEN_HEIGHT - dy;

    if (clip_enabled) {
        int cx0 = clip_x - dx;
        int cy0 = clip_y - dy;
        int cx1 = clip_x + clip_w - dx;
        int cy1 = clip_y + clip_h - dy;
        if (x0 < cx0) x0 = cx0;
        if (y0 < cy0) y0 = cy0;
        if (x1 > cx1) x1 = cx1;
        if (y1 > cy1) y1 = cy1;
    }

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sw) x1 = sw;
    if (y1 > sh) y1 = sh;
    if (x0 >= x1 || y0 >= y1) return;

    for (int row = y0; row < y1; row++) {
        int src_row = flip_y ? (sh - 1 - row) : row;
        const uint8_t *srow = src + (size_t)src_row * (size_t)sw;
        uint8_t *drow = frame_buffer[dy + row];
        if (!flip_x) {
            for (int col = x0; col < x1; col++) {
                uint8_t idx = srow[col];
                if (idx) drow[dx + col] = idx;
            }
        } else {
            for (int col = x0; col < x1; col++) {
                uint8_t idx = srow[sw - 1 - col];
                if (idx) drow[dx + col] = idx;
            }
        }
    }
}

static int tile_in_view(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    if (x + w <= 0 || y + h <= 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return 0;
    if (clip_enabled) {
        if (x + w <= clip_x || y + h <= clip_y ||
            x >= clip_x + clip_w || y >= clip_y + clip_h) return 0;
    }
    return 1;
}

void draw_tile(const char *path, int width, int height, int tile_index, int x, int y, bool flipped) {
    x -= camera_x;
    y -= camera_y;
    if (!tile_in_view(x, y, width, height)) return;

    Bitmap *bitmap = bitmap_get(path);
    if (!bitmap || bitmap->size < 0 || width <= 0 || height <= 0) return;

    long tile_pixels = (long)width * (long)height;
    if (tile_pixels <= 0 || tile_index < 0) return;
    if ((long)tile_index >= bitmap->size / tile_pixels) return;

    const uint8_t *tile = bitmap->data + (size_t)tile_index * (size_t)tile_pixels;
    blit_indexed(tile, width, height, x, y, flipped ? 1 : 0, 0);
}

void draw_spr(const char *path, int width, int height, int x, int y, bool flipped) {
    x -= camera_x;
    y -= camera_y;
    if (!tile_in_view(x, y, width, height)) return;

    Bitmap *bitmap = bitmap_get(path);
    if (!bitmap || bitmap->size < 0) return;

    long pixels = (long)width * (long)height;
    if (pixels <= 0 || pixels > bitmap->size) return;

    blit_indexed(bitmap->data, width, height, x, y, flipped ? 1 : 0, 0);
}

void draw_map_layer(MapLayerData *data, int map_width, int tile_size, int cam_x, int cam_y) {
    if (!data || !data->path || !data->tile_ids || map_width <= 0) return;
    if (data->tile_w <= 0 || data->tile_h <= 0) return;

    Bitmap *bitmap = bitmap_get(data->path);
    if (!bitmap || bitmap->size < 0) return;

    long tile_pixels = (long)data->tile_w * (long)data->tile_h;
    if (tile_pixels <= 0 || bitmap->size < tile_pixels) return;
    long max_id = bitmap->size / tile_pixels;

    for (int map_idx = 0; map_idx < data->count; map_idx++) {
        if (data->tile_ids[map_idx] < 0) continue;

        int tile_id_flags = data->tile_ids[map_idx];
        int flip_x = (tile_id_flags & 1024) != 0;
        int flip_y = (tile_id_flags & 2048) != 0;
        int tile_id = tile_id_flags & ~(1024 | 2048);
        if (tile_id < 0 || (long)tile_id >= max_id) continue;

        int col = map_idx % map_width;
        int row = map_idx / map_width;
        int sx = col * tile_size + cam_x - camera_x;
        int sy = row * tile_size + cam_y - camera_y;
        if (!tile_in_view(sx, sy, data->tile_w, data->tile_h)) continue;

        const uint8_t *tile = bitmap->data + (size_t)tile_id * (size_t)tile_pixels;
        blit_indexed(tile, data->tile_w, data->tile_h, sx, sy, flip_x, flip_y);
    }
}
