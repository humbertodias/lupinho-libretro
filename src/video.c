#include "video.h"
#include "ui.h"

extern Color palette[PALETTE_SIZE];

void present_frame_rgb565(uint16_t *out) {
    uint16_t lut[PALETTE_SIZE];
    lut[0] = 0;
    for (int i = 1; i < PALETTE_SIZE; i++) {
        Color c = palette[i];
        if (c.a == 0) {
            lut[i] = 0;
            continue;
        }
        lut[i] = (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
    }

    const uint8_t *src = &frame_buffer[0][0];
    int pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
    for (int i = 0; i < pixels; i++) {
        out[i] = lut[src[i]];
    }
}
