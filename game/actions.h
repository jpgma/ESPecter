#ifndef ESPECTER_ACTIONS_H
#define ESPECTER_ACTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct actions {
    int dpad_up;
    int dpad_down;
    int dpad_left;
    int dpad_right;
    int face_y;
    int face_x;
    int face_a;
    int face_b;
    int shoulder_l;
    int trigger_l2;
    int trigger_r2;
    int shoulder_r;
    int select;
    int home;
    int start;
    int move_up;
    int move_down;
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
