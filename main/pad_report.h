#ifndef ESPECTER_PAD_REPORT_H
#define ESPECTER_PAD_REPORT_H

#include <stdint.h>

/* Report do Pad Emu. Três bytes, little-endian: uint16 buttons, uint8 hat.
 * Hat: 0 up, depois no sentido horário, 8 neutro.
 * Service f31a0001-7c4e-4a2b-9e15-6d8f0b2c4a10
 * Report  f31a0002-7c4e-4a2b-9e15-6d8f0b2c4a10
 * BLE_UUID128_INIT guarda a string em little-endian.
 */

#define PAD_REPORT_BYTES 3

#define PAD_BTN_A    (1u << 0)
#define PAD_BTN_B    (1u << 1)
#define PAD_BTN_X    (1u << 2)
#define PAD_BTN_Y    (1u << 3)
#define PAD_BTN_L    (1u << 4)
#define PAD_BTN_L2   (1u << 5)
#define PAD_BTN_R    (1u << 6)
#define PAD_BTN_R2   (1u << 7)
#define PAD_BTN_VIEW (1u << 8)
#define PAD_BTN_MENU (1u << 9)
#define PAD_BTN_HOME (1u << 10)
#define PAD_BTN_MASK 0x07ffu

#define PAD_HAT_N       0
#define PAD_HAT_NE      1
#define PAD_HAT_E       2
#define PAD_HAT_SE      3
#define PAD_HAT_S       4
#define PAD_HAT_SW      5
#define PAD_HAT_W       6
#define PAD_HAT_NW      7
#define PAD_HAT_NEUTRAL 8

#define PAD_SVC_UUID128 \
    0x10, 0x4a, 0x2c, 0x0b, 0x8f, 0x6d, 0x15, 0x9e, \
    0x2b, 0x4a, 0x4e, 0x7c, 0x01, 0x00, 0x1a, 0xf3

#define PAD_CHR_UUID128 \
    0x10, 0x4a, 0x2c, 0x0b, 0x8f, 0x6d, 0x15, 0x9e, \
    0x2b, 0x4a, 0x4e, 0x7c, 0x02, 0x00, 0x1a, 0xf3

enum {
    PAD_LINK_DOWN = 0,
    PAD_LINK_SCANNING = 1,
    PAD_LINK_LINKED = 2,
};

typedef struct pad_view {
    uint16_t buttons;
    uint8_t hat;
    uint8_t link;
} pad_view;

#endif
