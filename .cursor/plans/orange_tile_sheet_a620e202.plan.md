---
name: Orange tile sheet
overview: Pack the orange sheet’s existing shapes as 8bpp palette indices and blit them for each map kind. MAP_SET is the one full tile, Orange1, on every set cell. No autotile.
todos:
  - id: pin-colors
    content: Pin the four orange-sheet colors in the palette generator and name them in palette.h. Reuse the ember index.
    status: completed
  - id: pack-sheet
    content: Pack the 25 cells into game/tile_sheet.c, with a span check per kind, and compile it on both targets.
    status: completed
  - id: blit
    content: Blit indices into the 8bpp buffer on device and RGB565 on PC. Skip index 0 and clean rows.
    status: completed
  - id: draw-map
    content: Draw each published solid from its kind sprite, clipped to that rect. Leave spike, checkpoint, exit, shuttle, and disk.
    status: completed
  - id: record
    content: Update architecture.md, the boot log, and the function comments. Arenas stay the same size.
    status: completed
isProject: false
---

# Orange tiles, one sprite per kind

MAP_SET is one collision shape: the full 16×16 box. [Orange1](refs/PlatformerTilesetTemplate_AssetPack/Alternate Colors/Orange/PlatformerTilesetTemplate_Orange1.png) is that picture (sheet cell column 0, row 0): a checker block with a 2 px outline and a transparent pixel in each corner. Repeating it draws that outline on every cell, including edges shared with the next set tile. The other full cells in the sheet only change which sides have that outline. That neighbor choice stays later.

The map strip stays kind bytes. Present picks a sprite from the kind. Collision, the apron, the snapshot, and both arenas stay as they are.

## What gets a sprite

Packed from [PlatformerTilesetTemplate_Tileset_Orange.png](refs/PlatformerTilesetTemplate_AssetPack/Alternate Colors/Orange/PlatformerTilesetTemplate_Tileset_Orange.png), 13×9 cells of 16 px. The script checks each cell’s opaque span and stops if the sheet moved.

- `MAP_SET`: cell (0, 0), Orange1.
- Floor: R45 (5, 11), L45 (6, 1), R2 (6, 2) then (6, 3), L2 (6, 6) then (6, 7), R3 (6, 8) (6, 9) (6, 10), L3 (7, 0) (7, 1) (7, 2). Order matches the kind bytes, low segment first for R and high segment first for L.
- Ceiling, same order: R45 (7, 5), L45 (7, 4), R2 (7, 10) (7, 11), L2 (7, 8) (7, 9), R3 (8, 4) (8, 5) (8, 6), L3 (8, 1) (8, 2) (8, 3). These are the sheet’s ceiling cells, not a flip of the floor art.
- One-way keeps the 4 px lip. The blit uses the top 4 rows of Orange1, because the published rect is already that lip. No second sprite.
- Spike, checkpoint, exit, shuttle, and disk stay on their current draws. Half tiles and corners stay unused.

## Why this blit

`drawPixel` on the device runs `index_for`, a scan of all 256 palette entries, once per pixel. `Renderer::drawSprite` writes `packRgb565ToTftSprite8` into the 8bpp buffer, which is the wrong encoding, and `getSpriteBuffer` is null on both targets. The SDL `drawTileDirect` indexes the RGB565 framebuffer as if it were a palette.

The cheap path is the one the body sheet already uses for storage, with a tighter write:

- Flash holds 8bpp indices. Index 0 is skip, so slope air and Orange1’s four corners leave the ground that the dirty rows already restored.
- Device: a new blit on [host/st7789_drawer.cpp](host/st7789_drawer.cpp) writes those indices into the 8bpp buffer. A clean row is left alone, same rule as `drawFilledRectangle`.
- PC: the same loop writes `palette_rgb565[index]` through `getPixelBuffer()`. No new code in the vendored SDL drawer.

A view of these tiles is a few dozen 16×16 copies. That write is small next to the SPI expand. The row mask still comes from the published rect, so a slope’s empty triangle is ground again.

## Colors

The sheet has five opaque colors. `(255, 162, 20)` is already `PALETTE_BODY_EMBER` (143). Pin the other four in [tools/gen_palette.py](tools/gen_palette.py) and name them in [game/palette.h](game/palette.h): `(237, 118, 20)`, `(198, 69, 36)`, `(26, 25, 50)`, `(14, 7, 27)`. Index 0 stays black and stays the skip index. The outlines are those two dark purples, not a second black.

## Files

- [tools/pack_tile_sheet.py](tools/pack_tile_sheet.py) writes [game/tile_sheet.c](game/tile_sheet.c) and [game/tile_sheet.h](game/tile_sheet.h). Stdlib only, same PNG reader as [tools/pack_body_sheet.py](tools/pack_body_sheet.py). `tile_sheet_at(kind)` returns the 256 indices, or null when that kind has no picture. 25 frames, 6400 bytes in flash.
- [main/CMakeLists.txt](main/CMakeLists.txt) and [pc/CMakeLists.txt](pc/CMakeLists.txt) compile it next to `body_sheet.c`.
- [host/engine_host.cpp](host/engine_host.cpp) replaces the black fill and `draw_slope` with one blit clipped to the published rect and the view. World position plus the camera offset, because the write bypasses `Renderer`.
- The boot log gains the tile-sheet byte count beside the body sheet.
- [architecture.md](architecture.md): platforms and the lip are this sheet, not black; slopes and ceilings are the sprites, not black spans; the flash row; one decision-log line. Half tiles and corners stay later. Function comments whose sentence stops being true get the new sentence. `third_party/` stays untouched.
