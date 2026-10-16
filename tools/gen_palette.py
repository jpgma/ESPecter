"""Build the lantern palette: 16 ramps of 16, snapped to unique RGB565.

Stdlib only. Index 0 is black. Pins overwrite the nearest step of their ramp.
"""

import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_H = ROOT / "game" / "palette.h"
OUT_C = ROOT / "game" / "palette.c"
OUT_PNG = ROOT / "refs" / "palette_lantern.png"

CELL = 16
COUNT = 256

# Dark to light, sRGB. The light end of glass, leaf, ember, and kiln is the sheet fill.
RAMPS = (
    ("ink", (0, 0, 0), (255, 236, 214)),
    ("ash", (18, 22, 32), (186, 196, 208)),
    ("dusk", (20, 8, 48), (148, 112, 220)),
    ("tide", (0, 32, 36), (48, 200, 188)),
    ("glass", (0, 36, 52), (0, 205, 249)),
    ("leaf", (12, 40, 8), (153, 230, 95)),
    ("moss", (0, 28, 8), (120, 210, 70)),
    ("lantern", (48, 28, 0), (255, 204, 48)),
    ("ember", (56, 16, 0), (255, 162, 20)),
    ("kiln", (40, 0, 8), (245, 85, 93)),
    ("hazard", (48, 0, 36), (248, 0, 248)),
    ("wine", (40, 0, 18), (200, 64, 96)),
    ("clay", (48, 20, 14), (224, 156, 118)),
    ("sand", (44, 32, 16), (240, 208, 150)),
    ("iris", (24, 8, 48), (164, 124, 255)),
    ("fog", (28, 32, 36), (214, 224, 230)),
)

# Exact RGB565 the scene already draws, then the four sheet fills.
PIN_WHITE = 0xFFFF
PIN_DARK_GREEN = 0x13C2
PIN_HAZARD = 0xF81F


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def from_rgb565(color):
    r = (color >> 11) & 31
    g = (color >> 5) & 63
    b = color & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def srgb_to_linear(channel):
    c = channel / 255.0
    if c <= 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4


def linear_to_srgb(channel):
    if channel <= 0.0031308:
        value = 12.92 * channel
    else:
        value = 1.055 * (channel ** (1.0 / 2.4)) - 0.055
    value = 0.0 if value < 0.0 else value
    value = 1.0 if value > 1.0 else value
    return int(round(value * 255.0))


def lerp_ramp(dark, light, t):
    out = []
    for a, b in zip(dark, light):
        la = srgb_to_linear(a)
        lb = srgb_to_linear(b)
        out.append(linear_to_srgb(la + (lb - la) * t))
    return tuple(out)


def dist2(left, right):
    a = from_rgb565(left)
    b = from_rgb565(right)
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def gap_ok(left, right):
    if left is None:
        return True
    ar, ag, ab = from_rgb565(left)
    br, bg, bb = from_rgb565(right)
    dr = abs(ar - br)
    dg = abs(ag - bg)
    db = abs(ab - bb)
    return dr >= 16 or dg >= 12 or db >= 16 or (dr + dg + db) >= 28


def candidates(color):
    r = (color >> 11) & 31
    g = (color >> 5) & 63
    b = color & 31
    yield color
    for dg in range(1, 64):
        for sign in (1, -1):
            ng = g + sign * dg
            if 0 <= ng <= 63:
                yield (r << 11) | (ng << 5) | b
    for db in range(1, 32):
        for sign in (1, -1):
            nb = b + sign * db
            if 0 <= nb <= 31:
                yield (r << 11) | (g << 5) | nb
    for dr in range(1, 32):
        for sign in (1, -1):
            nr = r + sign * dr
            if 0 <= nr <= 31:
                yield (nr << 11) | (g << 5) | b
    for dg in range(0, 64):
        for db in range(0, 32):
            for dr in range(0, 32):
                if dg == 0 and db == 0 and dr == 0:
                    continue
                yield (dr << 11) | (dg << 5) | db


def acquire(color, used, neighbor):
    for choice in candidates(color):
        if choice == 0 or choice in used:
            continue
        if not gap_ok(neighbor, choice):
            continue
        return choice
    raise SystemExit("no free RGB565")


def build_ramps():
    colors = [0] * COUNT
    used = {0}
    for row, (_name, dark, light) in enumerate(RAMPS):
        prev = None
        for step in range(CELL):
            index = row * CELL + step
            if index == 0:
                prev = 0
                continue
            t = (step / (CELL - 1)) ** 1.65
            snapped = rgb565(*lerp_ramp(dark, light, t))
            choice = acquire(snapped, used, prev)
            colors[index] = choice
            used.add(choice)
            prev = choice
    return colors


def nearest_step(colors, row, target, taken):
    best = None
    best_d = None
    for step in range(CELL):
        index = row * CELL + step
        if index == 0 or index in taken:
            continue
        d = dist2(colors[index], target)
        if best_d is None or d < best_d:
            best = index
            best_d = d
    return best


def displace(colors, index, taken):
    if index in taken:
        raise SystemExit(f"pin collision at {index}")
    banned = colors[index]
    used = set(colors)
    used.add(banned)
    neighbor = colors[index - 1] if index % CELL else None
    colors[index] = acquire(banned, used, neighbor)


def place_pin(colors, index, target, taken):
    if target in colors:
        other = colors.index(target)
        if other != index:
            displace(colors, other, taken)
    colors[index] = target
    taken.add(index)


def apply_pins(colors):
    taken = set()
    place_pin(colors, 15, PIN_WHITE, taken)
    place_pin(colors, 10 * CELL + 15, PIN_HAZARD, taken)
    pins = {
        "white": 15,
        "hazard": 10 * CELL + 15,
    }
    nearest = (
        ("dark_green", 6, PIN_DARK_GREEN),
        ("linked", 6, rgb565(0, 140, 32)),
        ("scanning", 7, rgb565(220, 180, 0)),
        ("down", 9, rgb565(180, 0, 0)),
        ("kiln", 9, rgb565(245, 85, 93)),
        ("glass", 4, rgb565(0, 205, 249)),
        ("leaf", 5, rgb565(153, 230, 95)),
        ("ember", 8, rgb565(255, 162, 20)),
    )
    for name, row, target in nearest:
        index = nearest_step(colors, row, target, taken)
        place_pin(colors, index, target, taken)
        pins[name] = index
    repair_neighbors(colors, taken)
    return pins


def repair_neighbors(colors, taken):
    for _pass in range(4):
        changed = False
        for row in range(CELL):
            for step in range(1, CELL):
                index = row * CELL + step
                prev = index - 1
                if gap_ok(colors[prev], colors[index]):
                    continue
                if index in taken and prev not in taken:
                    move = prev
                    anchor = colors[index]
                elif index not in taken:
                    move = index
                    anchor = colors[prev]
                else:
                    continue
                used = set(colors)
                used.discard(colors[move])
                colors[move] = acquire(colors[move], used, anchor)
                changed = True
        if not changed:
            return


def check(colors, pins):
    if colors[0] != 0:
        raise SystemExit("index 0 is not black")
    if len(set(colors)) != COUNT:
        raise SystemExit("palette has duplicate RGB565")
    for name in ("glass", "leaf", "ember", "kiln"):
        if colors[pins[name]] == 0:
            raise SystemExit(f"body slot {name} is black")
    for row in range(CELL):
        for step in range(1, CELL):
            index = row * CELL + step
            if colors[index] == colors[index - 1]:
                raise SystemExit(f"ramp {row} step {step} collapsed")


def emit_h(pins):
    lines = [
        "#ifndef ESPECTER_PALETTE_H",
        "#define ESPECTER_PALETTE_H",
        "",
        "#include <stdint.h>",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        "#define PALETTE_COUNT 256",
        f"#define PALETTE_BODY_LEAF {pins['leaf']}",
        f"#define PALETTE_BODY_GLASS {pins['glass']}",
        f"#define PALETTE_BODY_EMBER {pins['ember']}",
        f"#define PALETTE_BODY_KILN {pins['kiln']}",
        "",
        "extern const uint16_t palette_rgb565[PALETTE_COUNT];",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        "#endif",
        "",
    ]
    OUT_H.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def emit_c(colors):
    lines = [
        '#include "palette.h"',
        "",
        "/* 16 rampas de 16. O índice 0 é preto e o skip do sprite. */",
        "const uint16_t palette_rgb565[PALETTE_COUNT] = {",
    ]
    for row in range(CELL):
        name = RAMPS[row][0]
        lines.append(f"    /* {name} */")
        for step_row in range(2):
            chunk = colors[row * CELL + step_row * 8 : row * CELL + step_row * 8 + 8]
            words = ", ".join(f"0x{word:04x}" for word in chunk)
            lines.append(f"    {words},")
    lines.append("};")
    lines.append("")
    OUT_C.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def png_chunk(tag, data):
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def emit_png(colors):
    scale = 16
    width = CELL * scale
    height = CELL * scale
    raw = bytearray()
    for row in range(CELL):
        line = bytearray()
        for step in range(CELL):
            r, g, b = from_rgb565(colors[row * CELL + step])
            line.extend(bytes((r, g, b)) * scale)
        raw.extend(b"\x00" + line)
        raw.extend((b"\x00" + line) * (scale - 1))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", ihdr)
    png += png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += png_chunk(b"IEND", b"")
    OUT_PNG.write_bytes(png)


def build_palette():
    colors = build_ramps()
    pins = apply_pins(colors)
    check(colors, pins)
    return colors, pins


def main():
    colors, pins = build_palette()
    emit_h(pins)
    emit_c(colors)
    emit_png(colors)
    print("pins", pins)
    print("bytes", COUNT * 2)


if __name__ == "__main__":
    main()
