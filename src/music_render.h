#ifndef MUSIC_RENDER_H
#define MUSIC_RENDER_H

#include <stdint.h>

/* Interleaved stereo s16 at the Furnace rate (44100). Silence when no song plays. */
void music_render(int16_t *interleaved, unsigned frames);

#endif
