#include "actions.h"

#include <SDL2/SDL.h>
#include <string.h>

#define PAD_DEADZONE 8000

static SDL_GameController *pad;
static int started;
static int keys_started;

/* [set_held] Liga o campo quando a tecla ou o botão está pressionado. */
static void set_held(int *field, int down)
{
    if (down) {
        *field = 1;
    }
}

/* [keys_ready] Garante os eventos SDL e atualiza o estado do teclado. */
static void keys_ready(void)
{
    if (!keys_started) {
        SDL_InitSubSystem(SDL_INIT_EVENTS);
        keys_started = 1;
    }
    SDL_PumpEvents();
}

/* [keys_apply] Copia o teclado na posição do Q36 para os botões. */
static void keys_apply(const Uint8 *keys, actions *out)
{
    set_held(&out->dpad_up, keys[SDL_SCANCODE_W]);
    set_held(&out->dpad_left, keys[SDL_SCANCODE_A]);
    set_held(&out->dpad_down, keys[SDL_SCANCODE_S]);
    set_held(&out->dpad_right, keys[SDL_SCANCODE_D]);
    set_held(&out->face_y, keys[SDL_SCANCODE_I]);
    set_held(&out->face_x, keys[SDL_SCANCODE_J]);
    set_held(&out->face_a, keys[SDL_SCANCODE_K]);
    set_held(&out->face_b, keys[SDL_SCANCODE_L]);
    set_held(&out->shoulder_l, keys[SDL_SCANCODE_Q]);
    set_held(&out->trigger_l2, keys[SDL_SCANCODE_E]);
    set_held(&out->trigger_r2, keys[SDL_SCANCODE_U]);
    set_held(&out->shoulder_r, keys[SDL_SCANCODE_O]);
    set_held(&out->select, keys[SDL_SCANCODE_F]);
    set_held(&out->home, keys[SDL_SCANCODE_G]);
    set_held(&out->start, keys[SDL_SCANCODE_H]);
}

/* [pad_open] Abre o primeiro pad SDL que estiver ligado. */
static void pad_open(void)
{
    int i;
    int count;

    if (!started) {
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
        started = 1;
    }
    if (pad != NULL && SDL_GameControllerGetAttached(pad)) {
        return;
    }
    if (pad != NULL) {
        SDL_GameControllerClose(pad);
        pad = NULL;
    }
    count = SDL_NumJoysticks();
    for (i = 0; i < count; i++) {
        if (SDL_IsGameController(i)) {
            pad = SDL_GameControllerOpen(i);
            if (pad != NULL) {
                return;
            }
        }
    }
}

/* [pad_apply] Copia o axis e os botões do pad que [pad_open] achou. */
static void pad_apply(actions *out)
{
    int axis;

    SDL_GameControllerUpdate();
    axis = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    if (axis < -PAD_DEADZONE) {
        out->move_left = 1;
    }
    if (axis > PAD_DEADZONE) {
        out->move_right = 1;
    }
    set_held(&out->dpad_up, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP));
    set_held(&out->dpad_down, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN));
    set_held(&out->dpad_left, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT));
    set_held(&out->dpad_right, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
    set_held(&out->face_y, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y));
    set_held(&out->face_x, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X));
    set_held(&out->face_a, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A));
    set_held(&out->face_b, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B));
    set_held(&out->shoulder_l, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
    set_held(&out->shoulder_r, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
    axis = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    if (axis > PAD_DEADZONE) {
        out->trigger_l2 = 1;
    }
    axis = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    if (axis > PAD_DEADZONE) {
        out->trigger_r2 = 1;
    }
    set_held(&out->select, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK));
    set_held(&out->home, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_GUIDE));
    set_held(&out->start, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START));
}

/* [actions_fold] Deriva move, jump, pause, confirm e cancel dos botões. */
static void actions_fold(actions *out)
{
    if (out->dpad_up) {
        out->move_up = 1;
    }
    if (out->dpad_down) {
        out->move_down = 1;
    }
    if (out->dpad_left) {
        out->move_left = 1;
    }
    if (out->dpad_right) {
        out->move_right = 1;
    }
    if (out->face_a) {
        out->jump = 1;
        out->confirm = 1;
    }
    if (out->start) {
        out->pause = 1;
    }
    if (out->face_b) {
        out->cancel = 1;
    }
}

/* [actions_capture] Lê o teclado e o pad que [pad_open] achou. */
void actions_capture(actions *out)
{
    const Uint8 *keys;

    memset(out, 0, sizeof(*out));
    keys_ready();
    keys = SDL_GetKeyboardState(NULL);
    if (keys != NULL) {
        keys_apply(keys, out);
    }
    pad_open();
    if (pad != NULL) {
        pad_apply(out);
    }
    actions_fold(out);
}
