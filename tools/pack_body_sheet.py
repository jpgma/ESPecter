"""Pack the No Outlines body sheets into four 1bpp layers.

Stdlib only. Bit 15 of each row is the left pixel, matching Renderer::drawSprite.
"""

import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHEETS = ROOT / "refs" / "2DPlatformerCharacterTemplate_AssetPack" / "Sheets" / "No Outlines"
OUT_C = ROOT / "game" / "body_sheet.c"

CELL = 16
LAYERS = 4

CLIPS = [
    ("BODY_CLIP_IDLE", "2DPCT_Idle_NO.png"),
    ("BODY_CLIP_WALK", "2DPCT_Walk_NO.png"),
    ("BODY_CLIP_RUN", "2DPCT_Run_NO.png"),
    ("BODY_CLIP_RUN_TURN", "2DPCT_RunTurn_NO.png"),
    ("BODY_CLIP_JUMP", "2DPCT_Jump_NO.png"),
    ("BODY_CLIP_JUMP_TURN", "2DPCT_JumpTurn_NO.png"),
    ("BODY_CLIP_DASH", "2DPCT_Dash_NO.png"),
    ("BODY_CLIP_WALL", "2DPCT_WallSlide_NO.png"),
]

# The four fills, measured as the most common opaque colors. Extras fold to the nearest.
CENTROIDS = (
    (153, 230, 95),
    (0, 205, 249),
    (255, 162, 20),
    (245, 85, 93),
)


def paeth(a, b, c):
    estimate = a + b - c
    da = abs(estimate - a)
    db = abs(estimate - b)
    dc = abs(estimate - c)
    if da <= db and da <= dc:
        return a
    if db <= dc:
        return b
    return c


def read_png(path):
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"not a png: {path}")
    pos = 8
    width = height = color_type = bit_depth = None
    idat = b""
    while pos < len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        kind = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", chunk[:10])
        elif kind == b"IDAT":
            idat += chunk
        elif kind == b"IEND":
            break
    if color_type != 6 or bit_depth != 8:
        raise SystemExit(f"expected 8-bit rgba: {path}")
    raw = zlib.decompress(idat)
    stride = width * 4
    rows = []
    index = 0
    prev = bytearray(stride)
    for _y in range(height):
        filt = raw[index]
        index += 1
        row = bytearray(raw[index : index + stride])
        index += stride
        for x in range(stride):
            left = row[x - 4] if x >= 4 else 0
            up = prev[x]
            ul = prev[x - 4] if x >= 4 else 0
            if filt == 1:
                row[x] = (row[x] + left) & 255
            elif filt == 2:
                row[x] = (row[x] + up) & 255
            elif filt == 3:
                row[x] = (row[x] + ((left + up) // 2)) & 255
            elif filt == 4:
                row[x] = (row[x] + paeth(left, up, ul)) & 255
            elif filt != 0:
                raise SystemExit(f"bad filter {filt} in {path}")
        prev = row
        rows.append(row)
    pixels = []
    for row in rows:
        line = []
        for x in range(width):
            line.append(tuple(row[x * 4 : x * 4 + 4]))
        pixels.append(line)
    return pixels


def layer_of(r, g, b, a):
    if a < 128:
        return -1
    best = 0
    best_d = None
    for i, (cr, cg, cb) in enumerate(CENTROIDS):
        d = (r - cr) * (r - cr) + (g - cg) * (g - cg) + (b - cb) * (b - cb)
        if best_d is None or d < best_d:
            best = i
            best_d = d
    return best


def cell_bits(pixels, ox, oy):
    layers = [[0] * CELL for _ in range(LAYERS)]
    opaque = 0
    for y in range(CELL):
        for x in range(CELL):
            r, g, b, a = pixels[oy + y][ox + x]
            layer = layer_of(r, g, b, a)
            if layer < 0:
                continue
            opaque += 1
            layers[layer][y] |= 1 << (CELL - 1 - x)
    return layers, opaque


def pack():
    frames = []
    counts = []
    for _name, filename in CLIPS:
        pixels = read_png(SHEETS / filename)
        height = len(pixels)
        width = len(pixels[0])
        if width % CELL != 0 or height % CELL != 0:
            raise SystemExit(f"sheet is not a 16 grid: {filename}")
        clip_frames = []
        for cy in range(height // CELL):
            for cx in range(width // CELL):
                layers, opaque = cell_bits(pixels, cx * CELL, cy * CELL)
                if opaque == 0:
                    continue
                clip_frames.append(layers)
        if not clip_frames:
            raise SystemExit(f"no frames in {filename}")
        counts.append(len(clip_frames))
        frames.append(clip_frames)
    return frames, counts


def emit(frames, counts):
    lines = []
    lines.append('#include "body_sheet.h"')
    lines.append("")
    lines.append("/* RunTurn e JumpTurn nascem virados à direita. Nenhum quadro olha à esquerda, então o flip do host não espelha duas vezes. */")
    lines.append("static const uint16_t sheet_rows[] = {")
    for clip_frames in frames:
        for layers in clip_frames:
            for layer in layers:
                words = ", ".join(f"0x{word:04x}" for word in layer)
                lines.append(f"    {words},")
    lines.append("};")
    lines.append("")
    lines.append("static const int sheet_frames[BODY_CLIP_COUNT] = {")
    lines.append("    " + ", ".join(str(count) for count in counts) + ",")
    lines.append("};")
    lines.append("")
    lines.append("/* [body_sheet_frames] Devolve quantos quadros o clip tem. Fora da faixa, devolve 0. */")
    lines.append("int body_sheet_frames(int clip)")
    lines.append("{")
    lines.append("    if (clip < 0 || clip >= BODY_CLIP_COUNT) {")
    lines.append("        return 0;")
    lines.append("    }")
    lines.append("    return sheet_frames[clip];")
    lines.append("}")
    lines.append("")
    lines.append("/* [body_sheet_rows] Devolve as 16 linhas 1bpp da layer do quadro. Fora da faixa, devolve 0. */")
    lines.append("const uint16_t *body_sheet_rows(int clip, int frame, int layer)")
    lines.append("{")
    lines.append("    int base;")
    lines.append("    int i;")
    lines.append("")
    lines.append("    if (clip < 0 || clip >= BODY_CLIP_COUNT || layer < 0 || layer >= BODY_LAYER_COUNT) {")
    lines.append("        return 0;")
    lines.append("    }")
    lines.append("    if (frame < 0 || frame >= sheet_frames[clip]) {")
    lines.append("        return 0;")
    lines.append("    }")
    lines.append("    base = 0;")
    lines.append("    for (i = 0; i < clip; i++) {")
    lines.append("        base += sheet_frames[i];")
    lines.append("    }")
    lines.append("    base += frame;")
    lines.append("    return sheet_rows + (base * BODY_LAYER_COUNT + layer) * BODY_SHEET_H;")
    lines.append("}")
    lines.append("")
    OUT_C.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main():
    frames, counts = pack()
    total = sum(counts)
    emit(frames, counts)
    nbytes = total * LAYERS * CELL * 2
    print("frames", counts, "total", total, "bytes", nbytes)


if __name__ == "__main__":
    main()
