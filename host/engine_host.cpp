#include "engine_host.h"

#include "actions.h"
#include "arena.h"
#include "body_sheet.h"
#include "palette.h"
#include "tile_sheet.h"
#include "world.h"

extern "C" {
#include "map.h"
}

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
#include <new>

#ifdef PLATFORM_NATIVE
#include <SDL2/SDL.h>
#else
#include "battery.h"
#include "pad_link.h"
#include "st7789_drawer.h"

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/usb_serial_jtag_struct.h"

#include "frame_profile.h"

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
    uint8_t solid_kind[WORLD_SOLID_CAP];
    view_rect spike;
    view_rect checkpoint;
    view_rect exit_box;
    view_rect shuttle;
    int disk_phase;
    int debug;
    uint16_t ground;
    int battery_pct;
};

struct view_slot {
    std::atomic<uint32_t> seq;
    draw_view view;
};

#ifdef PLATFORM_NATIVE
view_slot g_slot_obj;
draw_view g_shown_obj;
view_slot *g_slot = &g_slot_obj;
draw_view *g_shown = &g_shown_obj;
#else
view_slot *g_slot;
draw_view *g_shown;

/* [bind_snapshot] Constrói o seqlock na slice de [arena_snapshot]. */
void bind_snapshot(void)
{
    arena_slice slice;

    slice = arena_snapshot();
    if (slice.data == nullptr || slice.size < sizeof(view_slot) + sizeof(draw_view)) {
        std::abort();
    }
    g_slot = new (slice.data) view_slot{};
    g_shown = reinterpret_cast<draw_view *>(slice.data + sizeof(view_slot));
    std::memset(g_shown, 0, sizeof *g_shown);
}
#endif

static_assert(sizeof(view_slot) + sizeof(draw_view) == ARENA_SNAPSHOT_BYTES, "draw snapshot size");

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

/* [draw_disk] Pinta o tampão em fileiras pretas e o raio branco da fase. O miolo sob o chão não entra. */
void draw_disk(pixelroot32::graphics::Renderer &renderer, int cam_x, int cam_y, int phase)
{
    int cap_x;
    int cap_y;
    int cap_w;
    int cap_h;
    int row;
    int x;
    int w;
    int sx;
    int sy;
    int sw;
    int sh;
    int spoke;

    world_disk_cap(&cap_x, &cap_y, &cap_w, &cap_h);
    (void)cap_x;
    (void)cap_w;
    for (row = 0; row < cap_h; row++) {
        if (!world_disk_row(cap_y + row, &x, &w)) {
            continue;
        }
        sx = x;
        sy = cap_y + row;
        sw = w;
        sh = 1;
        if (clip_view(cam_x, cam_y, &sx, &sy, &sw, &sh)) {
            renderer.drawFilledRectangle(sx, sy, sw, sh, pixelroot32::graphics::Color::Black);
        }
        if (!world_disk_spoke(phase, cap_y + row, &spoke)) {
            continue;
        }
        sx = spoke;
        sy = cap_y + row;
        sw = 1;
        sh = 1;
        if (clip_view(cam_x, cam_y, &sx, &sy, &sw, &sh)) {
            renderer.drawFilledRectangle(sx, sy, sw, sh, pixelroot32::graphics::Color::White);
        }
    }
}

/* [blit_tile_indices] Copia o recorte 8bpp para a View. No device usa [st7789_drawer::blit_indices]; no PC escreve RGB565. */
void blit_tile_indices(pixelroot32::graphics::Renderer &renderer, int dst_x, int dst_y, int src_x, int src_y, int w,
                       int h, const uint8_t *src)
{
#ifdef PLATFORM_NATIVE
    uint16_t *fb;
    int row;
    int col;
    int x0;
    int y0;
    int x1;
    int y1;
    int sx0;
    int sy0;

    if (src == nullptr || w <= 0 || h <= 0) {
        return;
    }
    fb = renderer.getDrawSurface().getPixelBuffer();
    if (fb == nullptr) {
        return;
    }
    x0 = dst_x;
    y0 = dst_y;
    x1 = dst_x + w;
    y1 = dst_y + h;
    sx0 = src_x;
    sy0 = src_y;
    if (x0 < 0) {
        sx0 -= x0;
        x0 = 0;
    }
    if (y0 < 0) {
        sy0 -= y0;
        y0 = 0;
    }
    if (x1 > WORLD_VIEW) {
        x1 = WORLD_VIEW;
    }
    if (y1 > WORLD_VIEW) {
        y1 = WORLD_VIEW;
    }
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    for (row = y0; row < y1; row++) {
        const uint8_t *src_row = src + (sy0 + (row - y0)) * TILE_SHEET_W + sx0;
        uint16_t *dst = fb + row * WORLD_VIEW + x0;

        for (col = 0; col < x1 - x0; col++) {
            uint8_t index = src_row[col];

            if (index != 0) {
                dst[col] = palette_rgb565[index];
            }
        }
    }
#else
    auto &drawer = static_cast<especter::st7789_drawer &>(renderer.getDrawSurface());

    drawer.blit_indices(dst_x, dst_y, src_x, src_y, w, h, TILE_SHEET_W, src);
#endif
}

/* [draw_tile_kind] Pinta o sprite do kind no retângulo publicado, recortado à View. Índice 0 deixa o chão. */
void draw_tile_kind(pixelroot32::graphics::Renderer &renderer, int cam_x, int cam_y, int tile_x, int tile_y, int w,
                    int h, int kind)
{
    const uint8_t *px;
    int sx;
    int sy;
    int sw;
    int sh;
    int src_x;
    int src_y;

    px = tile_sheet_at(kind);
    if (px == nullptr || w <= 0 || h <= 0) {
        return;
    }
    sx = tile_x;
    sy = tile_y;
    sw = w;
    sh = h;
    if (!clip_view(cam_x, cam_y, &sx, &sy, &sw, &sh)) {
        return;
    }
    src_x = sx - tile_x;
    src_y = sy - tile_y;
    if (src_x < 0 || src_y < 0 || src_x + sw > TILE_SHEET_W || src_y + sh > TILE_SHEET_H) {
        return;
    }
    blit_tile_indices(renderer, sx - cam_x, sy - cam_y, src_x, src_y, sw, sh, px);
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

    x = g_shown->body_x;
    y = g_shown->body_y;
    w = g_shown->body_w;
    h = g_shown->body_h;
    x -= (BODY_SHEET_W - w) / 2;
    y -= BODY_SHEET_H - h;
    if (!box_hits_view(cam_x, cam_y, x, y, BODY_SHEET_W, BODY_SHEET_H)) {
        return;
    }
    clip = g_shown->clip;
    frame = g_shown->frame;
    face = g_shown->face;
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

    seq = g_slot->seq.load(std::memory_order_relaxed);
    g_slot->seq.store(seq + 1u, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    world_camera(&g_slot->view.cam_x, &g_slot->view.cam_y);
    world_rect(&g_slot->view.body_x, &g_slot->view.body_y, &g_slot->view.body_w, &g_slot->view.body_h);
    world_body_pose(&g_slot->view.clip, &g_slot->view.frame, &g_slot->view.face);
    count = world_solid_count();
    if (count < 0) {
        count = 0;
    }
    if (count > WORLD_SOLID_CAP) {
        count = WORLD_SOLID_CAP;
    }
    g_slot->view.solid_count = count;
    for (i = 0; i < count; i++) {
        int kind;

        world_solid_rect(i, &g_slot->view.solids[i].x, &g_slot->view.solids[i].y, &g_slot->view.solids[i].w,
                         &g_slot->view.solids[i].h, &kind);
        g_slot->view.solid_kind[i] = (uint8_t)kind;
    }
    world_spike_rect(&g_slot->view.spike.x, &g_slot->view.spike.y, &g_slot->view.spike.w, &g_slot->view.spike.h);
    world_checkpoint_rect(&g_slot->view.checkpoint.x, &g_slot->view.checkpoint.y, &g_slot->view.checkpoint.w,
                          &g_slot->view.checkpoint.h);
    world_exit_rect(&g_slot->view.exit_box.x, &g_slot->view.exit_box.y, &g_slot->view.exit_box.w, &g_slot->view.exit_box.h);
    world_shuttle_rect(&g_slot->view.shuttle.x, &g_slot->view.shuttle.y, &g_slot->view.shuttle.w,
                       &g_slot->view.shuttle.h);
    g_slot->view.disk_phase = world_disk_phase();
    g_slot->view.debug = g_debug;
#ifndef PLATFORM_NATIVE
    g_slot->view.ground = link_ground();
    g_slot->view.battery_pct = battery_percent();
#else
    g_slot->view.ground = 0;
    g_slot->view.battery_pct = -1;
#endif
    std::atomic_thread_fence(std::memory_order_release);
    g_slot->seq.store(seq + 2u, std::memory_order_relaxed);
}

/* [load_view] Copia o slot estável para a view do present. Repete se o seq mudar. */
void load_view(void)
{
    for (;;) {
        uint32_t seq;

        seq = g_slot->seq.load(std::memory_order_relaxed);
        if (seq & 1u) {
            continue;
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        std::memcpy(g_shown, &g_slot->view, sizeof *g_shown);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq == g_slot->seq.load(std::memory_order_relaxed)) {
            return;
        }
    }
}

/* [sim_advance] Lê [host_edges], gasta o delta em ticks de 12 ms de [world_tick], no máximo 4, e chama [publish_view]. No PC, [pc_sim_held] segura os ticks. */
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
    while (g_sim_ms >= WORLD_TICK_MS && steps < 4) {
        actions input;

        actions_capture(&input);
        world_tick(&input);
        g_sim_ms -= WORLD_TICK_MS;
        steps += 1;
    }
    if (steps == 4 && g_sim_ms >= WORLD_TICK_MS) {
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

#ifndef PLATFORM_NATIVE
constexpr int k_text_rows = 8;

int g_have_last;
int g_last_cam_x;
int g_last_cam_y;
int g_last_body_x;
int g_last_body_y;
int g_last_body_w;
int g_last_body_h;
int g_last_clip;
int g_last_frame;
int g_last_face;
int g_last_debug;
int g_last_fps;
int g_last_battery;
uint16_t g_last_ground;
view_rect g_last_spike;
view_rect g_last_checkpoint;
view_rect g_last_exit;
view_rect g_last_shuttle;
int g_last_disk_phase;

/* [rect_same] Devolve 1 se os dois Rect coincidem. */
int rect_same(const view_rect *a, const view_rect *b)
{
    return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

/* [mark_rect_rows] Marca o Y de tela do Rect no cam_y. */
void mark_rect_rows(int cam_y, int y, int h)
{
    if (h <= 0) {
        return;
    }
    especter::st7789_rows_mark(y - cam_y, y - cam_y + h);
}

/* [mark_body_rows] Marca o Y de tela da sheet de 16×16, como [draw_body]. */
void mark_body_rows(int cam_y, int body_y, int body_h)
{
    int top;

    top = body_y - (BODY_SHEET_H - body_h);
    especter::st7789_rows_mark(top - cam_y, top - cam_y + BODY_SHEET_H);
}

/* [body_changed] Devolve 1 se a pose ou o Rect do corpo mudou desde o último present. */
int body_changed(void)
{
    return g_shown->body_x != g_last_body_x || g_shown->body_y != g_last_body_y || g_shown->body_w != g_last_body_w
        || g_shown->body_h != g_last_body_h || g_shown->clip != g_last_clip || g_shown->frame != g_last_frame
        || g_shown->face != g_last_face;
}

/* [mark_disk_rows] Marca o Y de tela do tampão. */
void mark_disk_rows(int cam_y)
{
    int x;
    int y;
    int w;
    int h;

    world_disk_cap(&x, &y, &w, &h);
    mark_rect_rows(cam_y, y, h);
}

/* [mark_movers] Marca sólidos, spike, checkpoint, saída, shuttle, tampão e corpo no cam_y. */
void mark_movers(int cam_y)
{
    int i;

    for (i = 0; i < g_shown->solid_count; i++) {
        mark_rect_rows(cam_y, g_shown->solids[i].y, g_shown->solids[i].h);
    }
    mark_rect_rows(cam_y, g_shown->spike.y, g_shown->spike.h);
    mark_rect_rows(cam_y, g_shown->checkpoint.y, g_shown->checkpoint.h);
    mark_rect_rows(cam_y, g_shown->exit_box.y, g_shown->exit_box.h);
    mark_rect_rows(cam_y, g_shown->shuttle.y, g_shown->shuttle.h);
    mark_disk_rows(cam_y);
    mark_body_rows(cam_y, g_shown->body_y, g_shown->body_h);
}

/* [mark_present_rows] Monta a máscara a partir da view e da anterior. Devolve quantas rows ficaram sujas. */
int mark_present_rows(void)
{
    int cam_moved;
    int debug_toggled;

    especter::st7789_rows_clear();
    if (!g_have_last || especter::st7789_take_gram() || g_shown->ground != g_last_ground) {
        especter::st7789_rows_mark_all();
        return especter::st7789_rows_count();
    }
    cam_moved = g_shown->cam_x != g_last_cam_x || g_shown->cam_y != g_last_cam_y;
    debug_toggled = g_shown->debug != g_last_debug;
    if (debug_toggled || (g_shown->debug && cam_moved)) {
        especter::st7789_rows_mark_all();
        return especter::st7789_rows_count();
    }
    if (cam_moved) {
        mark_movers(g_last_cam_y);
        mark_movers(g_shown->cam_y);
        if (body_changed()) {
            mark_body_rows(g_last_cam_y, g_last_body_y, g_last_body_h);
            mark_body_rows(g_shown->cam_y, g_shown->body_y, g_shown->body_h);
        }
        if (!rect_same(&g_shown->spike, &g_last_spike)) {
            mark_rect_rows(g_last_cam_y, g_last_spike.y, g_last_spike.h);
            mark_rect_rows(g_shown->cam_y, g_shown->spike.y, g_shown->spike.h);
        }
        if (!rect_same(&g_shown->checkpoint, &g_last_checkpoint)) {
            mark_rect_rows(g_last_cam_y, g_last_checkpoint.y, g_last_checkpoint.h);
            mark_rect_rows(g_shown->cam_y, g_shown->checkpoint.y, g_shown->checkpoint.h);
        }
        if (!rect_same(&g_shown->exit_box, &g_last_exit)) {
            mark_rect_rows(g_last_cam_y, g_last_exit.y, g_last_exit.h);
            mark_rect_rows(g_shown->cam_y, g_shown->exit_box.y, g_shown->exit_box.h);
        }
        if (!rect_same(&g_shown->shuttle, &g_last_shuttle)) {
            mark_rect_rows(g_last_cam_y, g_last_shuttle.y, g_last_shuttle.h);
            mark_rect_rows(g_shown->cam_y, g_shown->shuttle.y, g_shown->shuttle.h);
        }
    } else {
        if (body_changed()) {
            mark_body_rows(g_shown->cam_y, g_last_body_y, g_last_body_h);
            mark_body_rows(g_shown->cam_y, g_shown->body_y, g_shown->body_h);
        }
        if (!rect_same(&g_shown->spike, &g_last_spike)) {
            mark_rect_rows(g_shown->cam_y, g_last_spike.y, g_last_spike.h);
            mark_rect_rows(g_shown->cam_y, g_shown->spike.y, g_shown->spike.h);
        }
        if (!rect_same(&g_shown->checkpoint, &g_last_checkpoint)) {
            mark_rect_rows(g_shown->cam_y, g_last_checkpoint.y, g_last_checkpoint.h);
            mark_rect_rows(g_shown->cam_y, g_shown->checkpoint.y, g_shown->checkpoint.h);
        }
        if (!rect_same(&g_shown->exit_box, &g_last_exit)) {
            mark_rect_rows(g_shown->cam_y, g_last_exit.y, g_last_exit.h);
            mark_rect_rows(g_shown->cam_y, g_shown->exit_box.y, g_shown->exit_box.h);
        }
        if (!rect_same(&g_shown->shuttle, &g_last_shuttle)) {
            mark_rect_rows(g_shown->cam_y, g_last_shuttle.y, g_last_shuttle.h);
            mark_rect_rows(g_shown->cam_y, g_shown->shuttle.y, g_shown->shuttle.h);
        }
        if (g_shown->disk_phase != g_last_disk_phase) {
            mark_disk_rows(g_shown->cam_y);
        }
    }
    if (g_shown->debug && (g_fps != g_last_fps || g_shown->battery_pct != g_last_battery)) {
        especter::st7789_rows_mark(0, k_text_rows);
    }
    return especter::st7789_rows_count();
}

/* [remember_present] Guarda a view que este present pintou. */
void remember_present(void)
{
    g_last_cam_x = g_shown->cam_x;
    g_last_cam_y = g_shown->cam_y;
    g_last_body_x = g_shown->body_x;
    g_last_body_y = g_shown->body_y;
    g_last_body_w = g_shown->body_w;
    g_last_body_h = g_shown->body_h;
    g_last_clip = g_shown->clip;
    g_last_frame = g_shown->frame;
    g_last_face = g_shown->face;
    g_last_debug = g_shown->debug;
    g_last_fps = g_fps;
    g_last_battery = g_shown->battery_pct;
    g_last_ground = g_shown->ground;
    g_last_spike = g_shown->spike;
    g_last_checkpoint = g_shown->checkpoint;
    g_last_exit = g_shown->exit_box;
    g_last_shuttle = g_shown->shuttle;
    g_last_disk_phase = g_shown->disk_phase;
    g_have_last = 1;
}
#endif

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

    /* [rect_scene::draw] Pinta a view de [load_view]: ground, tiles da sheet, shuttle, tampão, spike, checkpoint, saída e corpo no offset da câmera. No device só as rows de [mark_present_rows]. Com o debug, o wireframe das células, a carga no canto superior esquerdo no device e o FPS no canto superior direito. No PC, com o PWR apagado, pinta preto e volta. */
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
#ifndef PLATFORM_NATIVE
        int timing;
        int64_t stamp;
#endif

#ifndef PLATFORM_NATIVE
        timing = frame_profile_on();
        stamp = 0;
        if (timing) {
            stamp = esp_timer_get_time();
        }
#endif
        load_view();
#ifdef PLATFORM_NATIVE
        if (!g_pc_lit) {
            renderer.setOffsetBypass(true);
            renderer.drawFilledRectangle(0, 0, WORLD_VIEW, WORLD_VIEW, pixelroot32::graphics::Color::Black);
            return;
        }
#else
        if (mark_present_rows() == 0) {
            if (timing) {
                frame_profile_add(FRAME_MASK, esp_timer_get_time() - stamp);
                frame_profile_add(FRAME_GROUND, 0);
                frame_profile_add(FRAME_SOLIDS, 0);
                frame_profile_add(FRAME_BODY, 0);
                frame_profile_add(FRAME_DEBUG, 0);
            }
            return;
        }
        if (timing) {
            frame_profile_add(FRAME_MASK, esp_timer_get_time() - stamp);
            stamp = esp_timer_get_time();
        }
#endif
        cam_x = g_shown->cam_x;
        cam_y = g_shown->cam_y;
        renderer.setOffsetBypass(true);
#ifdef PLATFORM_NATIVE
        renderer.drawFilledRectangle(0, 0, WORLD_VIEW, WORLD_VIEW, pixelroot32::graphics::Color::DarkGreen);
#else
        renderer.drawFilledRectangleW(0, 0, WORLD_VIEW, WORLD_VIEW, g_shown->ground);
        if (timing) {
            frame_profile_add(FRAME_GROUND, esp_timer_get_time() - stamp);
            stamp = esp_timer_get_time();
        }
#endif
        renderer.setOffsetBypass(false);
        renderer.setDisplayOffset(-cam_x, -cam_y);
        count = g_shown->solid_count;
        for (i = 0; i < count; i++) {
            draw_tile_kind(renderer, cam_x, cam_y, g_shown->solids[i].x, g_shown->solids[i].y, g_shown->solids[i].w,
                           g_shown->solids[i].h, g_shown->solid_kind[i]);
        }
        mx = g_shown->shuttle.x;
        my = g_shown->shuttle.y;
        mw = g_shown->shuttle.w;
        mh = g_shown->shuttle.h;
        if (clip_view(cam_x, cam_y, &mx, &my, &mw, &mh)) {
            renderer.drawFilledRectangle(mx, my, mw, mh, pixelroot32::graphics::Color::Black);
        }
        draw_disk(renderer, cam_x, cam_y, g_shown->disk_phase);
        mx = g_shown->spike.x;
        my = g_shown->spike.y;
        mw = g_shown->spike.w;
        mh = g_shown->spike.h;
        draw_world_fill(renderer, cam_x, cam_y, mx, my, mw, mh, g_hazard_rgb);
        mx = g_shown->checkpoint.x;
        my = g_shown->checkpoint.y;
        mw = g_shown->checkpoint.w;
        mh = g_shown->checkpoint.h;
        draw_world_outline(renderer, cam_x, cam_y, mx, my, mw, mh);
        mx = g_shown->exit_box.x;
        my = g_shown->exit_box.y;
        mw = g_shown->exit_box.w;
        mh = g_shown->exit_box.h;
        draw_world_outline(renderer, cam_x, cam_y, mx, my, mw, mh);
#ifndef PLATFORM_NATIVE
        if (timing) {
            frame_profile_add(FRAME_SOLIDS, esp_timer_get_time() - stamp);
            stamp = esp_timer_get_time();
        }
#endif
        draw_body(renderer, cam_x, cam_y);
#ifndef PLATFORM_NATIVE
        if (timing) {
            frame_profile_add(FRAME_BODY, esp_timer_get_time() - stamp);
            stamp = esp_timer_get_time();
        }
#endif
        if (g_shown->debug) {
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
        if (g_shown->debug) {
#ifndef PLATFORM_NATIVE
            {
                const int pct = g_shown->battery_pct;

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
#ifndef PLATFORM_NATIVE
        if (timing) {
            frame_profile_add(FRAME_DEBUG, esp_timer_get_time() - stamp);
        }
        remember_present();
#endif
    }
};

rect_scene g_scene;

/* [log_bytes] Imprime o nome e os bytes. */
void log_bytes(const char *name, size_t bytes)
{
#ifdef PLATFORM_NATIVE
    std::printf("%s %u\n", name, (unsigned)bytes);
#else
    ESP_LOGI("especter", "%s %u", name, (unsigned)bytes);
#endif
}

/* [sheet_bytes] Soma os quadros de [body_sheet_frames]. */
size_t sheet_bytes(void)
{
    int clip;
    int frames;

    frames = 0;
    for (clip = 0; clip < BODY_CLIP_COUNT; clip++) {
        frames += body_sheet_frames(clip);
    }
    return static_cast<size_t>(frames) * static_cast<size_t>(BODY_SHEET_W) * static_cast<size_t>(BODY_SHEET_H);
}

/* [log_memory] Imprime cada slice das arenas e, no device, o heap interno, a PSRAM e o app na flash. */
void log_memory(void)
{
    log_bytes("psram arena", arena_capacity());
    log_bytes("psram world", arena_world().size);
    log_bytes("psram level", arena_level().size);
    log_bytes("psram scratch", arena_scratch().size);
    log_bytes("psram spare", arena_capacity() - arena_offset());
    log_bytes("palette", sizeof palette_rgb565);
    log_bytes("body", sheet_bytes());
    log_bytes("tiles", static_cast<size_t>(TILE_SHEET_COUNT) * TILE_SHEET_W * TILE_SHEET_H);
#ifndef PLATFORM_NATIVE
    log_bytes("internal arena", arena_internal_capacity());
    log_bytes("internal bands", arena_bands().size);
    log_bytes("internal pixels", arena_pixels().size);
    log_bytes("internal snapshot", arena_snapshot().size);
    log_bytes("internal sim", arena_stack_sim().size);
    log_bytes("internal present", arena_stack_present().size);
    log_bytes("internal backlight", arena_stack_backlight().size);
    log_bytes("internal pad", arena_stack_pad().size);
    log_bytes("internal tcb sim", arena_tcb_sim().size);
    log_bytes("internal tcb present", arena_tcb_present().size);
    log_bytes("internal tcb backlight", arena_tcb_backlight().size);
    log_bytes("internal tcb pad", arena_tcb_pad().size);
    log_bytes("internal engine", arena_engine().size);
    log_bytes("internal drawer", arena_drawer().size);
    size_t carved = arena_bands().size + arena_pixels().size + arena_snapshot().size + arena_stack_sim().size
        + arena_stack_present().size + arena_stack_backlight().size + arena_stack_pad().size
        + arena_tcb_sim().size + arena_tcb_present().size + arena_tcb_backlight().size + arena_tcb_pad().size
        + arena_engine().size + arena_drawer().size;
    if (arena_internal_offset() > carved) {
        log_bytes("internal align", arena_internal_offset() - carved);
    }
    log_bytes("internal spare", arena_internal_capacity() - arena_internal_offset());
    ESP_LOGI("especter", "internal heap total %u free %u largest %u",
             (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ESP_LOGI("especter", "psram heap total %u free %u",
             (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    {
        const esp_partition_t *part = esp_ota_get_running_partition();
        esp_partition_pos_t pos = {};
        esp_image_metadata_t image = {};

        if (part != nullptr) {
            pos.offset = part->address;
            pos.size = part->size;
            if (esp_image_get_metadata(&pos, &image) == ESP_OK) {
                ESP_LOGI("especter", "flash app %u partition %u",
                         (unsigned)image.image_len,
                         (unsigned)part->size);
            }
        }
    }
#endif
}

#ifndef PLATFORM_NATIVE
static_assert(sizeof(especter::st7789_drawer) <= ARENA_DRAWER_BYTES, "drawer slice");
static_assert(alignof(especter::st7789_drawer) <= 16u, "drawer align");
static_assert(sizeof(pixelroot32::core::Engine) <= ARENA_ENGINE_BYTES, "engine slice");
static_assert(alignof(pixelroot32::core::Engine) <= 16u, "engine align");
static_assert(sizeof(StaticTask_t) <= ARENA_TCB_BYTES, "tcb slice");
static_assert(alignof(StaticTask_t) <= 16u, "tcb align");
static_assert(alignof(view_slot) <= 16u, "snapshot align");

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
        int timing;
        int64_t loop_t0;
        int64_t wait_us;
        int64_t sim_t0;

        timing = frame_profile_on();
        loop_t0 = 0;
        wait_us = 0;
        if (timing) {
            loop_t0 = esp_timer_get_time();
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
        if (timing) {
            wait_us = esp_timer_get_time() - loop_t0;
        }
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
        sim_t0 = 0;
        if (timing) {
            sim_t0 = esp_timer_get_time();
        }
        if (delta > 0) {
            sim_advance(delta);
        }
        if (timing) {
            int64_t sim_us;
            int64_t other_us;

            sim_us = esp_timer_get_time() - sim_t0;
            other_us = (esp_timer_get_time() - loop_t0) - wait_us - sim_us;
            if (other_us < 0) {
                other_us = 0;
            }
            frame_profile_core0(wait_us, sim_us, other_us);
        }
    }
}

/* [present_task] No core 1, constrói o drawer na slice de [arena_drawer] e o Engine na slice de [arena_engine], e chama Engine::run enquanto [st7789_lit] estiver aceso. Máscara vazia espera 1 ms. No duty 0, [st7789_drawer::spi_drain] fecha o band no fio antes de dormir. */
void present_task(void *arg)
{
    unsigned long prev;
    arena_slice drawer_slice;
    arena_slice engine_slice;
    especter::st7789_drawer *drawer;
    pixelroot32::core::Engine *engine;

    (void)arg;
    drawer_slice = arena_drawer();
    engine_slice = arena_engine();
    if (drawer_slice.data == nullptr || engine_slice.data == nullptr
        || drawer_slice.size < sizeof(especter::st7789_drawer)
        || engine_slice.size < sizeof(pixelroot32::core::Engine)) {
        ESP_LOGE("especter", "engine slice failed");
        std::abort();
    }
    drawer = new (drawer_slice.data) especter::st7789_drawer();
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
    engine = new (engine_slice.data) pixelroot32::core::Engine(config);
    especter::st7789_bind_present(xTaskGetCurrentTaskHandle());
    engine->init();
    log_memory();
    engine->setScene(&g_scene);
    prev = millis();
    for (;;) {
        unsigned long now;
        unsigned long delta;

        if (!especter::st7789_lit()) {
            drawer->spi_drain();
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            prev = millis();
            continue;
        }
        {
            int timing;
            int64_t loop_t0;
            int64_t wait_us;
            int rows;

            timing = frame_profile_on();
            loop_t0 = 0;
            wait_us = 0;
            if (timing) {
                frame_profile_begin();
                loop_t0 = esp_timer_get_time();
            }
            now = millis();
            delta = now - prev;
            prev = now;
            if (delta > 0) {
                note_frame(delta);
            }
            engine->run();
            rows = especter::st7789_rows_count();
            if (rows == 0) {
                int64_t wait_t0;

                wait_t0 = 0;
                if (timing) {
                    wait_t0 = esp_timer_get_time();
                }
                vTaskDelay(pdMS_TO_TICKS(1));
                if (timing) {
                    wait_us = esp_timer_get_time() - wait_t0;
                }
            }
            if (timing) {
                frame_profile_frame(esp_timer_get_time() - loop_t0, wait_us, rows == 0, rows);
            }
            frame_profile_flush();
        }
    }
}
#endif

} // namespace

#ifndef PLATFORM_NATIVE
namespace {

enum {
    CORE0_WAIT = 0,
    CORE0_SIM = 1,
    CORE0_OTHER = 2,
    CORE0_PART_COUNT = 3
};

std::atomic<int> g_profile_on{0};
int g_profile_primed;
int64_t g_gate_at;
int64_t g_print_at;
uint32_t g_sof;
int g_profile_n;
int g_profile_empty;
int g_profile_busy;
int g_profile_rows_max;
int64_t g_profile_rows;
int64_t g_part_sum[FRAME_PART_COUNT];
int64_t g_part_max[FRAME_PART_COUNT];
int64_t g_part_last[FRAME_PART_COUNT];
std::atomic<int64_t> g_c0_sum[CORE0_PART_COUNT];
std::atomic<int64_t> g_c0_max[CORE0_PART_COUNT];
std::atomic<int> g_c0_n{0};

const char *k_part_name[FRAME_PART_COUNT] = {
    "mask", "ground", "solids", "body", "debug", "expand", "spi", "engine", "wait"};
const char *k_c0_name[CORE0_PART_COUNT] = {"wait", "sim", "other"};

/* [profile_reset_present] Zera os acumuladores do core 1. */
void profile_reset_present(void)
{
    int i;

    g_profile_n = 0;
    g_profile_empty = 0;
    g_profile_busy = 0;
    g_profile_rows_max = 0;
    g_profile_rows = 0;
    for (i = 0; i < FRAME_PART_COUNT; i++) {
        g_part_sum[i] = 0;
        g_part_max[i] = 0;
        g_part_last[i] = 0;
    }
}

/* [profile_reset_core0] Zera os acumuladores do core 0. */
void profile_reset_core0(void)
{
    int i;

    g_c0_n.store(0, std::memory_order_relaxed);
    for (i = 0; i < CORE0_PART_COUNT; i++) {
        g_c0_sum[i].store(0, std::memory_order_relaxed);
        g_c0_max[i].store(0, std::memory_order_relaxed);
    }
}

/* [profile_c0_max] Sobe o máximo do pedaço do core 0 se us for maior. */
void profile_c0_max(int part, int64_t us)
{
    int64_t prev;

    prev = g_c0_max[part].load(std::memory_order_relaxed);
    while (us > prev && !g_c0_max[part].compare_exchange_weak(prev, us, std::memory_order_relaxed)) {
    }
}

/* [profile_pct] Devolve o percentual inteiro de part sobre frame. */
int profile_pct(int64_t part, int64_t frame)
{
    if (frame <= 0) {
        return 0;
    }
    return (int)((part * 100) / frame);
}

/* [profile_print] Imprime o segundo. Cada percentual é a fatia daquele core. O tempo deste print sai na linha log. */
void profile_print(void)
{
    int64_t t0;
    int n;
    int rows;
    int i;
    int64_t core1;
    int c0_n;
    int64_t c0_sum[CORE0_PART_COUNT];
    int64_t c0_max[CORE0_PART_COUNT];
    int64_t core0;

    if (g_profile_n <= 0) {
        return;
    }
    t0 = esp_timer_get_time();
    n = g_profile_n;
    rows = g_profile_busy > 0 ? (int)(g_profile_rows / g_profile_busy) : 0;
    core1 = 0;
    for (i = 0; i < FRAME_PART_COUNT; i++) {
        core1 += g_part_sum[i];
    }
    ESP_LOGI("frame", "core1 %lld us n %d empty %d busy %d rows %d max %d", (long long)core1, n, g_profile_empty,
             g_profile_busy, rows, g_profile_rows_max);
    for (i = 0; i < FRAME_PART_COUNT; i++) {
        ESP_LOGI("frame", "core1 %s %lld us max %lld %d%%", k_part_name[i], (long long)(g_part_sum[i] / n),
                 (long long)g_part_max[i], profile_pct(g_part_sum[i], core1));
    }
    core0 = 0;
    c0_n = g_c0_n.exchange(0, std::memory_order_relaxed);
    for (i = 0; i < CORE0_PART_COUNT; i++) {
        c0_sum[i] = g_c0_sum[i].exchange(0, std::memory_order_relaxed);
        c0_max[i] = g_c0_max[i].exchange(0, std::memory_order_relaxed);
        core0 += c0_sum[i];
    }
    ESP_LOGI("frame", "core0 %lld us n %d", (long long)core0, c0_n);
    for (i = 0; i < CORE0_PART_COUNT; i++) {
        int64_t mean;

        mean = c0_n > 0 ? c0_sum[i] / c0_n : 0;
        ESP_LOGI("frame", "core0 %s %lld us max %lld %d%%", k_c0_name[i], (long long)mean, (long long)c0_max[i],
                 profile_pct(c0_sum[i], core0));
    }
    ESP_LOGI("frame", "core1 log %lld us", (long long)(esp_timer_get_time() - t0));
}

} // namespace

/* [frame_profile_on] Devolve 1 no segundo em que o log do frame está armado. */
int frame_profile_on(void)
{
    return g_profile_on.load(std::memory_order_relaxed);
}

/* [frame_profile_begin] Zera os pedaços deste frame no core 1. */
void frame_profile_begin(void)
{
    int i;

    for (i = 0; i < FRAME_PART_COUNT; i++) {
        g_part_last[i] = 0;
    }
}

/* [frame_profile_add] Soma us no pedaço do core 1. Com [frame_profile_on] desligado, não lê o relógio. */
void frame_profile_add(int part, int64_t us)
{
    if (!frame_profile_on() || part < 0 || part >= FRAME_PART_COUNT) {
        return;
    }
    g_part_sum[part] += us;
    g_part_last[part] = us;
    if (us > g_part_max[part]) {
        g_part_max[part] = us;
    }
}

/* [frame_profile_frame] Fecha o laço do core 1: o resto vira engine, a espera entra em wait. */
void frame_profile_frame(int64_t loop_us, int64_t wait_us, int empty, int rows)
{
    int i;
    int64_t engine;

    if (!frame_profile_on()) {
        return;
    }
    engine = loop_us - wait_us;
    for (i = 0; i < FRAME_PART_COUNT; i++) {
        if (i == FRAME_ENGINE || i == FRAME_WAIT) {
            continue;
        }
        engine -= g_part_last[i];
    }
    if (engine < 0) {
        engine = 0;
    }
    frame_profile_add(FRAME_ENGINE, engine);
    frame_profile_add(FRAME_WAIT, wait_us);
    g_profile_n += 1;
    if (empty) {
        g_profile_empty += 1;
    } else {
        g_profile_busy += 1;
        g_profile_rows += rows;
        if (rows > g_profile_rows_max) {
            g_profile_rows_max = rows;
        }
    }
}

/* [frame_profile_core0] Soma um laço do core 0: espera, [sim_advance] e o resto. */
void frame_profile_core0(int64_t wait_us, int64_t sim_us, int64_t other_us)
{
    if (!frame_profile_on()) {
        return;
    }
    g_c0_sum[CORE0_WAIT].fetch_add(wait_us, std::memory_order_relaxed);
    g_c0_sum[CORE0_SIM].fetch_add(sim_us, std::memory_order_relaxed);
    g_c0_sum[CORE0_OTHER].fetch_add(other_us, std::memory_order_relaxed);
    profile_c0_max(CORE0_WAIT, wait_us);
    profile_c0_max(CORE0_SIM, sim_us);
    profile_c0_max(CORE0_OTHER, other_us);
    g_c0_n.fetch_add(1, std::memory_order_relaxed);
}

/* [frame_profile_flush] Se o SOF andou, imprime uma vez por segundo média, máximo e percentual de cada core. */
void frame_profile_flush(void)
{
    int64_t now;
    uint32_t sof;
    int live;
    int on;

    now = esp_timer_get_time();
    if (!g_profile_primed) {
        g_sof = USB_SERIAL_JTAG.fram_num.sof_frame_index;
        g_profile_primed = 1;
        g_gate_at = now + 1000000;
        g_profile_on.store(0, std::memory_order_relaxed);
        return;
    }
    on = frame_profile_on();
    if (on && now >= g_print_at) {
        profile_print();
        profile_reset_present();
        g_print_at = now + 1000000;
    }
    if (now < g_gate_at) {
        return;
    }
    sof = USB_SERIAL_JTAG.fram_num.sof_frame_index;
    live = sof != g_sof;
    g_sof = sof;
    g_gate_at = now + 1000000;
    if (live == on) {
        return;
    }
    g_profile_on.store(live, std::memory_order_relaxed);
    profile_reset_present();
    profile_reset_core0();
    if (live) {
        g_print_at = now + 1000000;
    }
}
#endif

/* [engine_host_run] Sobe a Scene depois de [world_init]. No PC entra no laço do Engine. No device monta o snapshot, publica a view e prende o sim no core 0 e o present no core 1. */
extern "C" void engine_host_run(void)
{
#ifndef PLATFORM_NATIVE
    bind_snapshot();
#endif
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
    {
        arena_slice sim_stack = arena_stack_sim();
        arena_slice sim_tcb = arena_tcb_sim();
        arena_slice present_stack = arena_stack_present();
        arena_slice present_tcb = arena_tcb_present();
        TaskHandle_t sim;
        TaskHandle_t present;

        sim = nullptr;
        present = nullptr;
        if (sim_stack.data != nullptr && sim_tcb.data != nullptr) {
            sim = xTaskCreateStaticPinnedToCore(sim_task, "sim", static_cast<uint32_t>(sim_stack.size), nullptr,
                                                k_task_prio, reinterpret_cast<StackType_t *>(sim_stack.data),
                                                reinterpret_cast<StaticTask_t *>(sim_tcb.data), 0);
        }
        if (present_stack.data != nullptr && present_tcb.data != nullptr) {
            present = xTaskCreateStaticPinnedToCore(present_task, "present", static_cast<uint32_t>(present_stack.size),
                                                    nullptr, k_task_prio,
                                                    reinterpret_cast<StackType_t *>(present_stack.data),
                                                    reinterpret_cast<StaticTask_t *>(present_tcb.data), 1);
        }
        if (sim == nullptr) {
            ESP_LOGE("especter", "sim task failed");
            std::abort();
        }
        if (present == nullptr) {
            ESP_LOGE("especter", "present task failed");
            std::abort();
        }
        g_sim = sim;
    }
#endif
}
