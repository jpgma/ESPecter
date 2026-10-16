#pragma once

#include "graphics/BaseDrawSurface.h"

namespace especter {

class st7789_drawer : public pixelroot32::graphics::BaseDrawSurface {
public:
    /* [st7789_drawer] Deixa o panel fechado. [st7789_drawer::init] aloca o framebuffer. */
    st7789_drawer();
    /* [st7789_drawer::~st7789_drawer] Não libera o panel. O LCD fica ligado até o reset. */
    ~st7789_drawer() override;

    /* [st7789_drawer::init] Sobe o SPI3, dois bands DMA e o panel, depois [backlight_start]. */
    void init() override;
    /* [st7789_drawer::clearBuffer] Zera o framebuffer e a palette. */
    void clearBuffer() override;
    /* [st7789_drawer::sendBuffer] Converte o band ocioso enquanto o outro segue no DMA. */
    void sendBuffer() override;
    /* [st7789_drawer::drawPixel] Grava um índice da palette no pixel. */
    void drawPixel(int x, int y, uint16_t color) override;
    /* [st7789_drawer::drawFilledRectangle] Preenche um rectangle com um índice da palette. Uma linha cheia usa memset. */
    void drawFilledRectangle(int x, int y, int w, int h, uint16_t color) override;

private:
    /* [st7789_drawer::index_for] Devolve o índice da cor na palette, criando entrada se couber. */
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
