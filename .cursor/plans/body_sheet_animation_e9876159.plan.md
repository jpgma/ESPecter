---
name: body sheet animation
overview: "Replace the white 16×16 body with the No Outlines sheets, packed as four 1bpp layers and drawn with PixelRoot32 `drawSprite` plus `flipX`. The clip follows the sim tick: idle, walk, run, run-turn, jump, jump-turn, wall slide, and the dash sheet on a wall jump."
todos:
  - id: pack-sheets
    content: Pack the No Outlines sheets into four 1bpp layers in game/body_sheet.c, MSB = left pixel
    status: completed
  - id: sim-pose
    content: Choose idle, walk, run, turns, jump, wall slide, and wall-jump dash on the 16 ms tick
    status: completed
  - id: host-draw
    content: Draw the pose with four drawSprite calls and flipX, at the unclipped world origin
    status: completed
  - id: docs
    content: Record the four-color 1bpp figure, the tick-driven pose, and the flash row in architecture.md and README.md
    status: completed
isProject: false
---

# Body sheet animation

The body stays a 16×16 AABB. Art is four 1bpp layers of that box, not a second collision shape. `game/` still does not include PixelRoot32. The host only draws the pose the world already chose.

## Why this path

PixelRoot32 1.12.0 already blits a 1bpp [`Sprite`](third_party/pixelroot32/include/graphics/Renderer.h) with `flipX`. Bit `(width - 1)` is the left pixel in [`Renderer::drawSprite`](third_party/pixelroot32/src/graphics/Renderer.cpp); the header comment that says bit 0 is left is wrong. Unset bits are skipped, so black in the sheet is transparent.

`SpriteAnimation` and `drawMultiSprite` stay unused. The animation helper only steps a 1bpp or `MultiSprite` index, with no duration and no one-shot. `drawMultiSprite` calls `drawSprite` with flip forced off. 2bpp would keep only three visible colors (index 0 is skipped) and its flag is off. 4bpp stays off too.

Do not override `getSpriteBuffer`. The device buffer stores a palette index from `index_for`. The engine's fast 8bpp sprite path writes `packRgb565ToTftSprite8` instead. Both targets already fall through to `drawPixel`, which is the correct encoding. Four 16×16 layers are a small draw.

## Sheets

Source is [`refs/2DPlatformerCharacterTemplate_AssetPack/Sheets/No Outlines`](refs/2DPlatformerCharacterTemplate_AssetPack/Sheets/No Outlines). Individual frames already exist for most clips; RunTurn has only the sheet.

- Idle: 1 frame. Floor and `vx == 0`.
- Walk: 8 frames. Floor and `0 < |vx| < WORLD_RUN_MAX` (768).
- Run: 8 frames. Floor and `|vx| >= 768`, including the bleed down from a wall jump.
- RunTurn: cells of `2DPCT_RunTurn_NO.png`. One-shot when grounded speed changes sign and both the old and the new `vx` are non-zero. Leaving idle is not a turn.
- Jump: 5 frames. Airborne, played once from frame 0, then the last frame holds.
- JumpTurn: 4 frames. One-shot on the same sign change while airborne.
- Dash: 2 frames. One-shot on the wall-jump edge. That edge is "jumping off a wall".
- WallSlide: the sheet's 16×16 cells (one individual frame exists). While the latch holds.

Jog, crouch, and crouch-turn stay out. Down only drops the latch. There is one horizontal accel, so jog would be a second copy of walk.

A Python script under `tools/`, stdlib only, slices every 16×16 cell, drops empty cells, and writes [`game/body_sheet.c`](game/body_sheet.c). Each opaque pixel goes to the nearest of the four colors measured on the sheets. Those four layers are `Color::LightGreen`, `Color::Cyan`, `Color::Orange`, and `Color::Red`. A fifth shade, if one shows up, folds into the nearest of those four. Flash is about 4 KB: 16 `uint16_t` rows times 4 layers times the frame count (about 33). The generated arrays are committed so the ESP-IDF and PC builds do not need the script.

Sheets face right. `flipX` is on when face is `-1`, including turns. While packing, if a turn frame already faces left, leave that frame unflipped in the note next to the table so it is not mirrored twice.

## Sim

New SoA columns on the first world row, still inside the existing 4096-byte `world` slice (`WORLD_COLS` grows; the slice size does not): face, clip, frame, hold, the jump act, and previous `vx`.

[`body_jump`](game/world.c) records floor jump versus wall jump on that tick. After the camera spring, one new function picks the clip:

1. Latch wins: wall slide. A wall jump has already cleared the latch, so the dash can start.
2. Wall-jump act starts the dash one-shot from frame 0.
3. An unfinished one-shot (dash, run-turn, jump-turn) advances. Landing cancels dash, jump, and jump-turn.
4. Otherwise: grounded sign change starts run-turn; airborne sign change starts jump-turn; still airborne continues jump without restarting it; else run, walk, or idle.

Walk and run share 8 frames, so crossing the cap keeps the frame index. Any other clip change resets frame and hold. [`body_place`](game/world.c) sets face to `1`, clip to idle, frame 0.

Holds, in 16 ms ticks: walk and wall slide 4 per frame; run, jump, and the one-shots 2 per frame. Idle does not advance. When a one-shot ends, the same rules pick the next clip on that tick. Dash ending in the air holds jump's last frame instead of replaying the takeoff.

[`world_body_pose`](game/world.h) writes clip, frame, and face for the host. Collision, camera, and `world_rect` stay as they are.

## Draw

In [`rect_scene::draw`](host/engine_host.cpp), drop the white `drawFilledRectangle` for the body. If the 16×16 box misses the view, draw nothing. If it crosses the view, draw it at the unclipped world origin. [`clip_view`](host/engine_host.cpp) rewrites width and height, so it must not feed the sprite. The camera offset already applied to the solids applies here too.

Four `drawSprite` calls, one per layer, same origin, `flipX` when face is `-1`. The host builds a `Sprite` around the `uint16_t` rows from `body_sheet`. Both CMake target lists gain `game/body_sheet.c`.

On screen with debug: ground, black, white, and the four body slots. That is 7, under the cap of 8.

## Docs

Update [architecture.md](architecture.md) in the same change:

- Look: the figure is those four slots, not white. Sprites stay 1bpp. Animation here is idle, walk, run, the two turns, jump, wall slide, and wall-jump dash. No skeletal animation. 2bpp and 4bpp flags stay off.
- Physics: the pose is chosen at the end of the 16 ms tick, so present rate does not drive it.
- Memory: a flash row for the sheet, and the world-slice note lists face, clip, frame, and hold. The 4096 and the spare do not change.
- Decision log line for this.

[README.md](README.md) replaces the "one 16×16 rectangle" sentence with the posed figure, and says standing is idle, a slow floor speed is walk, full run is the run loop, reversing on the ground or in the air plays the matching turn once, and leaving a wall plays the dash sheet once.
