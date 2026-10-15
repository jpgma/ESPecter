#include "actions.h"

#include <SDL2/SDL.h>
#include <string.h>

#define PAD_DEADZONE 8000

static SDL_GameController *pad;
static int started;

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

/* [actions_capture] Lê o axis e os botões do pad que [pad_open] achou. */
void actions_capture(actions *out)
{
    int axis;

    memset(out, 0, sizeof(*out));
    pad_open();
    if (pad == NULL) {
        return;
    }
    SDL_GameControllerUpdate();
    axis = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    if (axis < -PAD_DEADZONE || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
        out->move_left = 1;
    }
    if (axis > PAD_DEADZONE || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
        out->move_right = 1;
    }
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) {
        out->jump = 1;
        out->confirm = 1;
    }
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)) {
        out->pause = 1;
    }
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B)) {
        out->cancel = 1;
    }
}
