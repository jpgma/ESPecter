#include "world.h"

#include "arena.h"
#include "body_sheet.h"

#define WORLD_CAP 8
#define WORLD_SUB 8
#define WORLD_RUN_ACCEL 64
#define WORLD_RUN_MAX 700
#define WORLD_JOG_MIN (WORLD_RUN_MAX / 2)
#define BODY_TICK_MS 16
#define WORLD_GRAVITY_RISE 128
#define WORLD_GRAVITY_FALL 256
#define WORLD_JUMP_VY (-2048)
#define WORLD_FALL_MAX 2048
#define WORLD_SLIDE_MAX 256
#define WORLD_WALL_JUMP 1500
#define WORLD_COLS 20
#define BODY_ACT_NONE 0
#define BODY_ACT_FLOOR 1
#define BODY_ACT_WALL 2
#define WORLD_CAM_STIFF 2
#define WORLD_CAM_DAMP_NUM 3
#define WORLD_CAM_DAMP_DEN 8
#define WORLD_CAM_FLOOR 32
#define WORLD_SPAN (WORLD_VIEW << WORLD_SUB)
#define SOLID_CAP 256
#define SOLID_COLS 6
#define SOLID_USED 10
#define PLAY_H 16
#define WALL_W 16
#define WALL_H 184
#define GROUND_Y 224
#define GROUND_H 32
#define PIT_Y (GROUND_Y + GROUND_H)
#define WORLD_TAIL 11
#define LIFE_PLAY 0
#define LIFE_DYING 1
#define LIFE_DONE 2
#define DEATH_HOLD 16
#define SPAWN_CELL_X 0
#define SPAWN_CELL_Y 0
#define SPAWN_X (((WORLD_VIEW - WORLD_BODY_W) / 2) << WORLD_SUB)
#define SPAWN_Y (((WORLD_VIEW - WORLD_BODY_H) / 2) << WORLD_SUB)
#define SPIKE_CX 1
#define SPIKE_CY 0
#define SPIKE_X 112
#define SPIKE_Y 216
#define SPIKE_W 48
#define SPIKE_H 8
#define CP_CX 1
#define CP_CY 0
#define CP_X 176
#define CP_Y 176
#define CP_W 16
#define CP_H 48
#define CP_STAND_X (180 << WORLD_SUB)
#define CP_STAND_Y (212 << WORLD_SUB)
#define EXIT_CX 2
#define EXIT_CY 0
#define EXIT_X 16
#define EXIT_Y 192
#define EXIT_W 16
#define EXIT_H 32

static int *row_x;
static int *row_y;
static int *row_w;
static int *row_h;
static int *row_vx;
static int *row_vy;
static int *row_hit_left;
static int *row_hit_right;
static int *row_hit_floor;
static int *row_hit_ceil;
static int *row_jump_was;
static int *row_wall;
static int *row_face;
static int *row_clip;
static int *row_frame;
static int *row_hold;
static int *row_act;
static int *row_vx_was;
static int *row_cell_x;
static int *row_cell_y;
static int row_count;
static int *cam_x;
static int *cam_y;
static int *cam_vx;
static int *cam_vy;
static int *cam_ground;
static int *life;
static int *dead_hold;
static int *cp_cell_x;
static int *cp_x;
static int *cp_cell_y;
static int *cp_y;
static int *solid_cx;
static int *solid_cy;
static int *solid_x;
static int *solid_y;
static int *solid_w;
static int *solid_h;
static int solid_count;

/* [range_overlaps] Devolve 1 se [a0, a1) cruza [b0, b1). Aresta encostada não conta. */
static int range_overlaps(int a0, int a1, int b0, int b1)
{
    return a0 < b1 && b0 < a1;
}

/* [compose_sub] Junta a célula e o local no subpixel do mundo. */
static int compose_sub(int cell, int local)
{
    return cell * WORLD_SPAN + local;
}

/* [split_sub] Separa o subpixel composto na célula e no local de [0, 240<<8). */
static void split_sub(int composed, int *cell, int *local)
{
    int next;

    next = composed / WORLD_SPAN;
    if (composed < 0 && composed % WORLD_SPAN != 0) {
        next -= 1;
    }
    *cell = next;
    *local = composed - next * WORLD_SPAN;
}

/* [solid_box] Escreve o AABB index em subpixels do mundo. */
static void solid_box(int index, int *left, int *top, int *right, int *bottom)
{
    int x;
    int y;

    x = solid_cx[index] * WORLD_VIEW + solid_x[index];
    y = solid_cy[index] * WORLD_VIEW + solid_y[index];
    *left = x << WORLD_SUB;
    *top = y << WORLD_SUB;
    *right = (x + solid_w[index]) << WORLD_SUB;
    *bottom = (y + solid_h[index]) << WORLD_SUB;
}

/* [axis_out] Devolve a posição que sai pela face mais curta. side recebe 1 na mínima e -1 na máxima. */
static int axis_out(int pos, int size, int min_edge, int max_edge, int *side)
{
    int into_min;
    int into_max;

    into_min = (pos + size) - min_edge;
    into_max = max_edge - pos;
    if (into_min <= into_max) {
        *side = 1;
        return min_edge - size;
    }
    *side = -1;
    return max_edge;
}

/* [body_embed] Tira best do sólido mais raso neste eixo e repete. hit fica se já estiver fora. */
static void body_embed(int horizontal, int fixed, int fixed_size, int move_size, int *best, int *hit)
{
    int pass;
    int i;
    int found;
    int pen_best;
    int out_best;
    int side_best;
    int left;
    int top;
    int right;
    int bottom;
    int min_edge;
    int max_edge;
    int out;
    int side;
    int pen;

    for (pass = 0; pass < solid_count; pass++) {
        found = 0;
        pen_best = 0;
        out_best = *best;
        side_best = 0;
        for (i = 0; i < solid_count; i++) {
            if (solid_w[i] <= 0 || solid_h[i] <= 0) {
                continue;
            }
            solid_box(i, &left, &top, &right, &bottom);
            if (horizontal) {
                if (!range_overlaps(fixed, fixed + fixed_size, top, bottom)) {
                    continue;
                }
                if (!range_overlaps(*best, *best + move_size, left, right)) {
                    continue;
                }
                min_edge = left;
                max_edge = right;
            } else {
                if (!range_overlaps(fixed, fixed + fixed_size, left, right)) {
                    continue;
                }
                if (!range_overlaps(*best, *best + move_size, top, bottom)) {
                    continue;
                }
                min_edge = top;
                max_edge = bottom;
            }
            out = axis_out(*best, move_size, min_edge, max_edge, &side);
            pen = side > 0 ? (*best + move_size) - min_edge : max_edge - *best;
            if (!found || pen < pen_best) {
                found = 1;
                pen_best = pen;
                out_best = out;
                side_best = side;
            }
        }
        if (!found) {
            return;
        }
        *best = out_best;
        *hit = side_best;
    }
}

/* [body_latched] Devolve 1 se o latch de parede segura o corpo. */
static int body_latched(void)
{
    return row_wall[0] != 0;
}

/* [body_run] Acelera ou atrita vx. O latch e o sangramento acima do teto de corrida ignoram o eixo. */
static void body_run(const actions *input)
{
    int vx;

    if (body_latched()) {
        return;
    }
    vx = row_vx[0];
    if (vx > WORLD_RUN_MAX) {
        vx -= WORLD_RUN_ACCEL;
        if (vx < WORLD_RUN_MAX) {
            vx = WORLD_RUN_MAX;
        }
    } else if (vx < -WORLD_RUN_MAX) {
        vx += WORLD_RUN_ACCEL;
        if (vx > -WORLD_RUN_MAX) {
            vx = -WORLD_RUN_MAX;
        }
    } else {
        if (input->move_left && !input->move_right) {
            vx -= WORLD_RUN_ACCEL;
        } else if (input->move_right && !input->move_left) {
            vx += WORLD_RUN_ACCEL;
        } else if (vx > 0) {
            vx -= WORLD_RUN_ACCEL;
            if (vx < 0) {
                vx = 0;
            }
        } else if (vx < 0) {
            vx += WORLD_RUN_ACCEL;
            if (vx > 0) {
                vx = 0;
            }
        }
        if (vx > WORLD_RUN_MAX) {
            vx = WORLD_RUN_MAX;
        } else if (vx < -WORLD_RUN_MAX) {
            vx = -WORLD_RUN_MAX;
        }
    }
    row_vx[0] = vx;
}

/* [body_gravity] Soma a gravidade em vy e limita a queda. */
static void body_gravity(const actions *input)
{
    int vy;

    vy = row_vy[0];
    if (vy < 0 && input->jump) {
        vy += WORLD_GRAVITY_RISE;
    } else {
        vy += WORLD_GRAVITY_FALL;
    }
    if (vy > WORLD_FALL_MAX) {
        vy = WORLD_FALL_MAX;
    }
    row_vy[0] = vy;
}

/* [body_slide] Limita a queda enquanto [body_latched] vale. */
static void body_slide(void)
{
    if (body_latched() && row_vy[0] > WORLD_SLIDE_MAX) {
        row_vy[0] = WORLD_SLIDE_MAX;
    }
}

/* [body_stop_x] Devolve o x composto na primeira face vertical cruzada. hit recebe -1, 1 ou 0. */
static int body_stop_x(int x0, int dest, int y, int bw, int bh, int *hit)
{
    int best;
    int i;
    int left;
    int top;
    int right;
    int bottom;
    int face;

    *hit = 0;
    best = dest;
    for (i = 0; i < solid_count; i++) {
        if (solid_w[i] <= 0 || solid_h[i] <= 0) {
            continue;
        }
        solid_box(i, &left, &top, &right, &bottom);
        if (!range_overlaps(y, y + bh, top, bottom)) {
            continue;
        }
        if (dest > x0) {
            face = left - bw;
            if (x0 + bw <= left && dest + bw > left && face < best) {
                best = face;
                *hit = 1;
            }
        } else if (dest < x0) {
            face = right;
            if (x0 >= right && dest < right && face > best) {
                best = face;
                *hit = -1;
            }
        }
    }
    body_embed(1, y, bh, bw, &best, hit);
    return best;
}

/* [body_stop_y] Devolve o y composto na primeira face horizontal cruzada. hit recebe -1 teto, 1 chão ou 0. */
static int body_stop_y(int y0, int dest, int x, int bw, int bh, int *hit)
{
    int best;
    int i;
    int left;
    int top;
    int right;
    int bottom;
    int face;

    *hit = 0;
    best = dest;
    for (i = 0; i < solid_count; i++) {
        if (solid_w[i] <= 0 || solid_h[i] <= 0) {
            continue;
        }
        solid_box(i, &left, &top, &right, &bottom);
        if (!range_overlaps(x, x + bw, left, right)) {
            continue;
        }
        if (dest > y0) {
            face = top - bh;
            if (y0 + bh <= top && dest + bh > top && face < best) {
                best = face;
                *hit = 1;
            }
        } else if (dest < y0) {
            face = bottom;
            if (y0 >= bottom && dest < bottom && face > best) {
                best = face;
                *hit = -1;
            }
        }
    }
    body_embed(0, x, bw, bh, &best, hit);
    return best;
}

/* [body_on_latch] Devolve 1 se um subpixel contra o muro ainda cruza uma face. */
static int body_on_latch(void)
{
    int side;
    int x;
    int y;
    int bw;
    int bh;
    int probe;
    int i;
    int left;
    int top;
    int right;
    int bottom;

    side = row_wall[0];
    if (side == 0) {
        return 0;
    }
    x = compose_sub(row_cell_x[0], row_x[0]);
    y = compose_sub(row_cell_y[0], row_y[0]);
    bw = row_w[0] << WORLD_SUB;
    bh = row_h[0] << WORLD_SUB;
    if (side < 0) {
        probe = x - 1;
    } else {
        probe = x + 1;
    }
    for (i = 0; i < solid_count; i++) {
        if (solid_w[i] <= 0 || solid_h[i] <= 0) {
            continue;
        }
        solid_box(i, &left, &top, &right, &bottom);
        if (range_overlaps(probe, probe + bw, left, right) &&
            range_overlaps(y, y + bh, top, bottom)) {
            return 1;
        }
    }
    return 0;
}

/* [body_solve_x] Soma vx, projeta nas faces verticais, grava o latch e separa a célula. */
static void body_solve_x(const actions *input)
{
    int x0;
    int y0;
    int bw;
    int bh;
    int hit;
    int x;

    x0 = compose_sub(row_cell_x[0], row_x[0]);
    y0 = compose_sub(row_cell_y[0], row_y[0]);
    bw = row_w[0] << WORLD_SUB;
    bh = row_h[0] << WORLD_SUB;
    row_hit_left[0] = 0;
    row_hit_right[0] = 0;
    x = body_stop_x(x0, x0 + row_vx[0], y0, bw, bh, &hit);
    if (hit != 0) {
        row_vx[0] = 0;
        if (hit < 0) {
            row_hit_left[0] = 1;
            if (!input->move_down) {
                row_wall[0] = -1;
            }
        } else {
            row_hit_right[0] = 1;
            if (!input->move_down) {
                row_wall[0] = 1;
            }
        }
    }
    split_sub(x, &row_cell_x[0], &row_x[0]);
}

/* [cam_feet] Devolve o y dos pés em subpixels do mundo. */
static int cam_feet(void)
{
    return compose_sub(row_cell_y[0], row_y[0]) + (row_h[0] << WORLD_SUB);
}

/* [cam_note_ground] Grava a linha dos pés quando [body_solve_y] marca o chão. */
static void cam_note_ground(void)
{
    if (row_hit_floor[0]) {
        *cam_ground = cam_feet();
    }
}

/* [body_solve_y] Soma vy, projeta nas faces horizontais, separa a célula, solta o latch no chão ou na saída do muro, e o chão chama [cam_note_ground]. */
static void body_solve_y(void)
{
    int x0;
    int y0;
    int bw;
    int bh;
    int hit;
    int y;

    x0 = compose_sub(row_cell_x[0], row_x[0]);
    y0 = compose_sub(row_cell_y[0], row_y[0]);
    bw = row_w[0] << WORLD_SUB;
    bh = row_h[0] << WORLD_SUB;
    row_hit_floor[0] = 0;
    row_hit_ceil[0] = 0;
    y = body_stop_y(y0, y0 + row_vy[0], x0, bw, bh, &hit);
    split_sub(y, &row_cell_y[0], &row_y[0]);
    if (hit > 0) {
        row_vy[0] = 0;
        row_hit_floor[0] = 1;
        row_wall[0] = 0;
    } else if (hit < 0) {
        row_vy[0] = 0;
        row_hit_ceil[0] = 1;
    }
    if (row_wall[0] != 0 && !body_on_latch()) {
        row_wall[0] = 0;
    }
    cam_note_ground();
}

/* [body_jump] Impulsiona no chão, ou na diagonal se o latch de [body_solve_x] vale, e grava o ato do tick. */
static void body_jump(const actions *input)
{
    int edge;

    edge = input->jump && !row_jump_was[0];
    row_act[0] = BODY_ACT_NONE;
    if (edge && row_hit_floor[0]) {
        row_vy[0] = WORLD_JUMP_VY;
        row_hit_floor[0] = 0;
        row_act[0] = BODY_ACT_FLOOR;
    } else if (edge && body_latched()) {
        if (row_wall[0] < 0) {
            row_vx[0] = WORLD_WALL_JUMP;
        } else {
            row_vx[0] = -WORLD_WALL_JUMP;
        }
        row_vy[0] = -WORLD_WALL_JUMP;
        row_wall[0] = 0;
        row_act[0] = BODY_ACT_WALL;
    }
    row_jump_was[0] = input->jump ? 1 : 0;
}

/* [body_speed] Devolve o módulo de vx. */
static int body_speed(void)
{
    if (row_vx[0] < 0) {
        return -row_vx[0];
    }
    return row_vx[0];
}

/* [body_turned] Devolve 1 se vx trocou de sinal com os dois valores diferentes de zero. */
static int body_turned(void)
{
    int prev;
    int vx;

    prev = row_vx_was[0];
    vx = row_vx[0];
    if (prev == 0 || vx == 0) {
        return 0;
    }
    if ((prev > 0 && vx < 0) || (prev < 0 && vx > 0)) {
        return 1;
    }
    return 0;
}

/* [body_face_now] A face olha para longe do muro no latch; senão segue o sinal de vx. */
static void body_face_now(void)
{
    if (body_latched()) {
        row_face[0] = -row_wall[0];
        return;
    }
    if (row_vx[0] > 0) {
        row_face[0] = 1;
    } else if (row_vx[0] < 0) {
        row_face[0] = -1;
    }
}

/* [body_frame_ms] Devolve os ms do quadro, no tempo dos showcases. Idle não avança. */
static int body_frame_ms(int clip, int frame)
{
    if (clip == BODY_CLIP_WALK || clip == BODY_CLIP_JUMP || clip == BODY_CLIP_WALL) {
        return 120;
    }
    if (clip == BODY_CLIP_JOG) {
        return 80;
    }
    if (clip == BODY_CLIP_JUMP_TURN && frame > 0) {
        return 120;
    }
    if (clip == BODY_CLIP_IDLE) {
        return 0;
    }
    return 60;
}

/* [body_gait] Devolve 1 se o clip é walk, jog ou run. */
static int body_gait(int clip)
{
    return clip == BODY_CLIP_WALK || clip == BODY_CLIP_JOG || clip == BODY_CLIP_RUN;
}

/* [body_oneshot] Devolve 1 se o clip toca uma vez e solta. */
static int body_oneshot(int clip)
{
    return clip == BODY_CLIP_RUN_TURN || clip == BODY_CLIP_JUMP_TURN || clip == BODY_CLIP_DASH;
}

/* [body_air_clip] Devolve 1 se o pouso cancela o clip. */
static int body_air_clip(int clip)
{
    return clip == BODY_CLIP_JUMP || clip == BODY_CLIP_JUMP_TURN || clip == BODY_CLIP_DASH;
}

/* [body_restart] Começa o clip no quadro 0 com o hold zerado. */
static void body_restart(int clip)
{
    row_clip[0] = clip;
    row_frame[0] = 0;
    row_hold[0] = 0;
}

/* [body_enter] Troca o clip. Walk, jog e run conservam o quadro. */
static void body_enter(int clip)
{
    int keep;

    if (row_clip[0] == clip) {
        return;
    }
    keep = body_gait(row_clip[0]) && body_gait(clip);
    if (!keep) {
        body_restart(clip);
        return;
    }
    row_clip[0] = clip;
    row_hold[0] = 0;
    if (row_frame[0] < 0 || row_frame[0] >= body_sheet_frames(clip)) {
        row_frame[0] = 0;
    }
}

/* [body_advance_due] Avança o quadro quando o hold chega em [body_frame_ms]. O resto fica. Devolve 1 se o one-shot terminou. */
static int body_advance_due(void)
{
    int clip;
    int frames;
    int need;

    clip = row_clip[0];
    frames = body_sheet_frames(clip);
    need = body_frame_ms(clip, row_frame[0]);
    if (frames <= 0 || need <= 0 || row_hold[0] < need) {
        return 0;
    }
    row_hold[0] -= need;
    if (row_frame[0] + 1 < frames) {
        row_frame[0] += 1;
        return 0;
    }
    if (clip == BODY_CLIP_JUMP) {
        return 0;
    }
    if (body_gait(clip) || clip == BODY_CLIP_WALL) {
        row_frame[0] = 0;
        return 0;
    }
    return 1;
}

/* [body_commit] Soma 16 ms no hold do clip que ficou para o desenho. */
static void body_commit(void)
{
    int need;

    need = body_frame_ms(row_clip[0], row_frame[0]);
    if (need <= 0) {
        return;
    }
    row_hold[0] += BODY_TICK_MS;
}

/* [body_select_ground] Escolhe idle, walk, jog, run ou a virada no chão. */
static void body_select_ground(void)
{
    int speed;
    int clip;

    if (body_turned()) {
        body_restart(BODY_CLIP_RUN_TURN);
        return;
    }
    speed = body_speed();
    if (speed >= WORLD_RUN_MAX) {
        clip = BODY_CLIP_RUN;
    } else if (speed >= WORLD_JOG_MIN) {
        clip = BODY_CLIP_JOG;
    } else if (row_vx[0] != 0) {
        clip = BODY_CLIP_WALK;
    } else {
        clip = BODY_CLIP_IDLE;
    }
    if (clip == BODY_CLIP_IDLE) {
        if (row_clip[0] == BODY_CLIP_IDLE) {
            return;
        }
        body_restart(BODY_CLIP_IDLE);
        return;
    }
    if (row_clip[0] == clip) {
        body_advance_due();
        return;
    }
    body_enter(clip);
}

/* [body_select_air] Continua o jump, segura o último quadro depois do dash, ou começa a virada. */
static void body_select_air(int dash_done)
{
    int last;

    if (body_turned()) {
        body_restart(BODY_CLIP_JUMP_TURN);
        return;
    }
    if (dash_done) {
        last = body_sheet_frames(BODY_CLIP_JUMP) - 1;
        if (last < 0) {
            last = 0;
        }
        row_clip[0] = BODY_CLIP_JUMP;
        row_frame[0] = last;
        row_hold[0] = 0;
        return;
    }
    if (row_clip[0] == BODY_CLIP_JUMP) {
        body_advance_due();
        return;
    }
    body_restart(BODY_CLIP_JUMP);
}

/* [body_pose] Escolhe o clip no fim do tick. O latch ganha, o pulo da parede começa o dash, e o one-shot aberto avança. */
static void body_pose(void)
{
    int grounded;
    int done;

    body_face_now();
    grounded = row_hit_floor[0] != 0;
    if (body_latched()) {
        if (row_clip[0] == BODY_CLIP_WALL) {
            body_advance_due();
        } else {
            body_restart(BODY_CLIP_WALL);
        }
    } else if (row_act[0] == BODY_ACT_WALL) {
        body_restart(BODY_CLIP_DASH);
    } else if (grounded && body_air_clip(row_clip[0])) {
        body_select_ground();
    } else if (body_oneshot(row_clip[0])) {
        done = body_advance_due();
        if (done) {
            if (grounded) {
                body_select_ground();
            } else {
                body_select_air(row_clip[0] == BODY_CLIP_DASH);
            }
        }
    } else if (grounded) {
        body_select_ground();
    } else {
        body_select_air(0);
    }
    body_commit();
    row_vx_was[0] = row_vx[0];
}

/* [cam_focus_x] Devolve a origem x da View que põe o centro do corpo no meio horizontal. */
static int cam_focus_x(void)
{
    int center;

    center = compose_sub(row_cell_x[0], row_x[0]) + ((row_w[0] / 2) << WORLD_SUB);
    return center - ((WORLD_VIEW / 2) << WORLD_SUB);
}

/* [cam_focus_y] Devolve a origem y da View que deixa a linha de [cam_ground] a WORLD_CAM_FLOOR px da base. */
static int cam_focus_y(void)
{
    return *cam_ground - ((WORLD_VIEW - WORLD_CAM_FLOOR) << WORLD_SUB);
}

/* [cam_axis] Integra um eixo da mola até o alvo. */
static void cam_axis(int *pos, int *vel, int target)
{
    *vel += (target - *pos) / WORLD_CAM_STIFF;
    *vel = *vel * WORLD_CAM_DAMP_NUM / WORLD_CAM_DAMP_DEN;
    *pos += *vel;
}

/* [cam_spring] Segue o meio horizontal e a linha do chão. O pulo não levanta a View; a queda abaixo da linha sim. */
static void cam_spring(void)
{
    int feet;

    feet = cam_feet();
    if (feet > *cam_ground) {
        *cam_ground = feet;
    }
    cam_axis(cam_x, cam_vx, cam_focus_x());
    cam_axis(cam_y, cam_vy, cam_focus_y());
}

/* [body_place_at] Põe o corpo no local em subpixels, zerado, com a pose idle virada à direita, e encosta a câmera na linha dos pés com a mola parada. */
static void body_place_at(int cell_x, int x, int cell_y, int y)
{
    row_cell_x[0] = cell_x;
    row_cell_y[0] = cell_y;
    row_x[0] = x;
    row_y[0] = y;
    row_w[0] = WORLD_BODY_W;
    row_h[0] = WORLD_BODY_H;
    row_vx[0] = 0;
    row_vy[0] = 0;
    row_hit_left[0] = 0;
    row_hit_right[0] = 0;
    row_hit_floor[0] = 0;
    row_hit_ceil[0] = 0;
    row_jump_was[0] = 0;
    row_wall[0] = 0;
    row_face[0] = 1;
    row_clip[0] = BODY_CLIP_IDLE;
    row_frame[0] = 0;
    row_hold[0] = 0;
    row_act[0] = BODY_ACT_NONE;
    row_vx_was[0] = 0;
    *cam_ground = cam_feet();
    *cam_x = cam_focus_x();
    *cam_y = cam_focus_y();
    *cam_vx = 0;
    *cam_vy = 0;
}

/* [body_place] Põe o corpo no spawn com [body_place_at]. */
static void body_place(void)
{
    body_place_at(SPAWN_CELL_X, SPAWN_X, SPAWN_CELL_Y, SPAWN_Y);
}

/* [mark_spawn] Grava o spawn como checkpoint. */
static void mark_spawn(void)
{
    *cp_cell_x = SPAWN_CELL_X;
    *cp_x = SPAWN_X;
    *cp_cell_y = SPAWN_CELL_Y;
    *cp_y = SPAWN_Y;
}

/* [solid_put] Grava o AABB index na célula e no local em pixels. */
static void solid_put(int index, int cell_x, int cell_y, int x, int y, int w, int h)
{
    solid_cx[index] = cell_x;
    solid_cy[index] = cell_y;
    solid_x[index] = x;
    solid_y[index] = y;
    solid_w[index] = w;
    solid_h[index] = h;
}

/* [solid_course] Grava os dois chãos, os muros e as seis plataformas. */
static void solid_course(void)
{
    solid_put(0, 0, 0, 0, GROUND_Y, WORLD_VIEW, GROUND_H);
    solid_put(1, 0, 0, 80, 16, WALL_W, WALL_H);
    solid_put(2, 0, 0, 144, 16, WALL_W, WALL_H);
    solid_put(3, 0, 0, 8, 184, 64, PLAY_H);
    solid_put(4, 0, 0, 8, 144, 56, PLAY_H);
    solid_put(5, 0, 0, 12, 104, 48, PLAY_H);
    solid_put(6, 0, 0, 168, 184, 64, PLAY_H);
    solid_put(7, 0, 0, 168, 144, 56, PLAY_H);
    solid_put(8, 0, 0, 176, 104, 48, PLAY_H);
    solid_put(9, 1, 0, 48, GROUND_Y, WORLD_VIEW, GROUND_H);
    solid_count = SOLID_USED;
}

/* [solid_load] Grava o percurso de [solid_course] na slice de [arena_level]. Devolve 0 se couber. */
static int solid_load(void)
{
    arena_slice slice;
    int *rows;
    size_t need;

    if (SOLID_USED > SOLID_CAP) {
        return 1;
    }
    slice = arena_level();
    need = (size_t)SOLID_CAP * (size_t)SOLID_COLS * sizeof(int);
    if (slice.data == 0 || slice.size < need) {
        return 1;
    }
    rows = (int *)slice.data;
    solid_cx = rows;
    solid_cy = rows + SOLID_CAP;
    solid_x = rows + SOLID_CAP * 2;
    solid_y = rows + SOLID_CAP * 3;
    solid_w = rows + SOLID_CAP * 4;
    solid_h = rows + SOLID_CAP * 5;
    solid_course();
    return 0;
}

/* [world_init] Monta o corpo no spawn, o percurso fixo na slice de [arena_level], o checkpoint no spawn e a câmera com a mola parada. Devolve 0 se [arena_ready] servir. */
int world_init(void)
{
    arena_slice slice;
    int *rows;
    size_t need;

    if (!arena_ready()) {
        return 1;
    }
    slice = arena_world();
    need = (size_t)WORLD_CAP * (size_t)WORLD_COLS * sizeof(int) + (size_t)WORLD_TAIL * sizeof(int);
    if (slice.data == 0 || slice.size < need) {
        return 1;
    }
    rows = (int *)slice.data;
    row_x = rows;
    row_y = rows + WORLD_CAP;
    row_w = rows + WORLD_CAP * 2;
    row_h = rows + WORLD_CAP * 3;
    row_vx = rows + WORLD_CAP * 4;
    row_vy = rows + WORLD_CAP * 5;
    row_hit_left = rows + WORLD_CAP * 6;
    row_hit_right = rows + WORLD_CAP * 7;
    row_hit_floor = rows + WORLD_CAP * 8;
    row_hit_ceil = rows + WORLD_CAP * 9;
    row_jump_was = rows + WORLD_CAP * 10;
    row_wall = rows + WORLD_CAP * 11;
    row_face = rows + WORLD_CAP * 14;
    row_clip = rows + WORLD_CAP * 15;
    row_frame = rows + WORLD_CAP * 16;
    row_hold = rows + WORLD_CAP * 17;
    row_act = rows + WORLD_CAP * 18;
    row_vx_was = rows + WORLD_CAP * 19;
    row_cell_x = rows + WORLD_CAP * 12;
    row_cell_y = rows + WORLD_CAP * 13;
    cam_x = rows + WORLD_CAP * WORLD_COLS;
    cam_y = cam_x + 1;
    cam_vx = cam_y + 1;
    cam_vy = cam_vx + 1;
    cam_ground = cam_vy + 1;
    life = cam_ground + 1;
    dead_hold = life + 1;
    cp_cell_x = dead_hold + 1;
    cp_x = cp_cell_x + 1;
    cp_cell_y = cp_x + 1;
    cp_y = cp_cell_y + 1;
    *life = LIFE_PLAY;
    *dead_hold = 0;
    row_count = 1;
    if (solid_load() != 0) {
        return 1;
    }
    mark_spawn();
    body_place();
    return 0;
}

/* [world_reset] Recarrega o percurso, devolve o checkpoint ao spawn, põe o corpo lá e encosta a câmera. */
void world_reset(void)
{
    if (row_count < 1) {
        return;
    }
    solid_course();
    mark_spawn();
    *life = LIFE_PLAY;
    *dead_hold = 0;
    body_place();
}

/* [mark_box] Escreve a marca em subpixels do mundo. */
static void mark_box(int cell_x, int cell_y, int x, int y, int w, int h, int *left, int *top, int *right, int *bottom)
{
    int wx;
    int wy;

    wx = cell_x * WORLD_VIEW + x;
    wy = cell_y * WORLD_VIEW + y;
    *left = wx << WORLD_SUB;
    *top = wy << WORLD_SUB;
    *right = (wx + w) << WORLD_SUB;
    *bottom = (wy + h) << WORLD_SUB;
}

/* [body_hits] Devolve 1 se o AABB cruza a marca. Aresta encostada não conta, como [range_overlaps]. */
static int body_hits(int cell_x, int cell_y, int x, int y, int w, int h)
{
    int left;
    int top;
    int right;
    int bottom;
    int bx;
    int by;
    int bw;
    int bh;

    mark_box(cell_x, cell_y, x, y, w, h, &left, &top, &right, &bottom);
    bx = compose_sub(row_cell_x[0], row_x[0]);
    by = compose_sub(row_cell_y[0], row_y[0]);
    bw = row_w[0] << WORLD_SUB;
    bh = row_h[0] << WORLD_SUB;
    return range_overlaps(bx, bx + bw, left, right) && range_overlaps(by, by + bh, top, bottom);
}

/* [body_fallen] Devolve 1 se os pés passaram de PIT_Y. */
static int body_fallen(void)
{
    return cam_feet() > (PIT_Y << WORLD_SUB);
}

/* [world_tick] Integra um tick fixo. O pit e o spike abrem 16 ticks de morte e depois [body_place_at] no checkpoint. A saída congela até [world_reset]. A mola e [body_pose] só correm vivo. */
void world_tick(const actions *input)
{
    if (row_count < 1 || input == 0) {
        return;
    }
    if (*life == LIFE_DONE) {
        return;
    }
    if (*life == LIFE_DYING) {
        *dead_hold -= 1;
        if (*dead_hold <= 0) {
            body_place_at(*cp_cell_x, *cp_x, *cp_cell_y, *cp_y);
            *life = LIFE_PLAY;
            row_jump_was[0] = input->jump ? 1 : 0;
        }
        return;
    }
    if (input->move_down) {
        row_wall[0] = 0;
    }
    body_run(input);
    body_gravity(input);
    body_slide();
    body_solve_x(input);
    body_solve_y();
    if (body_fallen() || body_hits(SPIKE_CX, SPIKE_CY, SPIKE_X, SPIKE_Y, SPIKE_W, SPIKE_H)) {
        *life = LIFE_DYING;
        *dead_hold = DEATH_HOLD;
        return;
    }
    if (body_hits(CP_CX, CP_CY, CP_X, CP_Y, CP_W, CP_H)) {
        *cp_cell_x = CP_CX;
        *cp_x = CP_STAND_X;
        *cp_cell_y = CP_CY;
        *cp_y = CP_STAND_Y;
    }
    if (body_hits(EXIT_CX, EXIT_CY, EXIT_X, EXIT_Y, EXIT_W, EXIT_H)) {
        row_vx[0] = 0;
        row_vy[0] = 0;
        *life = LIFE_DONE;
        return;
    }
    body_jump(input);
    cam_spring();
    body_pose();
}

/* [world_body_pose] Escreve o clip, o quadro e a face. Face -1 pede o flip. */
void world_body_pose(int *clip, int *frame, int *face)
{
    if (row_count < 1) {
        *clip = BODY_CLIP_IDLE;
        *frame = 0;
        *face = 1;
        return;
    }
    *clip = row_clip[0];
    *frame = row_frame[0];
    *face = row_face[0];
}

/* [world_rect] Escreve a posição no mundo, em pixels, e o tamanho do Rect. */
void world_rect(int *x, int *y, int *w, int *h)
{
    if (row_count < 1) {
        *x = 0;
        *y = 0;
        *w = 0;
        *h = 0;
        return;
    }
    *x = row_cell_x[0] * WORLD_VIEW + (row_x[0] >> WORLD_SUB);
    *y = row_cell_y[0] * WORLD_VIEW + (row_y[0] >> WORLD_SUB);
    *w = row_w[0];
    *h = row_h[0];
}

/* [world_camera] Escreve a origem da View em pixels do mundo. */
void world_camera(int *x, int *y)
{
    if (cam_x == 0) {
        *x = 0;
        *y = 0;
        return;
    }
    *x = *cam_x >> WORLD_SUB;
    *y = *cam_y >> WORLD_SUB;
}

/* [world_solid_count] Devolve quantos AABB estáticos a slice guarda. */
int world_solid_count(void)
{
    return solid_count;
}

/* [world_solid_rect] Escreve o AABB index em pixels do mundo. Fora da contagem, zera a saída. */
void world_solid_rect(int index, int *x, int *y, int *w, int *h)
{
    if (index < 0 || index >= solid_count) {
        *x = 0;
        *y = 0;
        *w = 0;
        *h = 0;
        return;
    }
    *x = solid_cx[index] * WORLD_VIEW + solid_x[index];
    *y = solid_cy[index] * WORLD_VIEW + solid_y[index];
    *w = solid_w[index];
    *h = solid_h[index];
}

/* [mark_rect] Escreve a marca em pixels do mundo. */
static void mark_rect(int cell_x, int cell_y, int x, int y, int w, int h, int *ox, int *oy, int *ow, int *oh)
{
    *ox = cell_x * WORLD_VIEW + x;
    *oy = cell_y * WORLD_VIEW + y;
    *ow = w;
    *oh = h;
}

/* [world_spike_rect] Escreve o spike em pixels do mundo. */
void world_spike_rect(int *x, int *y, int *w, int *h)
{
    mark_rect(SPIKE_CX, SPIKE_CY, SPIKE_X, SPIKE_Y, SPIKE_W, SPIKE_H, x, y, w, h);
}

/* [world_checkpoint_rect] Escreve o volume do checkpoint em pixels do mundo. */
void world_checkpoint_rect(int *x, int *y, int *w, int *h)
{
    mark_rect(CP_CX, CP_CY, CP_X, CP_Y, CP_W, CP_H, x, y, w, h);
}

/* [world_exit_rect] Escreve a saída em pixels do mundo. */
void world_exit_rect(int *x, int *y, int *w, int *h)
{
    mark_rect(EXIT_CX, EXIT_CY, EXIT_X, EXIT_Y, EXIT_W, EXIT_H, x, y, w, h);
}
