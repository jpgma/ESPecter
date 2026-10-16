#include "engine_host.h"

#include "actions.h"
#include "arena.h"
#include "body_sheet.h"
#include "world.h"

#include "core/Engine.h"
#include "core/Scene.h"
#include "graphics/Color.h"
#include "graphics/DisplayConfig.h"
#include "graphics/FontManager.h"
#include "graphics/Renderer.h"

#include <cstdio>
#include <cstdlib>

#ifdef PLATFORM_NATIVE
#include <SDL2/SDL.h>
#else
#include "battery.h"
#include "pad_link.h"
#include "st7789_drawer.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

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
#ifdef PLATFORM_NATIVE
int g_prev_grave;
int g_prev_r;
#endif

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

/* [draw_body] Desenha as quatro layers 1bpp de [world_body_pose] com os pés e o centro no AABB. */
void draw_body(pixelroot32::graphics::Renderer &renderer, int cam_x, int cam_y)
{
    int x;
    int y;
    int w;
    int h;
    int clip;
    int frame;
    int face;
    int layer;
    const pixelroot32::graphics::Color colors[BODY_LAYER_COUNT] = {
        pixelroot32::graphics::Color::LightGreen,
        pixelroot32::graphics::Color::Cyan,
        pixelroot32::graphics::Color::Orange,
        pixelroot32::graphics::Color::Red,
    };

    world_rect(&x, &y, &w, &h);
    x -= (BODY_SHEET_W - w) / 2;
    y -= BODY_SHEET_H - h;
    if (!box_hits_view(cam_x, cam_y, x, y, BODY_SHEET_W, BODY_SHEET_H)) {
        return;
    }
    world_body_pose(&clip, &frame, &face);
    for (layer = 0; layer < BODY_LAYER_COUNT; layer++) {
        const uint16_t *rows;
        pixelroot32::graphics::Sprite sprite;

        rows = body_sheet_rows(clip, frame, layer);
        if (rows == nullptr) {
            continue;
        }
        sprite.data = rows;
        sprite.width = BODY_SHEET_W;
        sprite.height = BODY_SHEET_H;
        renderer.drawSprite(sprite, x, y, colors[layer], face < 0);
    }
}

/* [host_edges] Lê o reset e o debug uma vez por update, antes dos ticks. */
void host_edges(void)
{
#ifdef PLATFORM_NATIVE
    const Uint8 *keys;
    int grave;
    int reset;

    SDL_PumpEvents();
    keys = SDL_GetKeyboardState(nullptr);
    grave = keys[SDL_SCANCODE_GRAVE] != 0;
    reset = keys[SDL_SCANCODE_R] != 0;
    if (grave && !g_prev_grave) {
        g_debug = !g_debug;
    }
    if (reset && !g_prev_r) {
        world_reset();
    }
    g_prev_grave = grave;
    g_prev_r = reset;
#else
    if (pad_link_take_reset()) {
        world_reset();
    }
    if (pad_link_take_debug()) {
        g_debug = !g_debug;
    }
#endif
}

/* [sim_advance] Lê [host_edges] e gasta o delta em ticks de 16 ms de [world_tick], no máximo 4. */
void sim_advance(unsigned long delta_time)
{
    int steps;

    host_edges();
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
    /* [rect_scene::update] Avança com [sim_advance] e mede o present em [note_frame]. */
    void update(unsigned long delta_time) override
    {
        sim_advance(delta_time);
        note_frame(delta_time);
    }

    /* [rect_scene::draw] Pinta o ground, os sólidos e o corpo no offset da câmera. Com o debug, o wireframe das células, a carga no canto superior esquerdo no device e o FPS no canto superior direito. No device a cor do ground vem de [link_ground]. */
    void draw(pixelroot32::graphics::Renderer &renderer) override
    {
        int cam_x;
        int cam_y;
        int count;
        int i;

        world_camera(&cam_x, &cam_y);
        renderer.setOffsetBypass(true);
#ifdef PLATFORM_NATIVE
        renderer.drawFilledRectangle(0, 0, WORLD_VIEW, WORLD_VIEW, pixelroot32::graphics::Color::DarkGreen);
#else
        renderer.drawFilledRectangleW(0, 0, WORLD_VIEW, WORLD_VIEW, link_ground());
#endif
        renderer.setOffsetBypass(false);
        renderer.setDisplayOffset(-cam_x, -cam_y);
        count = world_solid_count();
        for (i = 0; i < count; i++) {
            int sx;
            int sy;
            int sw;
            int sh;

            world_solid_rect(i, &sx, &sy, &sw, &sh);
            if (clip_view(cam_x, cam_y, &sx, &sy, &sw, &sh)) {
                renderer.drawFilledRectangle(sx, sy, sw, sh, pixelroot32::graphics::Color::Black);
            }
        }
        draw_body(renderer, cam_x, cam_y);
        if (g_debug) {
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
        if (!g_debug) {
            return;
        }
#ifndef PLATFORM_NATIVE
        {
            const int pct = battery_percent();

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

} // namespace

/* [engine_host_run] Sobe a Scene depois de [world_init] e entra no laço do Engine. No device cada Engine::run pinta um frame e volta, então o host repete. */
extern "C" void engine_host_run(void)
{
    if (world_init() != 0) {
        std::fprintf(stderr, "world_init failed\n");
        std::abort();
    }

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
#else
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
#endif

    pixelroot32::core::Engine engine(config);
    engine.init();
    log_memory();
    engine.setScene(&g_scene);
#ifdef PLATFORM_NATIVE
    engine.run();
#else
    for (;;) {
        engine.run();
    }
#endif
}
