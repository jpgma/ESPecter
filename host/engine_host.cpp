#include "engine_host.h"

#include "actions.h"
#include "arena.h"
#include "body_sheet.h"
#include "palette.h"
#include "world.h"

#include "core/Engine.h"
#include "core/Scene.h"
#include "graphics/Color.h"
#include "graphics/DisplayConfig.h"
#include "graphics/FontManager.h"
#include "graphics/Renderer.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef PLATFORM_NATIVE
#include <SDL2/SDL.h>
#else
#include "battery.h"
#include "pad_link.h"
#include "st7789_drawer.h"

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdint>
#endif

namespace {

#ifndef PLATFORM_NATIVE
/* [rgb565] Empacota r, g e b em RGB565. */
uint16_t rgb565(int r, int g, int b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* [link_ground] Devolve a cor do ground segundo o link de [pad_link_read]. */
uint16_t link_ground(void)
{
    pad_view view;

    pad_link_read(&view);
    if (view.link == PAD_LINK_LINKED) {
        return rgb565(0, 140, 32);
    }
    if (view.link == PAD_LINK_SCANNING) {
        return rgb565(220, 180, 0);
    }
    return rgb565(180, 0, 0);
}
#endif

#ifdef PLATFORM_NATIVE
int g_pc_named;

/* [pc_window_bind] Põe o título ESPecter na window SDL, uma vez. */
void pc_window_bind(void)
{
    SDL_Window *window;

    if (g_pc_named) {
        return;
    }
    window = SDL_GetKeyboardFocus();
    if (window == nullptr) {
        window = SDL_GetMouseFocus();
    }
    if (window != nullptr) {
        SDL_SetWindowTitle(window, "ESPecter");
        g_pc_named = 1;
    }
}
#endif

unsigned long g_sim_ms;
int g_fps = -1;
unsigned long g_fps_ms;
int g_fps_frames;
int g_debug;
const uint16_t g_hazard_rgb = (uint16_t)((31u << 11) | 31u);
#ifdef PLATFORM_NATIVE
int g_prev_grave;
int g_prev_r;
int g_prev_pwr;
int g_prev_plus;
int g_prev_boot;
int g_pc_lit = 1;
unsigned long g_boot_wait;
unsigned long g_dark_ms;
#endif

struct view_rect {
    int x;
    int y;
    int w;
    int h;
};

struct draw_view {
    int cam_x;
    int cam_y;
    int body_x;
    int body_y;
    int body_w;
    int body_h;
    int clip;
    int frame;
    int face;
    int solid_count;
    view_rect solids[WORLD_SOLID_CAP];
    view_rect spike;
    view_rect checkpoint;
    view_rect exit_box;
    int debug;
    uint16_t ground;
    int battery_pct;
};

struct view_slot {
    std::atomic<uint32_t> seq;
    draw_view view;
};

view_slot g_slot;
draw_view g_shown;

static_assert(sizeof(view_slot) + sizeof(draw_view) == 8396, "draw snapshot size");

/* [floor_div] Divide a por b para baixo. b é positivo. */
int floor_div(int a, int b)
{
    int q;

    q = a / b;
    if (a < 0 && a % b != 0) {
        q -= 1;
    }
    return q;
}

/* [clip_view] Recorta o retângulo do mundo à View da câmera. Devolve 0 se a área some. */
int clip_view(int cam_x, int cam_y, int *x, int *y, int *w, int *h)
{
    int x0;
    int y0;
    int x1;
    int y1;

    x0 = *x;
    y0 = *y;
    x1 = *x + *w;
    y1 = *y + *h;
    if (x0 < cam_x) {
        x0 = cam_x;
    }
    if (y0 < cam_y) {
        y0 = cam_y;
    }
    if (x1 > cam_x + WORLD_VIEW) {
        x1 = cam_x + WORLD_VIEW;
    }
    if (y1 > cam_y + WORLD_VIEW) {
        y1 = cam_y + WORLD_VIEW;
    }
    if (x1 <= x0 || y1 <= y0) {
        return 0;
    }
    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
    return 1;
}

/* [box_hits_view] Devolve 1 se o Rect cruza a View. Não altera a origem. */
int box_hits_view(int cam_x, int cam_y, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        return 0;
    }
    return x < cam_x + WORLD_VIEW && cam_x < x + w && y < cam_y + WORLD_VIEW && cam_y < y + h;
}

/* [draw_world_fill] Pinta o AABB de mundo se [clip_view] deixar área. */
void draw_world_fill(pixelroot32::graphics::Renderer &renderer, int cam_x, int cam_y, int x, int y, int w, int h,
                     uint16_t color)
{
    if (clip_view(cam_x, cam_y, &x, &y, &w, &h)) {
        renderer.drawFilledRectangleW(x, y, w, h, color);
    }
}

/* [draw_world_outline] Traça o AABB de mundo em branco se ele cruza a View. */
void draw_world_outline(pixelroot32::graphics::Renderer &renderer, int cam_x, int cam_y, int x, int y, int w, int h)
{
    if (box_hits_view(cam_x, cam_y, x, y, w, h)) {
        renderer.drawRectangle(x, y, w, h, pixelroot32::graphics::Color::White);
    }
}

/* [draw_body] Desenha o quadro 8bpp da view estável com os pés e o centro no AABB. Índice 0 não pinta. */
void draw_body(pixelroot32::graphics::Renderer &renderer, int cam_x, int cam_y)
{
    int x;
    int y;
    int w;
    int h;
    int clip;
    int frame;
    int face;
    int row;
    int col;
    int ox;
    int oy;
    const uint8_t *px;

    x = g_shown.body_x;
    y = g_shown.body_y;
    w = g_shown.body_w;
    h = g_shown.body_h;
    x -= (BODY_SHEET_W - w) / 2;
    y -= BODY_SHEET_H - h;
    if (!box_hits_view(cam_x, cam_y, x, y, BODY_SHEET_W, BODY_SHEET_H)) {
        return;
    }
    clip = g_shown.clip;
    frame = g_shown.frame;
    face = g_shown.face;
    px = body_sheet_frame(clip, frame);
    if (px == nullptr) {
        return;
    }
    ox = 0;
    oy = 0;
    if (!renderer.isOffsetBypassEnabled()) {
        ox = renderer.getXOffset();
        oy = renderer.getYOffset();
    }
    pixelroot32::graphics::DrawSurface &surface = renderer.getDrawSurface();
    for (row = 0; row < BODY_SHEET_H; row++) {
        for (col = 0; col < BODY_SHEET_W; col++) {
            int sx;
            int sy;
            uint8_t index;

            index = px[row * BODY_SHEET_W + col];
            if (index == 0) {
                continue;
            }
            sx = face < 0 ? x + (BODY_SHEET_W - 1 - col) : x + col;
            sy = y + row;
            sx += ox;
            sy += oy;
            if (sx < 0 || sy < 0 || sx >= WORLD_VIEW || sy >= WORLD_VIEW) {
                continue;
            }
            surface.drawPixel(sx, sy, palette_rgb565[index]);
        }
    }
}

/* [host_edges] Lê o reset e o debug uma vez por update, antes dos ticks. No PC, 1 é o PWR, 2 é o PLUS e 3 é o BOOT. */
void host_edges(unsigned long delta_time)
{
#ifdef PLATFORM_NATIVE
    const Uint8 *keys;
    int grave;
    int reset;
    int pwr;
    int plus;
    int boot;

    SDL_PumpEvents();
    keys = SDL_GetKeyboardState(nullptr);
    grave = keys[SDL_SCANCODE_GRAVE] != 0;
    reset = keys[SDL_SCANCODE_R] != 0;
    pwr = keys[SDL_SCANCODE_1] != 0;
    plus = keys[SDL_SCANCODE_2] != 0;
    boot = keys[SDL_SCANCODE_3] != 0;
    if (grave && !g_prev_grave) {
        g_debug = !g_debug;
    }
    if (reset && !g_prev_r) {
        world_reset();
    }
    if (plus && !g_prev_plus) {
        g_debug = !g_debug;
    }
    if (pwr && !g_prev_pwr) {
        g_pc_lit = !g_pc_lit;
        g_dark_ms = 0;
    }
    if (boot && !g_prev_boot) {
        if (g_boot_wait > 0) {
            g_boot_wait = 0;
        } else {
            g_boot_wait = 400;
        }
    } else if (g_boot_wait > 0) {
        if (delta_time >= g_boot_wait) {
            g_boot_wait = 0;
            world_reset();
        } else {
            g_boot_wait -= delta_time;
        }
    }
    g_prev_grave = grave;
    g_prev_r = reset;
    g_prev_pwr = pwr;
    g_prev_plus = plus;
    g_prev_boot = boot;
#else
    (void)delta_time;
    if (pad_link_take_reset()) {
        world_reset();
    }
    if (pad_link_take_debug()) {
        g_debug = !g_debug;
    }
#endif
}

/* [pc_sim_held] No PC, devolve 1 quando o PWR ficou apagado por 30 s. O device dorme de outro jeito. */
int pc_sim_held(unsigned long delta_time)
{
#ifdef PLATFORM_NATIVE
    if (g_pc_lit) {
        g_dark_ms = 0;
        return 0;
    }
    g_dark_ms += delta_time;
    return g_dark_ms >= 30000ul;
#else
    (void)delta_time;
    return 0;
#endif
}

/* [publish_view] Copia a view do mundo para o slot. O seq ímpar marca a escrita. */
void publish_view(void)
{
    int count;
    int i;
    uint32_t seq;

    seq = g_slot.seq.load(std::memory_order_relaxed);
    g_slot.seq.store(seq + 1u, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    world_camera(&g_slot.view.cam_x, &g_slot.view.cam_y);
    world_rect(&g_slot.view.body_x, &g_slot.view.body_y, &g_slot.view.body_w, &g_slot.view.body_h);
    world_body_pose(&g_slot.view.clip, &g_slot.view.frame, &g_slot.view.face);
    count = world_solid_count();
    if (count < 0) {
        count = 0;
    }
    if (count > WORLD_SOLID_CAP) {
        count = WORLD_SOLID_CAP;
    }
    g_slot.view.solid_count = count;
    for (i = 0; i < count; i++) {
        world_solid_rect(i, &g_slot.view.solids[i].x, &g_slot.view.solids[i].y, &g_slot.view.solids[i].w,
                         &g_slot.view.solids[i].h);
    }
    world_spike_rect(&g_slot.view.spike.x, &g_slot.view.spike.y, &g_slot.view.spike.w, &g_slot.view.spike.h);
    world_checkpoint_rect(&g_slot.view.checkpoint.x, &g_slot.view.checkpoint.y, &g_slot.view.checkpoint.w,
                          &g_slot.view.checkpoint.h);
    world_exit_rect(&g_slot.view.exit_box.x, &g_slot.view.exit_box.y, &g_slot.view.exit_box.w, &g_slot.view.exit_box.h);
    g_slot.view.debug = g_debug;
#ifndef PLATFORM_NATIVE
    g_slot.view.ground = link_ground();
    g_slot.view.battery_pct = battery_percent();
#else
    g_slot.view.ground = 0;
    g_slot.view.battery_pct = -1;
#endif
    std::atomic_thread_fence(std::memory_order_release);
    g_slot.seq.store(seq + 2u, std::memory_order_relaxed);
}

/* [load_view] Copia o slot estável para a view do present. Repete se o seq mudar. */
void load_view(void)
{
    for (;;) {
        uint32_t seq;

        seq = g_slot.seq.load(std::memory_order_relaxed);
        if (seq & 1u) {
            continue;
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        std::memcpy(&g_shown, &g_slot.view, sizeof g_shown);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq == g_slot.seq.load(std::memory_order_relaxed)) {
            return;
        }
    }
}

/* [sim_advance] Lê [host_edges], gasta o delta em ticks de 16 ms de [world_tick], no máximo 4, e chama [publish_view]. No PC, [pc_sim_held] segura os ticks. */
void sim_advance(unsigned long delta_time)
{
    int steps;

    host_edges(delta_time);
    if (pc_sim_held(delta_time)) {
        publish_view();
        return;
    }
    g_sim_ms += delta_time;
    steps = 0;
    while (g_sim_ms >= 16 && steps < 4) {
        actions input;

        actions_capture(&input);
        world_tick(&input);
        g_sim_ms -= 16;
        steps += 1;
    }
    if (steps == 4 && g_sim_ms >= 16) {
        g_sim_ms = 0;
    }
    publish_view();
}

/* [note_frame] Acumula o present e atualiza o FPS a cada meio segundo. */
void note_frame(unsigned long delta_time)
{
#ifdef PLATFORM_NATIVE
    pc_window_bind();
#endif
    g_fps_ms += delta_time;
    g_fps_frames += 1;
    if (g_fps_ms < 500) {
        return;
    }
    g_fps = (int)((g_fps_frames * 1000ul) / g_fps_ms);
    g_fps_ms = 0;
    g_fps_frames = 0;
}

class rect_scene : public pixelroot32::core::Scene {
public:
    /* [rect_scene::update] No PC avança com [sim_advance] e mede o present em [note_frame]. No device o sim não passa por aqui. */
    void update(unsigned long delta_time) override
    {
#ifdef PLATFORM_NATIVE
        sim_advance(delta_time);
        if (g_pc_lit) {
            note_frame(delta_time);
        }
#else
        (void)delta_time;
#endif
    }

    /* [rect_scene::draw] Pinta a view de [load_view]: ground, sólidos, spike, checkpoint, saída e corpo no offset da câmera. Com o debug, o wireframe das células, a carga no canto superior esquerdo no device e o FPS no canto superior direito. No PC, com o PWR apagado, pinta preto e volta. */
    void draw(pixelroot32::graphics::Renderer &renderer) override
    {
        int cam_x;
        int cam_y;
        int count;
        int i;
        int mx;
        int my;
        int mw;
        int mh;

        load_view();
#ifdef PLATFORM_NATIVE
        if (!g_pc_lit) {
            renderer.setOffsetBypass(true);
            renderer.drawFilledRectangle(0, 0, WORLD_VIEW, WORLD_VIEW, pixelroot32::graphics::Color::Black);
            return;
        }
#endif
        cam_x = g_shown.cam_x;
        cam_y = g_shown.cam_y;
        renderer.setOffsetBypass(true);
#ifdef PLATFORM_NATIVE
        renderer.drawFilledRectangle(0, 0, WORLD_VIEW, WORLD_VIEW, pixelroot32::graphics::Color::DarkGreen);
#else
        renderer.drawFilledRectangleW(0, 0, WORLD_VIEW, WORLD_VIEW, g_shown.ground);
#endif
        renderer.setOffsetBypass(false);
        renderer.setDisplayOffset(-cam_x, -cam_y);
        count = g_shown.solid_count;
        for (i = 0; i < count; i++) {
            int sx;
            int sy;
            int sw;
            int sh;

            sx = g_shown.solids[i].x;
            sy = g_shown.solids[i].y;
            sw = g_shown.solids[i].w;
            sh = g_shown.solids[i].h;
            if (clip_view(cam_x, cam_y, &sx, &sy, &sw, &sh)) {
                renderer.drawFilledRectangle(sx, sy, sw, sh, pixelroot32::graphics::Color::Black);
            }
        }
        mx = g_shown.spike.x;
        my = g_shown.spike.y;
        mw = g_shown.spike.w;
        mh = g_shown.spike.h;
        draw_world_fill(renderer, cam_x, cam_y, mx, my, mw, mh, g_hazard_rgb);
        mx = g_shown.checkpoint.x;
        my = g_shown.checkpoint.y;
        mw = g_shown.checkpoint.w;
        mh = g_shown.checkpoint.h;
        draw_world_outline(renderer, cam_x, cam_y, mx, my, mw, mh);
        mx = g_shown.exit_box.x;
        my = g_shown.exit_box.y;
        mw = g_shown.exit_box.w;
        mh = g_shown.exit_box.h;
        draw_world_outline(renderer, cam_x, cam_y, mx, my, mw, mh);
        draw_body(renderer, cam_x, cam_y);
        if (g_shown.debug) {
            int x0;
            int y0;
            int x1;
            int y1;
            int cx;
            int cy;

            x0 = floor_div(cam_x, WORLD_VIEW);
            x1 = floor_div(cam_x + WORLD_VIEW - 1, WORLD_VIEW);
            y0 = floor_div(cam_y, WORLD_VIEW);
            y1 = floor_div(cam_y + WORLD_VIEW - 1, WORLD_VIEW);
            for (cy = y0; cy <= y1; cy++) {
                for (cx = x0; cx <= x1; cx++) {
                    renderer.drawRectangle(cx * WORLD_VIEW, cy * WORLD_VIEW, WORLD_VIEW, WORLD_VIEW,
                                           pixelroot32::graphics::Color::White);
                }
            }
        }
        renderer.setOffsetBypass(true);
        if (!g_shown.debug) {
            return;
        }
#ifndef PLATFORM_NATIVE
        {
            const int pct = g_shown.battery_pct;

            if (pct >= 0) {
                char label[16];

                std::snprintf(label, sizeof label, "%d%%", pct);
                renderer.drawText(label, 1, 0, pixelroot32::graphics::Color::White, 1);
            }
        }
#endif
        if (g_fps >= 0) {
            char label[16];
            int text_w;
            int text_x;

            std::snprintf(label, sizeof label, "%d FPS", g_fps);
            text_w = pixelroot32::graphics::FontManager::textWidth(nullptr, label, 1);
            text_x = WORLD_VIEW - text_w - 1;
            if (text_x < 0) {
                text_x = 0;
            }
            renderer.drawText(label, text_x, 0, pixelroot32::graphics::Color::White, 1);
        }
    }
};

rect_scene g_scene;

/* [log_memory] Imprime [arena_capacity] e [arena_offset] e, no device, o heap livre. */
void log_memory(void)
{
#ifdef PLATFORM_NATIVE
    std::printf("arena capacity %u offset %u\n",
                (unsigned)arena_capacity(),
                (unsigned)arena_offset());
#else
    ESP_LOGI("especter", "arena capacity %u offset %u",
             (unsigned)arena_capacity(),
             (unsigned)arena_offset());
    ESP_LOGI("especter", "internal free %u largest %u psram free %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
}

#ifndef PLATFORM_NATIVE
constexpr uint32_t k_sim_stack = 3072;
constexpr uint32_t k_present_stack = 4096;
constexpr UBaseType_t k_task_prio = 2;
constexpr uint64_t k_dark_us = 30000000ULL;

TaskHandle_t g_sim;
std::atomic<int> g_sleep_due{0};
esp_timer_handle_t g_dark_timer;
int g_was_lit = 1;

/* [dark_timer_cb] Pede o light sleep 30 s depois do duty 0. */
void dark_timer_cb(void *arg)
{
    (void)arg;
    g_sleep_due.store(1, std::memory_order_release);
    if (g_sim != nullptr) {
        xTaskNotifyGive(g_sim);
    }
}

/* [host_sleep] Espera o PWR solto, para o central, segura os pads e entra em light sleep até o GPIO5 baixar. Na volta [st7789_resume] refaz o init e acende o backlight. */
void host_sleep(void)
{
    while (especter::st7789_pwr_down()) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (especter::st7789_lit()) {
        return;
    }
    especter::st7789_hold_scan(1);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (esp_sleep_enable_ext0_wakeup(especter::st7789_pwr_pin(), 0) != ESP_OK) {
        ESP_LOGW("especter", "sleep skipped");
        especter::st7789_hold_scan(0);
        especter::st7789_fade_on();
        return;
    }
    if (pad_link_pause() != 0) {
        ESP_LOGW("especter", "sleep skipped");
        especter::st7789_hold_scan(0);
        especter::st7789_fade_on();
        return;
    }
    gpio_hold_en(GPIO_NUM_2);
    especter::st7789_prepare_sleep();
    esp_light_sleep_start();
    especter::st7789_resume();
    gpio_hold_dis(GPIO_NUM_2);
    especter::st7789_pwr_ready();
    pad_link_resume();
    especter::st7789_fade_on();
}

/* [sim_task] No core 0, gasta o millis em [sim_advance]. Com o duty em 0, arma os 30 s e então [host_sleep]. */
void sim_task(void *arg)
{
    unsigned long prev;

    (void)arg;
    g_sim = xTaskGetCurrentTaskHandle();
    prev = millis();
    for (;;) {
        unsigned long now;
        unsigned long delta;
        int lit;

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
        lit = especter::st7789_lit();
        if (lit) {
            if (!g_was_lit) {
                esp_timer_stop(g_dark_timer);
            }
        } else if (especter::st7789_take_dark()) {
            esp_timer_stop(g_dark_timer);
            esp_timer_start_once(g_dark_timer, k_dark_us);
        }
        g_was_lit = lit;
        if (g_sleep_due.exchange(0, std::memory_order_acq_rel) != 0 && !especter::st7789_lit()) {
            host_sleep();
            g_sim_ms = 0;
            prev = millis();
            g_was_lit = especter::st7789_lit();
            continue;
        }
        now = millis();
        delta = now - prev;
        prev = now;
        if (delta > 0) {
            sim_advance(delta);
        }
    }
}

/* [present_task] No core 1, constrói o Engine e chama Engine::run enquanto [st7789_lit] estiver aceso. */
void present_task(void *arg)
{
    unsigned long prev;
    auto *drawer = new especter::st7789_drawer();
    pixelroot32::graphics::DisplayConfig config(
        pixelroot32::graphics::DisplayType::CUSTOM,
        0,
        WORLD_VIEW,
        WORLD_VIEW,
        WORLD_VIEW,
        WORLD_VIEW,
        0,
        0,
        drawer);
    pixelroot32::core::Engine engine(config);

    (void)arg;
    especter::st7789_bind_present(xTaskGetCurrentTaskHandle());
    engine.init();
    log_memory();
    engine.setScene(&g_scene);
    prev = millis();
    for (;;) {
        unsigned long now;
        unsigned long delta;

        if (!especter::st7789_lit()) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            prev = millis();
            continue;
        }
        now = millis();
        delta = now - prev;
        prev = now;
        if (delta > 0) {
            note_frame(delta);
        }
        engine.run();
    }
}
#endif

} // namespace

/* [engine_host_run] Sobe a Scene depois de [world_init]. No PC entra no laço do Engine. No device publica a view e prende o sim no core 0 e o present no core 1. */
extern "C" void engine_host_run(void)
{
    if (world_init() != 0) {
        std::fprintf(stderr, "world_init failed\n");
        std::abort();
    }
    publish_view();

#ifdef PLATFORM_NATIVE
    pixelroot32::graphics::DisplayConfig config(
        pixelroot32::graphics::DisplayType::NONE,
        0,
        WORLD_VIEW,
        WORLD_VIEW,
        WORLD_VIEW,
        WORLD_VIEW,
        0,
        0);
    pixelroot32::core::Engine engine(config);

    engine.init();
    log_memory();
    engine.setScene(&g_scene);
    engine.run();
#else
    esp_timer_create_args_t dark_args = {};

    dark_args.callback = dark_timer_cb;
    dark_args.name = "dark";
    ESP_ERROR_CHECK(esp_timer_create(&dark_args, &g_dark_timer));
    if (xTaskCreatePinnedToCore(sim_task, "sim", k_sim_stack, nullptr, k_task_prio, &g_sim, 0) != pdPASS) {
        ESP_LOGE("especter", "sim task failed");
        std::abort();
    }
    if (xTaskCreatePinnedToCore(present_task, "present", k_present_stack, nullptr, k_task_prio, nullptr, 1) != pdPASS) {
        ESP_LOGE("especter", "present task failed");
        std::abort();
    }
#endif
}
