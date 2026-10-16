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
/* [st7789_prepare_sleep] Deixa o RST alto e segura os pads do ST7789 para o light sleep. */
void st7789_prepare_sleep(void);
/* [st7789_resume] Solta os pads, repete o init do panel sem pulsar o RST, e pede todas as rows no próximo present. */
void st7789_resume(void);
/* [st7789_take_gram] Devolve 1 uma vez depois que [st7789_resume] refez o panel. */
int st7789_take_gram(void);
/* [st7789_rows_clear] Zera a máscara de rows. */
void st7789_rows_clear(void);
/* [st7789_rows_mark] Marca as rows [y0, y1). Fora da View é cortado. */
void st7789_rows_mark(int y0, int y1);
/* [st7789_rows_mark_all] Marca as 240 rows. */
void st7789_rows_mark_all(void);
/* [st7789_rows_count] Devolve quantas rows a máscara tem. */
int st7789_rows_count(void);
/* [st7789_pwr_down] Devolve 1 enquanto o PWR está pressionado. */
int st7789_pwr_down(void);
/* [st7789_pwr_pin] Devolve o GPIO do PWR. */
gpio_num_t st7789_pwr_pin(void);
/* [st7789_pwr_ready] Reaplica o PWR como entrada com pull-up depois do light sleep. */
void st7789_pwr_ready(void);

class st7789_drawer : public pixelroot32::graphics::BaseDrawSurface {
public:
    /* [st7789_drawer] Deixa o panel fechado. [st7789_drawer::init] usa as slices de [arena_bands] e [arena_pixels]. */
    st7789_drawer();
    /* [st7789_drawer::~st7789_drawer] Não libera o panel. O LCD fica ligado até o reset. */
    ~st7789_drawer() override;
    /* [st7789_drawer::operator delete] Não devolve o objeto à heap. A slice de [arena_drawer] é o dono. */
    static void operator delete(void *ptr) noexcept
    {
        (void)ptr;
    }

    /* [st7789_drawer::init] Sobe o SPI3 e o panel com as slices de [arena_bands] e [arena_pixels], zera os índices uma vez, depois [backlight_start]. */
    void init() override;
    /* [st7789_drawer::clearBuffer] Não mexe na palette nem nos índices. A cena restaura as rows sujas. */
    void clearBuffer() override;
    /* [st7789_drawer::sendBuffer] Expande as rows sujas no band ocioso e manda essa janela. Máscara vazia não chama SPI. */
    void sendBuffer() override;
    /* [st7789_drawer::drawPixel] Grava um índice da palette no pixel se a row está na máscara. */
    void drawPixel(int x, int y, uint16_t color) override;
    /* [st7789_drawer::drawFilledRectangle] Preenche um rectangle com um índice da palette. Uma linha cheia usa memset. Row limpa não muda. */
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
