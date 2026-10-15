#pragma once

/* Injected before every translation unit. Keeps PixelRoot32 on the small set. */

#define PIXELROOT32_ENABLE_AUDIO 0
#define PIXELROOT32_ENABLE_PHYSICS 0
#define PIXELROOT32_ENABLE_PARTICLES 0
#define PIXELROOT32_ENABLE_UI_SYSTEM 0
#define PIXELROOT32_ENABLE_TOUCH 0
#define PIXELROOT32_ENABLE_SCENE_TRANSITIONS 0
#define PIXELROOT32_ENABLE_CAMERA_EFFECTS 0
#define PIXELROOT32_ENABLE_STATIC_TILEMAP_FB_CACHE 0
#define PIXELROOT32_NO_TFT_ESPI 1
#define PIXELROOT32_NO_I2S_AUDIO 1

#define LOGICAL_WIDTH 240
#define LOGICAL_HEIGHT 240
#define PHYSICAL_DISPLAY_WIDTH 240
#define PHYSICAL_DISPLAY_HEIGHT 240

#if defined(_MSC_VER)
#define IRAM_ATTR
static inline int __builtin_popcount(unsigned int value)
{
    int count = 0;
    while (value != 0u) {
        count += (int)(value & 1u);
        value >>= 1u;
    }
    return count;
}
#endif
