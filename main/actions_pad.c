#include "actions.h"

#include "pad_link.h"

#include <string.h>

/* [set_held] Liga o campo quando o bit do report está ativo. */
static void set_held(int *field, int down)
{
    if (down) {
        *field = 1;
    }
}

/* [hat_cardinal] Devolve 1 se o hat acende esse cardinal, inclusive a diagonal. */
static int hat_cardinal(uint8_t hat, uint8_t cardinal)
{
    if (hat > PAD_HAT_NW) {
        return 0;
    }
    if (hat == cardinal) {
        return 1;
    }
    if (((hat + 1) & 7) == cardinal) {
        return 1;
    }
    if (((cardinal + 1) & 7) == hat) {
        return 1;
    }
    return 0;
}

/* [buttons_apply] Copia os bits do report para os botões da face. */
static void buttons_apply(uint16_t buttons, actions *out)
{
    set_held(&out->face_a, buttons & PAD_BTN_A);
    set_held(&out->face_b, buttons & PAD_BTN_B);
    set_held(&out->face_x, buttons & PAD_BTN_X);
    set_held(&out->face_y, buttons & PAD_BTN_Y);
    set_held(&out->shoulder_l, buttons & PAD_BTN_L);
    set_held(&out->trigger_l2, buttons & PAD_BTN_L2);
    set_held(&out->shoulder_r, buttons & PAD_BTN_R);
    set_held(&out->trigger_r2, buttons & PAD_BTN_R2);
    set_held(&out->select, buttons & PAD_BTN_VIEW);
    set_held(&out->home, buttons & PAD_BTN_HOME);
    set_held(&out->start, buttons & PAD_BTN_MENU);
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

/* [actions_capture] Preenche as actions com o mailbox de [pad_link_read]. */
void actions_capture(actions *out)
{
    pad_view view;

    memset(out, 0, sizeof(*out));
    pad_link_read(&view);
    if (view.link != PAD_LINK_LINKED) {
        return;
    }
    buttons_apply(view.buttons, out);
    set_held(&out->dpad_up, hat_cardinal(view.hat, PAD_HAT_N));
    set_held(&out->dpad_right, hat_cardinal(view.hat, PAD_HAT_E));
    set_held(&out->dpad_down, hat_cardinal(view.hat, PAD_HAT_S));
    set_held(&out->dpad_left, hat_cardinal(view.hat, PAD_HAT_W));
    actions_fold(out);
}
