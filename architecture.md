# ESPecter architecture

Decision record for this repo. [initial_idea.md](initial_idea.md) stays the design proposal. When a decision here changes, update this file in the same change. That includes a slice size or a new resident in the memory table.

Hardware measurements live in the Waveshare board note `HARDWARE.md` for ESP32-S3-Touch-LCD-1.54. This file records what those facts force on the game.

## Language

Game code is C: plain structs, free functions, fixed-capacity SoA, no classes, templates, exceptions, RTTI, or STL containers. Names are standard-library style: `snake_case` types, functions, and files.

C++ exists only where PixelRoot32 forces it. The engine stays an unmodified C++17 library. [host/engine_host.cpp](host/engine_host.cpp) subclasses `Scene`. [host/st7789_drawer.cpp](host/st7789_drawer.cpp) subclasses `BaseDrawSurface`.

`game/` does not include PixelRoot32 or ESP-IDF.

## Layout

```text
ESPecter/
  CMakeLists.txt
  sdkconfig.defaults
  partitions.csv
  main/app_main.c
  game/
  host/
  compat/
  pc/
  pc.bat
  flash.bat
  monitor.bat
  third_party/pixelroot32/      # submodule, tag 1.12.0
  third_party/pixelroot32-apu/  # submodule, tag v2.0.0
```

The engine headers include `<pixelroot32/apu/ApuCore.h>` even when audio is compiled out, so the APU tree is pinned beside the engine. It is not a second game engine.

## Engine integration

PixelRoot32 1.12.0 has no ESP-IDF component. Its device drawer is TFT_eSPI and assumes a 40 MHz SPI ceiling. This panel is clear at SPI mode 3, 80 MHz, and a full RGB565 frame measured 12271 µs. There is no TE pin, so the glass cannot be vsynced.

The device build passes `DisplayType::CUSTOM` and [host/st7789_drawer.cpp](host/st7789_drawer.cpp). That drawer uses `esp_lcd` on SPI3, the same pins and mode as the board hello. [compat/Arduino.h](compat/Arduino.h) and [compat/SPI.h](compat/SPI.h) satisfy the engine's non-native includes. `ARDUINO_ARCH_ESP32` is not defined. The engine tree is not patched.

On this target [graphics/DisplayConfig.h](third_party/pixelroot32/include/graphics/DisplayConfig.h) names `DrawSurface` and [core/Engine.cpp](third_party/pixelroot32/src/core/Engine.cpp) aliases `pixelroot32::drivers` without opening that namespace. [host/pr32_config.h](host/pr32_config.h) is force-included and, for device C++ only, pulls in `graphics/DrawSurface.h` and opens `pixelroot32::drivers`. The same engine file calls `ESP.getFreeHeap` after [platforms/EngineConfig.h](third_party/pixelroot32/include/platforms/EngineConfig.h) includes `<Arduino.h>`. The shim's `ESP` reports the 8-bit heap. `millis`, `delay`, and the GPIO helpers are functions, so a method named `millis` in the engine still declares. The PC build defines `PLATFORM_NATIVE` and does not take this path.

Compile flags: C++17, no exceptions, no RTTI, `PIXELROOT32_NO_TFT_ESPI`, `PIXELROOT32_NO_I2S_AUDIO`, and `PIXELROOT32_ENABLE_AUDIO`, `PHYSICS`, `PARTICLES`, `UI_SYSTEM`, `TOUCH`, `SCENE_TRANSITIONS`, `CAMERA_EFFECTS`, and `STATIC_TILEMAP_FB_CACHE` all 0.

The PC build defines `PLATFORM_NATIVE` and uses the engine's SDL2 drawer. It does not compile the ST7789 drawer or the Arduino shim. The window title is ESPecter. Debug starts off. While it is on, both targets draw the present rate at the top-right, in the engine default `FONT_5X7` at size 1, and a white outline of each 240 cell that crosses the view. That glyph is 5×7 with 1 px of spacing, and size 1 is the smallest `drawText` still draws. The device also draws the pack percent at the top-left, same font and size, as `80%`. The PC build has no battery line. On the PC, backtick toggles the overlay and R resets the world.

## Hardware consequences

- Panel: write-only ST7789, 240×240 visible, RGB565, SPI mode 3 at 80 MHz. GPIO21 CS, GPIO38 CLK, GPIO39 MOSI, GPIO45 DC, GPIO40 RST, GPIO46 backlight. GPIO45 and GPIO46 are strapping pins. They are configured only after boot. No MISO. No TE. Tearing is a hardware fact. Dirty rectangles are the later mitigation.
- Backlight is LEDC low-speed channel 0 on GPIO46: 5 kHz, 10-bit. Duty starts at 0. The drawer fades it on after the panel is up. A full swing is requested at 420 ms. At 5 kHz the fade hardware holds each duty step for 2 cycles, so the glass takes about 410 ms. A reverse mid-fade keeps that duty per millisecond. PWR (GPIO5) is active low with an internal pull-up. The scan ignores it until the pin has been seen released, then toggles after two pressed samples 20 ms apart.
- One full RGB565 frame is 115200 bytes and about 12.3 ms on the wire. The drawer presents 1:1 through two 16-row DMA bands (15360 bytes): the idle band is filled while the other is on the wire. A second full frame is not reserved.
- Internal SRAM is 512 KB. DMA and the BLE controller live here. PSRAM cannot hold them.
- PSRAM is 8 MB octal. The game arena is one 64 KB block here. The rest stays free so a stray heap allocation is visible.
- Flash is 16 MB quad, not OPI. `sdkconfig.defaults` sets that.
- `BAT_EN` (GPIO2) is driven high before any blocking call. USB Serial/JTAG dies in deep sleep. There is no I2C fuel gauge. Deep sleep is not used.
- VBAT is GPIO1 on ADC1 channel 0, attenuation 6 dB. The divider is ×3 (R27 200 kΩ / R32 100 kΩ). The device averages eight calibrated reads about once a second and maps pack millivolts on a resting single-cell curve from 3.27 V (0%) to 4.20 V (100%). Load sags that number. While `CHG_STAT` (GPIO3) is low the cell is charging, so the same curve reads high. A failed ADC leaves the corner blank.
- One I2C bus (GPIO41/42): QMI8658A `0x6B`, CST816D `0x15`, ES8311 `0x18`, ES7210 `0x40`. No second bus. This binary does not init I2C, so those devices stay down until one owner exists. Touch is one finger. The IMU has no magnetometer, so yaw is not observable.
- PLUS is GPIO4, active low, with an internal pull-up, sampled on the same 20 ms task as BOOT. A press toggles the debug overlay. It starts off. BOOT is GPIO0, active low, configured as an input with pull-up after the app is up. One accepted press waits 400 ms. A second press in that window, after a release, terminates the Pad Emu link and scans again. If the window closes with one press, the host resets the world. The sample ignores each pin until it has been seen released, then accepts one press after two pressed samples 20 ms apart. Holding does not repeat. PWR (GPIO5) is the power latch, not a game button. A press fades the backlight off or back on.
- Wi-Fi is not started. On IDF 5.5 `ESP_WIFI_ENABLED` has no prompt, so the wifi library stays linked because the BT component requires it. This binary does not call `esp_wifi_init`, so that DMA is not allocated.
- The speaker and ES8311 exist. PixelRoot32's I2S backend is a bare DAC, not this codec. Audio is not in this binary. See the sound section.
- CPU is 240 MHz (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`). ESP-IDF is 5.5 or newer. Target `esp32s3`.

## Display

Logical resolution equals the panel: 240×240, no upscale. That fits more of a level on the square glass. The wire cost of a full present stays about 12.3 ms, because the panel always receives 240×240 RGB565.

The logical buffer is 240×240 8bpp, 57600 bytes, in internal RAM. The engine draws through `DrawSurface` and does not allocate this buffer itself. The ST7789 drawer does, because `DisplayType::CUSTOM` has no TFT sprite. Hot pixels stay out of PSRAM. The static tilemap framebuffer cache stays off. It would duplicate those 57600 bytes.

Each present the engine calls `clearBuffer` before the scene draws. That call does not wipe the 57600 indices, and it does not rebuild the palette. The palette is the fixed 256-entry table in `palette_rgb565`, so an index keeps its color. The scene's first draw is a full-view rectangle, so a second write of those bytes would sit on the CPU while the SPI bus is idle. A pixel left untouched keeps its previous index and the same color. `init` still wipes the indices once, before the panel is shown.

## Look

Reference is Nidhogg (2014), not Nidhogg 2. Flat colors, chunky silhouettes, a different small palette per level. Do not copy that game's sprites, palettes, or tracks.

- The glass palette is 256 RGB565 entries, sixteen ramps of sixteen, in `palette_rgb565`. Index 0 is black. A sprite index of 0 is skipped.
- This screen uses a small subset of that table: the ground, black platforms, white outlines, the magenta spike, and four body slots. The current sheets have no gradients and no dithering.
- Sprites are 8bpp indices. RGB565 is the palette entry and the wire format inside the drawer.
- Danger is one accent color, and those pixels are the hazard volume. The spike is magenta RGB565 `(31, 0, 31)`, the hot end of the hazard ramp. The body's red is the kiln fill. The checkpoint and the exit are white outlines.
- The body is an AABB. Nidhogg's pixel-precise sword hits are not the collision model.
- The sketch's wheel, spikes, and walkways become bold shapes.
- The view is square. The camera keeps the body on the horizontal middle. The ground under its feet stays 32 px above the bottom, so that slab fills the bottom of the view. A jump does not lift the view.
- The ninja is about 12 pixels tall inside a 16×16 cell. The AABB is 8×12, the idle body's opaque span, with the feet on the bottom of the cell and the body centered in it. The four fills are the sheet colors, pinned on leaf, glass, ember, and kiln: `(153, 230, 95)`, `(0, 205, 249)`, `(255, 162, 20)`, and `(245, 85, 93)`. The clips are idle, walk, jog, run, run-turn, jump, jump-turn, wall slide, and the dash sheet on a wall jump. No skeletal animation. `PIXELROOT32_ENABLE_2BPP_SPRITES` and `PIXELROOT32_ENABLE_4BPP_SPRITES` stay off. `getSpriteBuffer` stays null.

The figure is those four slots. Index 0 is skipped. Static platforms are black. On the PC the ground is dark green `0x13C2`. On the device the ground is the link, the same three flat colors as the pademu status bar: red `(180, 0, 0)` while the central is down, amber `(220, 180, 0)` while it scans, and green once Pad Emu is linked. That green is `(0, 140, 32)`, darker than the pademu bar `(0, 180, 40)`. Those grounds, the spike, white, and black are pinned in the table. With debug on, the screen is the ground, black, the four body slots, white, and that magenta.

## Sound

Daedelus's Nidhogg score is sample-based. PixelRoot32's pulse, triangle, and noise voices stay compiled out (`PIXELROOT32_ENABLE_AUDIO` is 0 for good).

Later music and effects are short PCM clips in flash, played through the ES8311. Gate PA GPIO7 only while sound is coming out. One owner for I2S. Stream from flash. A 16 kHz mono 16-bit clip is 32 KB per second. No MP3 decoder, and no decoded song kept in RAM. Silence is a valid mix. None of this is built now.

## Gamepad

The Q36XDV is not on the board. The chip is BLE only, so Classic HID is not an option. The Q36 report map is still unread. This binary does not connect to the first advertisement it sees.

The phone stand-in is what the device reads. The app name is Pad Emu. It advertises service `f31a0001-7c4e-4a2b-9e15-6d8f0b2c4a10` and notifies a 3-byte report on `f31a0002-7c4e-4a2b-9e15-6d8f0b2c4a10`: a little-endian `uint16` of buttons, then a hat (`0` up, then clockwise, `8` neutral). [main/pad_link.c](main/pad_link.c) is the NimBLE central. It scans for that service, subscribes to the report, and copies each notify into a static mailbox. A notify also marks the link up, and the 2 s watch leaves that phase alone. [main/actions_pad.c](main/actions_pad.c) calls `pad_link_read` from `actions_capture` and fills the same snapshot the PC keyboard fills. `game/` does not include that reader. While the link is down the mailbox is clear and the body keeps simulating. A drop clears the mailbox and the central scans again. Two presses of BOOT within 400 ms do the same when a vanished peer never delivers a disconnect: the one connection slot is dropped, the mailbox is cleared, and the central scans again. That reset runs on the NimBLE event queue. One press, after the 400 ms window, resets the world instead. A connect that completes after the press is terminated too. Advertisements are not duplicate-filtered, so the same phone is seen again. The connection uses a 2 s supervision timeout and a 10 s connect attempt, with no bonding. A host timer every 2 s restarts a scan that died, and abandons a connect or discover that has sat for 8 s. A linked report fills the held fields below. This stand-in has no stick.

`sdkconfig.defaults` enables NimBLE: central, observer, one connection, GATT client, host pinned to core 0, pool allocations in internal RAM. Bluedroid is off. This binary does not call `esp_wifi_init`. `app_main` raises `BAT_EN`, starts NVS, starts this central, then the arena, then the scene.

The PC build does not open a Bluetooth radio and does not link NimBLE or Bluedroid. DS4Windows owns the radio and, in its default profile, exposes an Xbox 360 pad. [pc/actions_ds4.c](pc/actions_ds4.c) is compiled only into the PC target. It reads the keyboard and the first SDL game controller, and ORs them. A virtual DualShock profile is not the one this sim supports. The device links [main/actions_pad.c](main/actions_pad.c), which reads the Pad Emu mailbox and does not read a keyboard. `game/` only sees the snapshot.

[game/actions.h](game/actions.h) is the shared snapshot. The host calls `actions_capture` once per sim tick. Held play buttons are `dpad_up`, `dpad_down`, `dpad_left`, `dpad_right`, `face_y`, `face_x`, `face_a`, `face_b`, `shoulder_l`, `trigger_l2`, `trigger_r2`, `shoulder_r`, `select`, `home`, and `start`. The reader then derives `move_up`, `move_down`, `move_left`, `move_right`, `jump`, `pause`, `confirm`, and `cancel`. The left stick past a dead zone also sets `move_left` / `move_right`. The Q36 and Pad Emu have no stick; that axis is only the Xbox stand-in.

Pad Emu report, while the link is up:

| Report | Snapshot |
| --- | --- |
| Hat north, and the diagonals beside it | `dpad_up` |
| Hat east, and the diagonals beside it | `dpad_right` |
| Hat south, and the diagonals beside it | `dpad_down` |
| Hat west, and the diagonals beside it | `dpad_left` |
| A, B, X, Y | `face_a` `face_b` `face_x` `face_y` |
| L, L2, R2, R | `shoulder_l` `trigger_l2` `trigger_r2` `shoulder_r` |
| View, Home, Menu | `select` `home` `start` |

Keyboard placement follows the Q36 face. The Type-C port, LED, reset hole, X/D/V slide, and Pair are not play buttons, so they have no key.

| Q36 | Key | Snapshot |
| --- | --- | --- |
| D-pad up, left, down, right | W A S D | `dpad_up` `dpad_left` `dpad_down` `dpad_right` |
| Y, X, A, B | I J K L | `face_y` `face_x` `face_a` `face_b` |
| L, L2 | Q E | `shoulder_l` `trigger_l2` |
| R2, R | U O | `trigger_r2` `shoulder_r` |
| View (−), Home, Menu (+) | F G H | `select` `home` `start` |

Derived, for either source:

- D-pad left and right: `move_left` / `move_right`. Opposite directions cancel. `move_up` stays in the snapshot and does not move the body. `move_down` does not move the body in open air; while held it releases the wall latch.
- Left stick past a dead zone: `move_left` / `move_right` as well.
- South button (`face_a`): `jump` and `confirm`. The body jumps on the rising edge while the floor contact of that tick is set, or while the wall latch is set and the body is off the floor. Holding it does not jump again. Releasing it while rising uses the heavier gravity. `move_down` on that tick releases the latch first, so the edge does not wall-jump, and the edge is still consumed.
- Menu (`start`): `pause`. B (`face_b`): `cancel`.
- No key and no controller on PC, or no Pad Emu link on the device: the snapshot stays clear. The body still falls and rests on the floor.

## Physics

N++ is the movement. The body is an 8×12 kinematic AABB. [game/world.c](game/world.c) integrates one fixed tick while alive: accelerate or friction on x, gravity on y, cap a latched fall, move x and respond, move y and respond, then a jump impulse, then the camera spring steps toward the horizontal middle and the ground line, then the pose. A pit or a spike stops that tick before the jump. `world_tick` takes no delta. One pixel is 256 subpixels. Position is a cell index plus a local offset in `[0, 240<<8)`. A negative cell uses division toward −∞. The cell is 240 px, one view. Velocity, the four face contacts, the wall latch (`-1` left, `1` right, `0` none), and the pose stay on the first world row. The camera origin and its velocity, in world subpixels, the held ground line, life, the death hold, and the checkpoint stand sit after that SoA. Up to 256 static AABBs live in the level slice as a cell, a local pixel origin in `[0, 240)`, and a width and height that may cross cells. Ten are used: two floors, two tall walls, and six platforms.

The host turns wall-clock milliseconds into ticks. `Engine::update` passes a whole-millisecond delta. The host adds it to an accumulator and calls `world_tick` once per 16 ms, at most four times per update. A zero delta adds nothing. Time left under 16 ms stays. If four ticks leave 16 ms or more, that remainder is dropped. Sixteen milliseconds is the step this clock can mark, 62.5 Hz, not 1000/60. Present rate does not scale velocity. The number in the corner is presents, not sim ticks.

Feel, per tick: horizontal accel and friction 64 subpixels, max run 768 (3 px, about 188 px/s). Jump impulse −2048. Gravity 128 while rising with jump held, 256 otherwise. Fall caps at 2048, under the body, so a later 16 px tile is not skipped in one tick. A held jump reaches about 64 px. Releasing jump early uses the heavier gravity. While the wall latch holds and the body is falling, vy caps at 256 (1 px), starting the tick after the grab. A wall jump sets vx away from that edge and vy to −1448, the integer of 2048 / sqrt(2). Held, that rise is about 32 px; releasing early is about half. While |vx| is above 768, x input is ignored and vx loses 64 per tick down to the run cap. Only the wall jump produces that speed. This is the cage tune.

The static AABBs stop the body and zero that axis. There is no view border. No bounce. Overlap on an axis is the strict interval test from [N Tutorial A](https://www.metanetsoftware.com/2016/n-tutorial-a-collision-detection-and-response) section 2: touching an edge is not a hit, so a body standing on a top does not grab the sides. The body moves on x and projects on x only, then moves on y and projects on y only. Solving the axes apart keeps a deep fall from being shoved sideways, which section 4 shows the shortest overlap can do. The stop is the face crossed on that move, so a rect thinner than the step is not tunneled (section 5). A body that starts the tick inside a solid is pushed out on the current axis through the shorter face. The shallowest solid goes first, and that repeats until the body is out. Crossing a left face of a solid sets the wall latch to −1 and zeros vx. A right face sets it to 1. While the latch holds, left and right do not change vx. A ceiling, a solid's underside, zeros vy and leaves the latch. A floor, a solid's top, clears the latch, so a corner jump is the floor jump. After the vertical move, the latch stays only while a one-subpixel nudge into that side still overlaps a face. Sliding off the bottom of a solid releases it on that tick, before the jump. `move_down` held clears the latch, and a stop on that tick does not set it again. Releasing down does not re-latch until vx crosses that face again. Holding into the wall after the release does that on the following tick. D-pad up does not move the body. With a clear snapshot the body still falls and rests on the floor, because the spawn column, x 96–144 of cell `(0, 0)`, is clear of the platforms.

The course is fixed. There is no LCG. Two floors share the line y 224, height 32. The first is cell `(0, 0)`, local `(0, 224)`, 240×32. The second is cell `(1, 0)`, local `(48, 224)`, 240×32, world x 288 through 528. The 48 px between them is the pit. Past either floor, or off the left of cell 0, feet below y 256 are death.

Two walls flank the spawn column. Each is 16 px wide, from y 16 down to y 200, so the body can walk underneath and a jump from the floor meets the face. The left wall is local x 80, so its right face is x 96. The right wall is local x 144. The column interior, x 96–144, stays empty, and the 48 px between the faces is short enough that a wall jump reaches the other face. The slide down that face is most of the cell. Three platforms of 16 px stack in each side pocket, outside the walls. Left: `(8, 184, 64, 16)`, `(8, 144, 56, 16)`, `(12, 104, 48, 16)`. Right: `(168, 184, 64, 16)`, `(168, 144, 56, 16)`, `(176, 104, 48, 16)`. Each top is 40 px above the one below. A held jump reaches about 64 px, so each step is reachable from the one below, and the low one from the floor.

The spike, the checkpoint, and the exit are not solids. Overlap uses the same strict test, after the vertical move and before the jump. The spike is cell `(1, 0)`, local `(112, 216, 48, 8)`: world x 352–400, the bottom 8 px of a body standing on that floor. About 64 px of floor sits between the pit and the spike. The checkpoint volume is cell `(1, 0)`, local `(176, 176, 16, 48)`. Touching it stores the authored stand, cell `(1, 0)`, local x 180 and y 212, feet on the floor. It does not store the live position. Spawn, the center of cell `(0, 0)`, is that checkpoint until then. The exit is cell `(2, 0)`, local `(16, 192, 16, 32)`, world x 496–512, on the second floor.

Feet below y 256, or a spike overlap, sets life to dying and the hold to 16. That tick stops before the jump, the camera, and the pose, so the body stays on the frame it died in. While dying, the tick only counts down. Input is ignored and the camera stays. At 0 the body is placed on the stored checkpoint the way spawn is placed: velocity, latch, pose, and contacts cleared, camera snapped, spring stopped. A jump still held on that tick does not fire. Death does not rebuild the room. The exit zeros velocity and sets life to finished. Later ticks return immediately. `world_reset` reloads the same solids, clears the checkpoint back to spawn, and clears life.

The horizontal target puts the body center at the middle of the view. The vertical target puts the held ground line 32 px above the bottom. That line is the feet on a floor contact, sampled before the jump can clear the contact, and it also drops when the feet fall past it. A jump leaves the line where it was, so the floor stays put and the body rises into the view. Each tick adds half of the remaining gap to the camera velocity, then keeps three eighths of that velocity. A 120 px gap settles in about 9 ticks with no overshoot. While running, the body sits about 9 px off the horizontal middle. `world_reset` snaps the origin onto that target and zeros the velocity. The host copies that origin into `Renderer::setDisplayOffset` once per present. Solids are clipped to the camera view before the draw. The spike is filled the same way, in magenta RGB565 `(31, 0, 31)`. The checkpoint and the exit are white outlines when they cross the view. The 16×16 sprite is drawn so its feet and center sit on the AABB, when that cell crosses the view. Index 0 is skipped, and so is a pixel that falls outside the view. The backdrop, the battery line, and the present rate use `setOffsetBypass`. The debug wireframe is `drawRectangle` on each 240 cell that crosses the view.

The pose is chosen at the end of the 16 ms tick, so the present rate does not drive it. A latch plays wall slide and faces away from that wall. A wall jump starts the dash sheet at the first cell; when that one-shot ends in the air, jump holds its last cell. Landing cancels dash, jump, and jump-turn. An open run-turn, jump-turn, or dash otherwise keeps advancing. A sign change of vx, with both the old and the new value non-zero, starts run-turn on the floor and jump-turn in the air. Leaving a stand is not a turn. Still airborne, jump plays once from the first cell and then holds the last. On the floor, speed at the run cap is run, speed from half that cap up to it is jog, a slower non-zero speed is walk, and zero is idle. Walk, jog, and run keep the frame across those lines. Each cell stays up for the time in the showcase sheets: walk, jump, and wall slide 120 ms, jog 80 ms, run, run-turn, and dash 60 ms, and jump-turn 60 ms then 120 ms. The 16 ms tick adds to that hold and keeps the leftover, so a 120 ms cell alternates 7 and 8 ticks and a 60 ms cell runs 4, 4, 4, 3. Idle does not advance. The sheets face right, and the host mirrors x when the face is −1. Run-turn and jump-turn are stored facing right, so that mirror is the only one. The blit writes each nonzero index through `drawPixel`. `SpriteAnimation` stays unused: it has no duration and no one-shot. `drawMultiSprite` cannot flip. `getSpriteBuffer` stays null, because the device buffer is a palette index and the engine fast path would write a packed byte instead.

Square tiles, wedges, and one-way platforms are still later.

Mechanic reference, not code to port: `d:\Repos\refs\school-n--master`. The latch nudge is that demo's probe into the wall normal, and an inside push takes the shallowest overlap. Do not take its classes, `std::vector`, per-overlap `new`, variable `deltaTime`, skeleton, or its speed constants.

`PIXELROOT32_ENABLE_PHYSICS` stays 0. One collision truth, and it is game code. The ninja is kinematic. SoA rows live in the arena.

## Memory

Three stores:

| Store | Size | What stays there |
| --- | ---: | --- |
| Internal SRAM | 512 KB | BLE controller and host, FreeRTOS stacks, DMA present band, logical 8bpp buffer |
| PSRAM | 8 MB octal | The game arena, and nothing else from this binary |
| Flash | 16 MB quad | Code, read-only data, later PCM |

Who may allocate:

- ESP-IDF and PixelRoot32 allocate their own residents from the heap. Those blocks are not inside the game arena.
- Game code allocates only through `arena`. There is no `arena_free`. The block is taken once at boot and never returned. There is no rewind. Per-frame work overwrites the scratch slice in place.

`arena` is a base, a capacity, and an offset. A slice bumps that offset, aligned to 8 bytes. If the offset would pass the capacity, boot fails.

The device block comes from `heap_caps_malloc` with `MALLOC_CAP_SPIRAM`, after BLE is up. The PC build uses one `malloc` of the same size, also never freed. Same carve order on both.

| Resident | Where | Bytes | Owner | Notes |
| --- | --- | ---: | --- | --- |
| BLE controller and host | internal | unmeasured | ESP-IDF | NimBLE, internal pools. Fill from the first boot log |
| FreeRTOS stacks, including the BT host task | internal | unmeasured | ESP-IDF | |
| Present band | internal, DMA | 15360 | ST7789 drawer | Two 16-row bands of 240 RGB565. The idle band fills during the other's DMA |
| Backlight task stack | internal | 3072 | ST7789 drawer | Polls PWR. The fade itself is LEDC hardware |
| Pad reset task stack | internal | 2048 | `pad_link` | Polls BOOT and PLUS. A double BOOT runs on the NimBLE event queue. A single BOOT is a mailbox the host reads |
| Logical buffer | internal | 57600 | ST7789 drawer | 240×240 8bpp. Not a second RGB565 frame |
| Body sheet | flash | 9984 | `body_sheet` | 39 frames, 8bpp indices, 16×16. Index 0 is skip |
| Palette | flash | 512 | `palette` | 256 RGB565 entries. The ST7789 drawer reads this table |
| Game arena | PSRAM | 65536 | `arena` | Never freed |
| `world` slice | arena | 4096 | `world` | SoA. The body keeps a cell index, local subpixel position, velocity, face contact, the wall latch, and the pose (face, clip, frame, and hold) on the first row. The camera origin, its velocity, the held ground line, life, the death hold, and the checkpoint stand sit after the rows |
| `level` slice | arena | 32768 | `world` | SoA of up to 256 static AABBs, 6144 bytes. Ten are used |
| `scratch` slice | arena | 16384 | reserved | Overwritten in place. The offset does not move |
| Spare | arena | 12288 | `arena` | Growing a slice takes from here and updates this table |

Boot log, after the engine buffer exists: free internal heap, largest free internal block, free PSRAM, arena capacity, arena offset.

## Runtime

`app_main` drives `BAT_EN` high, opens the VBAT ADC, starts NVS and the NimBLE central, creates the arena, then `engine_host_run`. On the device, `Engine::run` paints one frame and returns, so the host calls it in a loop. On the PC, that call already loops until the window closes. The drawer fades the backlight on after the panel is up. PWR fades it off or back on. The PC `main` creates the arena and calls the same host. Neither target inits I2C, Wi-Fi, the codec, or the IMU.

## Decision log

| Date | Decision |
| --- | --- |
| 2026-10-15 | ESP-IDF 5.5 or newer, target `esp32s3`, 16 MB quad flash, octal PSRAM, 240 MHz |
| 2026-10-15 | PixelRoot32 1.12.0 as a submodule, plus PixelRoot32-APU v2.0.0 because the headers require it. Custom `esp_lcd` drawer and an Arduino shim. The engine tree is not patched |
| 2026-10-15 | Logical and physical resolution 240×240, no scale |
| 2026-10-15 | Physics is a later kinematic step from Metanet tutorial A. Engine physics stays compiled out. The school demo is a mechanic reference only |
| 2026-10-15 | Bluedroid BLE-only is on from the first device boot. Wi-Fi is off. HID report map is still open. PC reads a DS4Windows Xbox pad and does not open a radio |
| 2026-10-15 | PC keyboard ORs into the same snapshot as the Xbox pad. Keys follow the Q36 face. Pair, the mode slide, the LED, the reset hole, and Type-C are not play buttons. The device snapshot stays clear |
| 2026-10-15 | Device BLE host is NimBLE central for Pad Emu. The Q36 HID map stays unread. Wi-Fi stays off. A linked report fills the snapshot |
| 2026-10-15 | Device ground shows the Pad Emu link. Linked green is `(0, 140, 32)`. A drop scans again, without the duplicate filter, and retries a refused scan |
| 2026-10-15 | Pad link matches pademu reconnect: 2 s supervision, no bonding, and a 2 s watch that restarts a dead scan or an attempt stuck for 8 s |
| 2026-10-15 | Device host repeats `Engine::run`. That call is one frame on the device; the PC call already loops |
| 2026-10-15 | Fixed timestep length and feel constants stay open until something actually jumps |
| 2026-10-15 | One 64 KB PSRAM arena, carved once, never freed. BLE's byte cost stays unmeasured until the boot log |
| 2026-10-15 | Nidhogg 1 art: 4-color screens, 1bpp or 2bpp, no copied assets. NES audio voices stay off. Later sound is PCM on the ES8311 |
| 2026-10-15 | Touch and IMU stay off. Frame-time budgets stay open |
| 2026-10-15 | Backlight is an LEDC fade on GPIO46. PWR fades it off and on. PLUS and BOOT stay unread |
| 2026-10-15 | Build scripts live at the repo root. The PC window is titled ESPecter and draws its current frame rate |
| 2026-10-15 | Sim tick is 16 ms of wall time, at most 4 per update. The first cage tune is in the physics section. Present rate does not scale the body |
| 2026-10-15 | Both targets draw the present rate at the top-right in `FONT_5X7` size 1 |
| 2026-10-15 | BOOT (GPIO0) terminates the Pad Emu link and scans again. PLUS stays unread |
| 2026-10-15 | Present uses two 16-row DMA bands so the next band fills while the previous is on the wire |
| 2026-10-15 | Wall latch on the side edges. Slide cap 256. Wall jump is 1448 on the diagonal. Speed above the run cap bleeds by 64. move_down releases the latch |
| 2026-10-15 | Per-frame clear resets the palette and leaves the 8bpp indices. The full-view ground is the wipe |
| 2026-10-15 | Device draws battery percent at the top-left from the GPIO1 divider. No fuel gauge. The PC build has no battery line |
| 2026-10-15 | Static AABBs, cap 32, in the level slice. Four faces use the cage latch, floor, and ceiling. The sweep stops on the face crossed that tick |
| 2026-10-15 | Positions are a 240 cell plus a local offset. No view borders. Solids cap 256. One 17-cell floor and four platforms re-rolled by `world_reset`. The camera slides to the body-center cell. PLUS toggles debug. One BOOT resets the world; two within 400 ms reset Pad Emu |
| 2026-10-16 | The camera springs toward the body center. Each tick adds a twelfth of the gap to the velocity and keeps six eighths of it |
| 2026-10-16 | The body is four 1bpp layers from the No Outlines sheets. The pose is chosen on the 16 ms tick. 2bpp and 4bpp stay off |
| 2026-10-16 | The body is one 8bpp sheet in a fixed 256-color palette. Index 0 is skip. clearBuffer leaves that table in place. 2bpp and 4bpp stay off |
| 2026-10-16 | Pose holds follow the showcase sheets. Walk, jump, and wall slide are 120 ms, jog is 80 ms, run, run-turn, and dash are 60 ms, and jump-turn is 60 ms then 120 ms. Half the run cap plays jog |
| 2026-10-16 | `world_reset` re-rolls two tall walls beside the spawn column and six platforms in the side pockets. The column interior stays clear |
| 2026-10-16 | The camera keeps the body on the horizontal middle and the ground line 32 px above the bottom. A jump does not lift the view. The spring takes half the gap and keeps three eighths of the velocity |
| 2026-10-16 | The AABB is 8×12, centered on the 16×16 cell with the feet on the bottom row. Wall slide faces away from the latch |
| 2026-10-16 | The course is fixed: a wall-jump slot, a 48 px pit, one spike, a checkpoint, and an exit. Death holds 16 ticks, then the last checkpoint. The exit freezes until reset. The LCG is gone |
