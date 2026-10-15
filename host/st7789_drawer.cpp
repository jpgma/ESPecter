#include "st7789_drawer.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <cstdlib>
#include <cstring>

namespace {

constexpr int k_width = 240;
constexpr int k_height = 240;
constexpr int k_band_rows = 16;
constexpr int k_band_pixels = k_width * k_band_rows;
constexpr gpio_num_t k_cs = GPIO_NUM_21;
constexpr gpio_num_t k_clk = GPIO_NUM_38;
constexpr gpio_num_t k_mosi = GPIO_NUM_39;
constexpr gpio_num_t k_dc = GPIO_NUM_45;
constexpr gpio_num_t k_rst = GPIO_NUM_40;
constexpr gpio_num_t k_bl = GPIO_NUM_46;
constexpr int k_pclk_hz = 80 * 1000 * 1000;

const char *k_tag = "st7789";

/* [spi_done_cb] Acorda a task quando o band SPI termina. */
bool spi_done_cb(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    auto *done = static_cast<SemaphoreHandle_t>(user_ctx);
    BaseType_t wake = pdFALSE;

    (void)panel_io;
    (void)edata;
    xSemaphoreGiveFromISR(done, &wake);
    return wake == pdTRUE;
}

} // namespace

namespace especter {

/* [st7789_drawer] Deixa o panel fechado. [st7789_drawer::init] aloca o framebuffer. */
st7789_drawer::st7789_drawer()
    : pixels(nullptr), band(nullptr), palette_count(1), ready(0), panel(nullptr), panel_io(nullptr), spi_done(nullptr)
{
    palette[0] = 0;
}

/* [st7789_drawer::~st7789_drawer] Não libera o panel. O LCD fica ligado até o reset. */
st7789_drawer::~st7789_drawer()
{
}

/* [st7789_drawer::index_for] Devolve o índice da cor na palette, criando entrada se couber. */
uint8_t st7789_drawer::index_for(uint16_t color)
{
    int i;

    for (i = 0; i < palette_count; i++) {
        if (palette[i] == color) {
            return static_cast<uint8_t>(i);
        }
    }
    if (palette_count < 16) {
        palette[palette_count] = color;
        palette_count++;
        return static_cast<uint8_t>(palette_count - 1);
    }
    return 0;
}

/* [st7789_drawer::init] Sobe o SPI3, o panel e o backlight. */
void st7789_drawer::init()
{
    spi_bus_config_t buscfg = {};
    esp_lcd_panel_io_spi_config_t io_cfg = {};
    esp_lcd_panel_dev_config_t panel_cfg = {};
    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_handle_t lcd = nullptr;
    SemaphoreHandle_t done;
    esp_lcd_panel_io_callbacks_t callbacks = {};

    if (ready) {
        return;
    }

    logicalWidth = k_width;
    logicalHeight = k_height;
    physicalWidth = k_width;
    physicalHeight = k_height;

    pixels = static_cast<uint8_t *>(heap_caps_malloc(k_width * k_height, MALLOC_CAP_INTERNAL));
    band = static_cast<uint16_t *>(heap_caps_malloc(k_band_pixels * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (pixels == nullptr || band == nullptr) {
        ESP_LOGE(k_tag, "framebuffer alloc failed");
        std::abort();
    }
    clearBuffer();

    buscfg.mosi_io_num = k_mosi;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = k_clk;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = k_band_pixels * static_cast<int>(sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));

    io_cfg.cs_gpio_num = k_cs;
    io_cfg.dc_gpio_num = k_dc;
    io_cfg.spi_mode = 3;
    io_cfg.pclk_hz = k_pclk_hz;
    io_cfg.trans_queue_depth = 10;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_cfg, &io));

    panel_cfg.reset_gpio_num = k_rst;
    panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
    panel_cfg.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &lcd));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(lcd, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd, true));

    done = xSemaphoreCreateBinary();
    callbacks.on_color_trans_done = spi_done_cb;
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io, &callbacks, done));

    gpio_set_direction(k_bl, GPIO_MODE_OUTPUT);
    gpio_set_level(k_bl, 1);

    panel = lcd;
    panel_io = io;
    spi_done = done;
    ready = 1;
}

/* [st7789_drawer::clearBuffer] Zera o framebuffer e a palette. */
void st7789_drawer::clearBuffer()
{
    if (pixels == nullptr) {
        return;
    }
    std::memset(pixels, 0, static_cast<size_t>(k_width * k_height));
    palette_count = 1;
    palette[0] = 0;
}

/* [st7789_drawer::drawPixel] Grava um índice da palette no pixel. */
void st7789_drawer::drawPixel(int x, int y, uint16_t color)
{
    if (pixels == nullptr || x < 0 || y < 0 || x >= logicalWidth || y >= logicalHeight) {
        return;
    }
    pixels[static_cast<size_t>(y * logicalWidth + x)] = index_for(color);
}

/* [st7789_drawer::drawFilledRectangle] Preenche um rectangle com um índice da palette. */
void st7789_drawer::drawFilledRectangle(int x, int y, int w, int h, uint16_t color)
{
    int x0;
    int y0;
    int x1;
    int y1;
    int row;
    int col;
    uint8_t index;

    if (pixels == nullptr || w <= 0 || h <= 0) {
        return;
    }
    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x + w;
    y1 = y + h;
    if (x1 > logicalWidth) {
        x1 = logicalWidth;
    }
    if (y1 > logicalHeight) {
        y1 = logicalHeight;
    }
    index = index_for(color);
    for (row = y0; row < y1; row++) {
        uint8_t *dst = pixels + static_cast<size_t>(row * logicalWidth);
        for (col = x0; col < x1; col++) {
            dst[col] = index;
        }
    }
}

/* [st7789_drawer::sendBuffer] Converte bands 8bpp em RGB565 e envia por DMA. */
void st7789_drawer::sendBuffer()
{
    auto *lcd = static_cast<esp_lcd_panel_handle_t>(panel);
    auto *done = static_cast<SemaphoreHandle_t>(spi_done);
    int y;

    if (!ready || pixels == nullptr || band == nullptr) {
        return;
    }
    for (y = 0; y < k_height; y += k_band_rows) {
        int rows = k_band_rows;
        int row;
        if (y + rows > k_height) {
            rows = k_height - y;
        }
        for (row = 0; row < rows; row++) {
            const uint8_t *src = pixels + static_cast<size_t>((y + row) * k_width);
            uint16_t *dst = band + row * k_width;
            int x;
            for (x = 0; x < k_width; x++) {
                dst[x] = palette[src[x]];
            }
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(lcd, 0, y, k_width, y + rows, band));
        if (xSemaphoreTake(done, pdMS_TO_TICKS(200)) != pdTRUE) {
            ESP_LOGW(k_tag, "SPI done timed out");
        }
    }
}

} // namespace especter
