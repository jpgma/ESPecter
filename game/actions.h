#ifndef ESPECTER_ACTIONS_H
#define ESPECTER_ACTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct actions {
    int move_left;
    int move_right;
    int jump;
    int pause;
    int confirm;
    int cancel;
} actions;

/* [actions_capture] Preenche as actions deste frame. */
void actions_capture(actions *out);

#ifdef __cplusplus
}
#endif

#endif
