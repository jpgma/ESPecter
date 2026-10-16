#include "engine_host.h"

#include "actions.h"
#include "arena.h"
#include "world.h"

#include "core/Engine.h"
#include "core/Scene.h"
#include "graphics/Color.h"
#include "graphics/DisplayConfig.h"
#include "graphics/Renderer.h"

#include <cstdio>
#include <cstdlib>

#ifdef PLATFORM_NATIVE
#include <SDL2/SDL.h>
#else
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
int g_pc_fps = -1;
unsigned long g_pc_ms;
int g_pc_frames;

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

/* [pc_note_frame] Acumula o frame e atualiza o FPS a cada meio segundo. */
void pc_note_frame(unsigned long delta_time)
{
    pc_window_bind();
    if (delta_time == 0 || delta_time > 100) {
        return;
    }
    g_pc_ms += delta_time;
    g_pc_frames += 1;
    if (g_pc_ms < 500) {
        return;
    }
    g_pc_fps = (int)((g_pc_frames * 1000ul) / g_pc_ms);
    g_pc_ms = 0;
    g_pc_frames = 0;
}
#endif

class rect_scene : public pixelroot32::core::Scene {
public:
    /* [rect_scene::update] Aplica [actions_capture] e avança com [world_tick]. */
    void update(unsigned long delta_time) override
    {
        actions input;

        actions_capture(&input);
        world_tick(&input);
#ifdef PLATFORM_NATIVE
        pc_note_frame(delta_time);
#else
        (void)delta_time;
#endif
    }

    /* [rect_scene::draw] Pinta o ground e o Rect. No device a cor vem de [link_ground]. O FPS vem de [pc_note_frame]. */
    void draw(pixelroot32::graphics::Renderer &renderer) override
    {
        int x;
        int y;
        int w;
        int h;

        world_rect(&x, &y, &w, &h);
#ifdef PLATFORM_NATIVE
        renderer.drawFilledRectangle(0, 0, WORLD_VIEW, WORLD_VIEW, pixelroot32::graphics::Color::DarkGreen);
#else
        renderer.drawFilledRectangleW(0, 0, WORLD_VIEW, WORLD_VIEW, link_ground());
#endif
        renderer.drawFilledRectangle(x, y, w, h, pixelroot32::graphics::Color::White);
#ifdef PLATFORM_NATIVE
        if (g_pc_fps >= 0) {
            char label[16];

            std::snprintf(label, sizeof label, "%d FPS", g_pc_fps);
            renderer.drawText(label, 4, 4, pixelroot32::graphics::Color::White, 1);
        }
#endif
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
