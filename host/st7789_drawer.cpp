#include "st7789_drawer.h"

#include "arena.h"
#include "palette.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int k_width = 240;
constexpr int k_height = 240;
constexpr int k_band_rows = 120;
constexpr int k_band_pixels = k_width * k_band_rows;
/* O registrador de comprimento do DMA no S3 aceita no máximo 32768 bytes por transação. */
constexpr int k_dma_max = 32768;
constexpr gpio_num_t k_cs = GPIO_NUM_21;
constexpr gpio_num_t k_clk = GPIO_NUM_38;
constexpr gpio_num_t k_mosi = GPIO_NUM_39;
constexpr gpio_num_t k_dc = GPIO_NUM_45;
constexpr gpio_num_t k_rst = GPIO_NUM_40;
constexpr gpio_num_t k_bl = GPIO_NUM_46;
constexpr gpio_num_t k_pwr = GPIO_NUM_5;
constexpr int k_pclk_hz = 80 * 1000 * 1000;
/* Fade de 420 ms: a 5 kHz o hardware segura cada degrau por 2 ciclos e o vidro leva cerca de 410 ms. */
constexpr int k_bl_hz = 5000;
constexpr uint32_t k_bl_duty_max = 1023;
constexpr int k_bl_fade_ms = 420;
constexpr int k_bl_scan_ms = 20;

const char *k_tag = "st7789";

static_assert(static_cast<size_t>(k_band_pixels) * 2u * sizeof(uint16_t) == ARENA_BANDS_BYTES, "band bytes");
static_assert(static_cast<size_t>(k_width) * static_cast<size_t>(k_height) == ARENA_PIXELS_BYTES, "pixel bytes");
static_assert(sizeof(StaticTask_t) <= ARENA_TCB_BYTES, "tcb slice");
static_assert(alignof(StaticTask_t) <= 16u, "tcb align");
esp_lcd_panel_handle_t g_lcd;
int g_bl_on = 1;
int g_bl_armed;
int g_bl_down;
int g_bl_stable;
int g_bl_hold;
portMUX_TYPE g_bl_mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<int> g_lit{1};
std::atomic<int> g_want_on{1};
std::atomic<int> g_dark_pending{0};
TaskHandle_t g_present;

/* [notify_present] Acorda a task de [st7789_bind_present]. */
void notify_present(void)
{
    if (g_present != nullptr) {
        xTaskNotifyGive(g_present);
    }
}

/* [fade_end] Marca o vidro apagado quando o duty pedido chega a 0. */
bool fade_end(const ledc_cb_param_t *param, void *user_arg)
{
    (void)user_arg;
    if (param->duty == 0 && g_want_on.load(std::memory_order_acquire) == 0) {
        g_lit.store(0, std::memory_order_release);
        g_dark_pending.store(1, std::memory_order_release);
    }
    return false;
}

/* [backlight_fade] Leva o duty do LEDC até aceso ou apagado, no mesmo ritmo de um swing cheio. Aceso marca o vidro ligado e acorda o present. */
void backlight_fade(int on)
{
    const uint32_t target = on ? k_bl_duty_max : 0;
    uint32_t cur;
    uint32_t delta;
    int ms;

    g_want_on.store(on ? 1 : 0, std::memory_order_release);
    if (on) {
        g_lit.store(1, std::memory_order_release);
        g_dark_pending.store(0, std::memory_order_release);
        notify_present();
    }
    ESP_ERROR_CHECK(ledc_fade_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    cur = ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    delta = cur > target ? cur - target : target - cur;
    if (delta == 0) {
        return;
    }
    ms = static_cast<int>((delta * static_cast<uint32_t>(k_bl_fade_ms) + k_bl_duty_max - 1) / k_bl_duty_max);
    ESP_ERROR_CHECK(ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, target, ms));
    ESP_ERROR_CHECK(ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT));
}

/* [backlight_scan] Debounce do PWR e chama [backlight_fade] no press. Com [st7789_hold_scan] não troca o fade. */
void backlight_scan(void)
{
    int pressed;
    int on = 0;
    int edge = 0;

    portENTER_CRITICAL(&g_bl_mux);
    if (g_bl_hold) {
        portEXIT_CRITICAL(&g_bl_mux);
        return;
    }
    pressed = gpio_get_level(k_pwr) == 0;
    if (!g_bl_armed) {
        if (!pressed) {
            g_bl_armed = 1;
        }
        portEXIT_CRITICAL(&g_bl_mux);
        return;
    }
    if (!pressed) {
        g_bl_stable = 0;
        g_bl_down = 0;
        portEXIT_CRITICAL(&g_bl_mux);
        return;
    }
    if (g_bl_stable < 2) {
        g_bl_stable++;
    }
    if (g_bl_stable < 2 || g_bl_down) {
        portEXIT_CRITICAL(&g_bl_mux);
        return;
    }
    g_bl_down = 1;
    g_bl_on = !g_bl_on;
    on = g_bl_on;
    edge = 1;
    portEXIT_CRITICAL(&g_bl_mux);
    if (edge) {
        backlight_fade(on);
        ESP_LOGI(k_tag, "backlight %s", on ? "on" : "off");
    }
}

/* [backlight_task] Chama [backlight_scan] a cada 20 ms. */
void backlight_task(void *arg)
{
    (void)arg;
    for (;;) {
        backlight_scan();
        vTaskDelay(pdMS_TO_TICKS(k_bl_scan_ms));
    }
}

/* [panel_pins_hold] Segura ou solta CS, CLK, MOSI, DC e RST. Segurar também tira o sleep sel desses pads. */
void panel_pins_hold(int hold)
{
    const gpio_num_t pins[] = {k_cs, k_clk, k_mosi, k_dc, k_rst};
    int i;

    for (i = 0; i < static_cast<int>(sizeof pins / sizeof pins[0]); i++) {
        if (hold) {
            gpio_sleep_sel_dis(pins[i]);
            ESP_ERROR_CHECK(gpio_hold_en(pins[i]));
        } else {
            ESP_ERROR_CHECK(gpio_hold_dis(pins[i]));
        }
    }
}

/* [backlight_start] Configura o LEDC no GPIO46, registra [fade_end], acende com [backlight_fade] e sobe [backlight_task] na slice de [arena_stack_backlight]. */
void backlight_start(void)
{
    ledc_timer_config_t timer = {};
    ledc_channel_config_t channel = {};
    gpio_config_t key = {};

    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = k_bl_hz;
    timer.clk_cfg = LEDC_AUTO_CLK;
    channel.gpio_num = k_bl;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.timer_sel = LEDC_TIMER_0;
    channel.duty = 0;
    channel.hpoint = 0;
    key.pin_bit_mask = 1ull << k_pwr;
    key.mode = GPIO_MODE_INPUT;
    key.pull_up_en = GPIO_PULLUP_ENABLE;

    ledc_cbs_t cbs = {};

    cbs.fade_cb = fade_end;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    ESP_ERROR_CHECK(ledc_fade_func_install(0));
    ESP_ERROR_CHECK(ledc_cb_register(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, &cbs, nullptr));
    backlight_fade(1);
    ESP_ERROR_CHECK(gpio_config(&key));
    {
        arena_slice stack = arena_stack_backlight();
        arena_slice tcb = arena_tcb_backlight();

        if (stack.data == nullptr || tcb.data == nullptr
            || xTaskCreateStatic(backlight_task, "bl", static_cast<uint32_t>(stack.size), nullptr, 1,
                                 reinterpret_cast<StackType_t *>(stack.data),
                                 reinterpret_cast<StaticTask_t *>(tcb.data)) == nullptr) {
            ESP_LOGE(k_tag, "backlight task failed");
        }
    }
}

/* [spi_wait] Espera um DMA de [st7789_drawer::sendBuffer] terminar. Devolve 1 se chegou. */
int spi_wait(SemaphoreHandle_t done)
{
    if (xSemaphoreTake(done, pdMS_TO_TICKS(200)) == pdTRUE) {
        return 1;
    }
    ESP_LOGW(k_tag, "SPI done timed out");
    return 0;
}
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

/* [st7789_lit] Devolve 1 enquanto o fade pedido está aceso ou o duty ainda não chegou a 0. */
int st7789_lit(void)
{
    return g_lit.load(std::memory_order_acquire);
}

/* [st7789_take_dark] Devolve 1 uma vez quando o duty chega a 0. */
int st7789_take_dark(void)
{
    return g_dark_pending.exchange(0, std::memory_order_acq_rel);
}

/* [st7789_bind_present] Guarda a task que [st7789_fade_on] acorda. */
void st7789_bind_present(TaskHandle_t task)
{
    g_present = task;
}

/* [st7789_hold_scan] Enquanto ligado, [backlight_scan] não troca o fade. */
void st7789_hold_scan(int hold)
{
    portENTER_CRITICAL(&g_bl_mux);
    g_bl_hold = hold ? 1 : 0;
    portEXIT_CRITICAL(&g_bl_mux);
}

/* [st7789_fade_on] Acende o backlight e engole o press do PWR que ainda está baixo. */
void st7789_fade_on(void)
{
    portENTER_CRITICAL(&g_bl_mux);
    g_bl_on = 1;
    g_bl_down = 1;
    g_bl_stable = 2;
    g_bl_hold = 0;
    portEXIT_CRITICAL(&g_bl_mux);
    backlight_fade(1);
    ESP_LOGI(k_tag, "backlight on");
}

/* [st7789_pwr_down] Devolve 1 enquanto o PWR está pressionado. */
int st7789_pwr_down(void)
{
    return gpio_get_level(k_pwr) == 0;
}

/* [st7789_pwr_pin] Devolve o GPIO do PWR. */
gpio_num_t st7789_pwr_pin(void)
{
    return k_pwr;
}

/* [st7789_prepare_sleep] Deixa o RST alto e chama [panel_pins_hold] para o light sleep não flutuar o ST7789. */
void st7789_prepare_sleep(void)
{
    gpio_set_level(k_rst, 1);
    panel_pins_hold(1);
}

/* [st7789_resume] Solta os pads com [panel_pins_hold] e repete o init, o invert e o DISPON. Não pulsa o RST. */
void st7789_resume(void)
{
    panel_pins_hold(0);
    if (g_lcd == nullptr) {
        return;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_lcd));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(g_lcd, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(g_lcd, true));
    ESP_LOGI(k_tag, "panel on");
}

/* [st7789_pwr_ready] Reaplica o PWR como entrada com pull-up depois do light sleep. */
void st7789_pwr_ready(void)
{
    gpio_config_t key = {};

    key.pin_bit_mask = 1ull << k_pwr;
    key.mode = GPIO_MODE_INPUT;
    key.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&key));
}

/* [st7789_drawer] Deixa o panel fechado. [st7789_drawer::init] usa as slices de [arena_bands] e [arena_pixels]. */
st7789_drawer::st7789_drawer()
    : pixels(nullptr), band(nullptr), ready(0), panel(nullptr), panel_io(nullptr), spi_done(nullptr)
{
}

/* [st7789_drawer::~st7789_drawer] Não libera o panel. O LCD fica ligado até o reset. */
st7789_drawer::~st7789_drawer()
{
}

/* [st7789_drawer::index_for] Devolve o índice dessa RGB565 em palette_rgb565. Se não achar, devolve 0. */
uint8_t st7789_drawer::index_for(uint16_t color)
{
    int i;

    for (i = 0; i < PALETTE_COUNT; i++) {
        if (palette_rgb565[i] == color) {
            return static_cast<uint8_t>(i);
        }
    }
    return 0;
}

/* [st7789_drawer::init] Sobe o SPI3 e o panel com as slices de [arena_bands] e [arena_pixels], zera os índices uma vez, depois [backlight_start]. */
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

    arena_slice pixel_slice = arena_pixels();
    arena_slice band_slice = arena_bands();

    if (pixel_slice.data == nullptr || band_slice.data == nullptr || pixel_slice.size < ARENA_PIXELS_BYTES
        || band_slice.size < ARENA_BANDS_BYTES) {
        ESP_LOGE(k_tag, "framebuffer alloc failed");
        std::abort();
    }
    pixels = pixel_slice.data;
    band = reinterpret_cast<uint16_t *>(band_slice.data);
    std::memset(pixels, 0, static_cast<size_t>(k_width * k_height));
    clearBuffer();

    buscfg.mosi_io_num = k_mosi;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = k_clk;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = k_dma_max;
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

    done = xSemaphoreCreateCounting(2, 0);
    callbacks.on_color_trans_done = spi_done_cb;
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io, &callbacks, done));

    backlight_start();

    g_lcd = lcd;
    panel = lcd;
    panel_io = io;
    spi_done = done;
    ready = 1;
}

/* [st7789_drawer::clearBuffer] Não mexe na palette nem nos índices. A cena cobre a View. */
void st7789_drawer::clearBuffer()
{
}

/* [st7789_drawer::drawPixel] Grava um índice da palette no pixel. */
void st7789_drawer::drawPixel(int x, int y, uint16_t color)
{
    if (pixels == nullptr || x < 0 || y < 0 || x >= logicalWidth || y >= logicalHeight) {
        return;
    }
    pixels[static_cast<size_t>(y * logicalWidth + x)] = index_for(color);
}

/* [st7789_drawer::drawFilledRectangle] Preenche um rectangle com um índice da palette. Uma linha cheia usa memset. */
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
    if (x0 == 0 && x1 == logicalWidth) {
        for (row = y0; row < y1; row++) {
            std::memset(pixels + static_cast<size_t>(row * logicalWidth), index, static_cast<size_t>(logicalWidth));
        }
        return;
    }
    for (row = y0; row < y1; row++) {
        uint8_t *dst = pixels + static_cast<size_t>(row * logicalWidth);
        for (col = x0; col < x1; col++) {
            dst[col] = index;
        }
    }
}

/* [st7789_drawer::sendBuffer] Converte o band ocioso enquanto o outro segue no DMA. */
void st7789_drawer::sendBuffer()
{
    auto *lcd = static_cast<esp_lcd_panel_handle_t>(panel);
    auto *done = static_cast<SemaphoreHandle_t>(spi_done);
    int y;
    int slot;
    int pending;

    if (!ready || pixels == nullptr || band == nullptr) {
        return;
    }
    slot = 0;
    pending = 0;
    for (y = 0; y < k_height; y += k_band_rows) {
        int rows = k_band_rows;
        int row;
        uint16_t *dst_band;

        if (pending == 2) {
            spi_wait(done);
            pending -= 1;
        }
        if (y + rows > k_height) {
            rows = k_height - y;
        }
        dst_band = band + slot * k_band_pixels;
        for (row = 0; row < rows; row++) {
            const uint8_t *src = pixels + static_cast<size_t>((y + row) * k_width);
            uint16_t *dst = dst_band + row * k_width;
            int x;

            for (x = 0; x < k_width; x++) {
                dst[x] = palette_rgb565[src[x]];
            }
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(lcd, 0, y, k_width, y + rows, dst_band));
        pending += 1;
        slot ^= 1;
    }
    while (pending > 0) {
        spi_wait(done);
        pending -= 1;
    }
}

} // namespace especter
