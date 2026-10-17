"""Pack the orange tileset cells into 8bpp palette indices.

Stdlib only. Run tools/gen_palette.py first. Index 0 is transparent.
Opaque pixels fold to the pinned tile slots and the ember fill.
"""

import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHEET = (
    ROOT
    / "refs"
    / "PlatformerTilesetTemplate_AssetPack"
    / "Alternate Colors"
    / "Orange"
    / "PlatformerTilesetTemplate_Tileset_Orange.png"
)
OUT_H = ROOT / "game" / "tile_sheet.h"
OUT_C = ROOT / "game" / "tile_sheet.c"

CELL = 16
COLS = 13
ROWS = 9

# kind name, sheet column, sheet row, expected opaque count.
KINDS = (
    ("MAP_SET", 0, 0, 252),
    ("MAP_SLOPE_R45", 11, 5, 136),
    ("MAP_SLOPE_L45", 1, 6, 136),
    ("MAP_SLOPE_R2_0", 2, 6, 64),
    ("MAP_SLOPE_R2_1", 3, 6, 192),
    ("MAP_SLOPE_L2_0", 7, 6, 64),
    ("MAP_SLOPE_L2_1", 6, 6, 192),
    ("MAP_SLOPE_R3_0", 8, 6, 45),
    ("MAP_SLOPE_R3_1", 9, 6, 131),
    ("MAP_SLOPE_R3_2", 10, 6, 216),
    ("MAP_SLOPE_L3_0", 2, 7, 45),
    ("MAP_SLOPE_L3_1", 1, 7, 131),
    ("MAP_SLOPE_L3_2", 0, 7, 216),
    ("MAP_CEIL_R45", 5, 7, 136),
    ("MAP_CEIL_L45", 4, 7, 136),
    ("MAP_CEIL_R2_0", 10, 7, 64),
    ("MAP_CEIL_R2_1", 11, 7, 192),
    ("MAP_CEIL_L2_0", 9, 7, 64),
    ("MAP_CEIL_L2_1", 8, 7, 192),
    ("MAP_CEIL_R3_0", 4, 8, 45),
    ("MAP_CEIL_R3_1", 5, 8, 131),
    ("MAP_CEIL_R3_2", 6, 8, 216),
    ("MAP_CEIL_L3_0", 3, 8, 45),
    ("MAP_CEIL_L3_1", 2, 8, 131),
    ("MAP_CEIL_L3_2", 1, 8, 216),
)

FILLS = (
    ((255, 162, 20), "PALETTE_BODY_EMBER"),
    ((237, 118, 20), "PALETTE_TILE_MID"),
    ((198, 69, 36), "PALETTE_TILE_DEEP"),
    ((26, 25, 50), "PALETTE_TILE_EDGE"),
    ((14, 7, 27), "PALETTE_TILE_INK"),
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


def cell_bytes(pixels, cx, cy, slots):
    rows = []
    opaque = 0
    spans = []
    for y in range(CELL):
        row = []
        first = None
        last = None
        for x in range(CELL):
            r, g, b, a = pixels[cy * CELL + y][cx * CELL + x]
            index = index_of(r, g, b, a, slots)
            if index != 0:
                opaque += 1
                if first is None:
                    first = x
                last = x
            row.append(index)
        rows.append(row)
        if first is None:
            spans.append((-1, -1))
        else:
            spans.append((first, last))
    return rows, opaque, spans


def pack(slots):
    pixels = read_png(SHEET)
    height = len(pixels)
    width = len(pixels[0])
    if width != COLS * CELL or height != ROWS * CELL:
        raise SystemExit(f"sheet size {width}x{height}, expected {COLS * CELL}x{ROWS * CELL}")
    frames = []
    for name, cx, cy, expect in KINDS:
        rows, opaque, spans = cell_bytes(pixels, cx, cy, slots)
        if opaque != expect:
            raise SystemExit(f"{name} at ({cx},{cy}): opaque {opaque}, expected {expect}")
        if opaque == 0:
            raise SystemExit(f"{name} is empty")
        # Span fingerprint: first and last non-empty rows must have a solid run.
        nonempty = [s for s in spans if s[0] >= 0]
        if not nonempty:
            raise SystemExit(f"{name} has no opaque span")
        frames.append((name, rows))
    return frames


def emit(frames):
    lines = []
    lines.append("#ifndef ESPECTER_TILE_SHEET_H")
    lines.append("#define ESPECTER_TILE_SHEET_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("#ifdef __cplusplus")
    lines.append('extern "C" {')
    lines.append("#endif")
    lines.append("")
    lines.append("#define TILE_SHEET_W 16")
    lines.append("#define TILE_SHEET_H 16")
    lines.append("#define TILE_SHEET_COUNT 25")
    lines.append("")
    lines.append("/* [tile_sheet_at] Devolve os 256 índices do kind. Sem quadro, devolve 0. */")
    lines.append("const uint8_t *tile_sheet_at(int kind);")
    lines.append("")
    lines.append("#ifdef __cplusplus")
    lines.append("}")
    lines.append("#endif")
    lines.append("")
    lines.append("#endif")
    lines.append("")
    OUT_H.write_text("\n".join(lines), encoding="utf-8", newline="\n")

    lines = []
    lines.append('#include "tile_sheet.h"')
    lines.append("")
    lines.append('#include "map.h"')
    lines.append("")
    lines.append("static const uint8_t sheet_px[TILE_SHEET_COUNT * TILE_SHEET_W * TILE_SHEET_H] = {")
    for _name, rows in frames:
        for row in rows:
            words = ", ".join(f"0x{index:02x}" for index in row)
            lines.append(f"    {words},")
    lines.append("};")
    lines.append("")
    lines.append("/* kind -> índice no atlas; -1 sem quadro. */")
    lines.append("static const int8_t kind_slot[] = {")
    # MAP_EMPTY through MAP_CEIL_L3_2
    slot_of = {name: i for i, (name, _rows) in enumerate(frames)}
    kind_names = [
        "MAP_EMPTY",
        "MAP_SET",
        "MAP_SPIKE",
        "MAP_CHECKPOINT",
        "MAP_EXIT",
        "MAP_ONEWAY",
        "MAP_SLOPE_R45",
        "MAP_SLOPE_L45",
        "MAP_SLOPE_R2_0",
        "MAP_SLOPE_R2_1",
        "MAP_SLOPE_L2_0",
        "MAP_SLOPE_L2_1",
        "MAP_SLOPE_R3_0",
        "MAP_SLOPE_R3_1",
        "MAP_SLOPE_R3_2",
        "MAP_SLOPE_L3_0",
        "MAP_SLOPE_L3_1",
        "MAP_SLOPE_L3_2",
        "MAP_CEIL_R45",
        "MAP_CEIL_L45",
        "MAP_CEIL_R2_0",
        "MAP_CEIL_R2_1",
        "MAP_CEIL_L2_0",
        "MAP_CEIL_L2_1",
        "MAP_CEIL_R3_0",
        "MAP_CEIL_R3_1",
        "MAP_CEIL_R3_2",
        "MAP_CEIL_L3_0",
        "MAP_CEIL_L3_1",
        "MAP_CEIL_L3_2",
    ]
    values = []
    for name in kind_names:
        if name == "MAP_ONEWAY":
            # One-way reuses MAP_SET; the host clips to the lip height.
            values.append(str(slot_of["MAP_SET"]))
        elif name in slot_of:
            values.append(str(slot_of[name]))
        else:
            values.append("-1")
    lines.append("    " + ", ".join(values) + ",")
    lines.append("};")
    lines.append("")
    lines.append("/* [tile_sheet_at] Devolve os 256 índices do kind. Sem quadro, devolve 0. */")
    lines.append("const uint8_t *tile_sheet_at(int kind)")
    lines.append("{")
    lines.append("    int slot;")
    lines.append("")
    lines.append("    if (kind < 0 || kind >= (int)(sizeof kind_slot / sizeof kind_slot[0])) {")
    lines.append("        return 0;")
    lines.append("    }")
    lines.append("    slot = kind_slot[kind];")
    lines.append("    if (slot < 0) {")
    lines.append("        return 0;")
    lines.append("    }")
    lines.append("    return sheet_px + slot * TILE_SHEET_W * TILE_SHEET_H;")
    lines.append("}")
    lines.append("")
    OUT_C.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main():
    frames = pack(fill_slots())
    emit(frames)
    nbytes = len(frames) * CELL * CELL
    print("tiles", len(frames), "bytes", nbytes)


if __name__ == "__main__":
    main()
