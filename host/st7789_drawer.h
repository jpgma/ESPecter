#pragma once

#include "graphics/BaseDrawSurface.h"

namespace especter {

class st7789_drawer : public pixelroot32::graphics::BaseDrawSurface {
public:
    st7789_drawer();
    ~st7789_drawer() override;

    void init() override;
    void clearBuffer() override;
    void sendBuffer() override;
    void drawPixel(int x, int y, uint16_t color) override;
    void drawFilledRectangle(int x, int y, int w, int h, uint16_t color) override;

private:
    uint8_t index_for(uint16_t color);

    uint8_t *pixels;
    uint16_t *band;
    uint16_t palette[16];
    int palette_count;
    int ready;
    void *panel;
    void *panel_io;
    void *spi_done;
};

} // namespace especter
