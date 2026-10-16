"""Pack the No Outlines body sheets into 8bpp palette indices.

Stdlib only. Run tools/gen_palette.py first. Index 0 is transparent.
Opaque pixels fold to the nearest of the four pinned fills.
"""

import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHEETS = ROOT / "refs" / "2DPlatformerCharacterTemplate_AssetPack" / "Sheets" / "No Outlines"
OUT_C = ROOT / "game" / "body_sheet.c"

CELL = 16

CLIPS = [
    ("BODY_CLIP_IDLE", "2DPCT_Idle_NO.png"),
    ("BODY_CLIP_WALK", "2DPCT_Walk_NO.png"),
    ("BODY_CLIP_JOG", "2DPCT_Jog_NO.png"),
    ("BODY_CLIP_RUN", "2DPCT_Run_NO.png"),
    ("BODY_CLIP_RUN_TURN", "2DPCT_RunTurn_NO.png"),
    ("BODY_CLIP_JUMP", "2DPCT_Jump_NO.png"),
    ("BODY_CLIP_JUMP_TURN", "2DPCT_JumpTurn_NO.png"),
    ("BODY_CLIP_DASH", "2DPCT_Dash_NO.png"),
    ("BODY_CLIP_WALL", "2DPCT_WallSlide_NO.png"),
]

# Sheet fills, in the order an opaque pixel is matched. The index is the pinned slot.
FILLS = (
    ((153, 230, 95), "PALETTE_BODY_LEAF"),
    ((0, 205, 249), "PALETTE_BODY_GLASS"),
    ((255, 162, 20), "PALETTE_BODY_EMBER"),
    ((245, 85, 93), "PALETTE_BODY_KILN"),
)


def fill_slots():
    text = (ROOT / "game" / "palette.h").read_text(encoding="utf-8")
    slots = []
    for rgb, name in FILLS:
        index = None
        prefix = f"#define {name} "
        for line in text.splitlines():
            if line.startswith(prefix):
                index = int(line[len(prefix) :])
                break
        if index is None:
            raise SystemExit(f"missing {name} in palette.h")
        slots.append((rgb, index))
    return slots


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


def index_of(r, g, b, a, slots):
    if a < 128:
        return 0
    best = slots[0][1]
    best_d = None
    for (cr, cg, cb), index in slots:
        d = (r - cr) * (r - cr) + (g - cg) * (g - cg) + (b - cb) * (b - cb)
        if best_d is None or d < best_d:
            best = index
            best_d = d
    return best


def cell_bytes(pixels, ox, oy, slots):
    rows = []
    opaque = 0
    for y in range(CELL):
        row = []
        for x in range(CELL):
            r, g, b, a = pixels[oy + y][ox + x]
            index = index_of(r, g, b, a, slots)
            if index != 0:
                opaque += 1
            row.append(index)
        rows.append(row)
    return rows, opaque


def pack(slots):
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
                rows, opaque = cell_bytes(pixels, cx * CELL, cy * CELL, slots)
                if opaque == 0:
                    continue
                clip_frames.append(rows)
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
    lines.append("static const uint8_t sheet_px[] = {")
    for clip_frames in frames:
        for rows in clip_frames:
            for row in rows:
                words = ", ".join(f"0x{index:02x}" for index in row)
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
    lines.append("/* [body_sheet_frame] Devolve os 256 índices do quadro. Fora da faixa, devolve 0. */")
    lines.append("const uint8_t *body_sheet_frame(int clip, int frame)")
    lines.append("{")
    lines.append("    int base;")
    lines.append("    int i;")
    lines.append("")
    lines.append("    if (clip < 0 || clip >= BODY_CLIP_COUNT) {")
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
    lines.append("    return sheet_px + base * BODY_SHEET_W * BODY_SHEET_H;")
    lines.append("}")
    lines.append("")
    OUT_C.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main():
    frames, counts = pack(fill_slots())
    total = sum(counts)
    emit(frames, counts)
    nbytes = total * CELL * CELL
    print("frames", counts, "total", total, "bytes", nbytes)


if __name__ == "__main__":
    main()
