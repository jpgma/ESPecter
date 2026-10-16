#pragma once

#include "graphics/BaseDrawSurface.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace especter {

/* [st7789_lit] Devolve 1 enquanto o fade pedido está aceso ou o duty ainda não chegou a 0. */
int st7789_lit(void);
/* [st7789_take_dark] Devolve 1 uma vez quando o duty chega a 0. */
int st7789_take_dark(void);
/* [st7789_bind_present] Guarda a task que [st7789_fade_on] acorda. */
void st7789_bind_present(TaskHandle_t task);
/* [st7789_hold_scan] Enquanto ligado, [backlight_scan] não troca o fade. */
void st7789_hold_scan(int hold);
/* [st7789_fade_on] Acende o backlight e engole o press do PWR que ainda está baixo. */
void st7789_fade_on(void);
/* [st7789_pwr_down] Devolve 1 enquanto o PWR está pressionado. */
int st7789_pwr_down(void);
/* [st7789_pwr_pin] Devolve o GPIO do PWR. */
gpio_num_t st7789_pwr_pin(void);
/* [st7789_pwr_ready] Reaplica o PWR como entrada com pull-up depois do light sleep. */
void st7789_pwr_ready(void);

class st7789_drawer : public pixelroot32::graphics::BaseDrawSurface {
public:
    /* [st7789_drawer] Deixa o panel fechado. [st7789_drawer::init] aloca o framebuffer. */
    st7789_drawer();
    /* [st7789_drawer::~st7789_drawer] Não libera o panel. O LCD fica ligado até o reset. */
    ~st7789_drawer() override;

    /* [st7789_drawer::init] Sobe o SPI3, dois bands DMA e o panel, zera os índices uma vez, depois [backlight_start]. */
    void init() override;
    /* [st7789_drawer::clearBuffer] Não mexe na palette nem nos índices. A cena cobre a View. */
    void clearBuffer() override;
    /* [st7789_drawer::sendBuffer] Converte o band ocioso enquanto o outro segue no DMA. */
    void sendBuffer() override;
    /* [st7789_drawer::drawPixel] Grava um índice da palette no pixel. */
    void drawPixel(int x, int y, uint16_t color) override;
    /* [st7789_drawer::drawFilledRectangle] Preenche um rectangle com um índice da palette. Uma linha cheia usa memset. */
    void drawFilledRectangle(int x, int y, int w, int h, uint16_t color) override;

private:
    /* [st7789_drawer::index_for] Devolve o índice dessa RGB565 em palette_rgb565. Se não achar, devolve 0. */
    uint8_t index_for(uint16_t color);

    uint8_t *pixels;
    uint16_t *band;
    int ready;
    void *panel;
    void *panel_io;
    void *spi_done;
};

} // namespace especter
