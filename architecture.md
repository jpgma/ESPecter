# ESPecter architecture

Decision record for this repo. [initial_idea.md](initial_idea.md) stays the design proposal. When a decision here changes, update this file in the same change. That includes a slice size or a new resident in the memory table.

Hardware measurements live in the Waveshare board note `HARDWARE.md` for ESP32-S3-Touch-LCD-1.54. This file records what those facts force on the game.

## Language

Game code is C: plain structs, free functions, fixed-capacity SoA, no classes, templates, exceptions, RTTI, or STL containers. Names are standard-library style: `snake_case` types, functions, and files.

C++ exists only where PixelRoot32 forces it. The vendored engine stays C++17 and may be patched. [host/engine_host.cpp](host/engine_host.cpp) subclasses `Scene`. [host/st7789_drawer.cpp](host/st7789_drawer.cpp) subclasses `BaseDrawSurface`. New C++ we write stays in those two files.

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
  third_party/pixelroot32/      # vendored 1.12.0, 826be881, include and src
  third_party/pixelroot32-apu/  # vendored v2.0.0, b603b3a9, include and src
```

The engine headers include `<pixelroot32/apu/ApuCore.h>` even when audio is compiled out, so the APU tree is vendored beside the engine. It is not a second game engine.

## Engine integration

PixelRoot32 1.12.0 has no ESP-IDF component. Its device drawer is TFT_eSPI and assumes a 40 MHz SPI ceiling. This panel is clear at SPI mode 3, 80 MHz, and a full RGB565 frame measured 12271 µs. There is no TE pin, so the glass cannot be vsynced.

The device build passes `DisplayType::CUSTOM` and [host/st7789_drawer.cpp](host/st7789_drawer.cpp). That drawer uses `esp_lcd` on SPI3, the same pins and mode as the board hello. [compat/Arduino.h](compat/Arduino.h) and [compat/SPI.h](compat/SPI.h) satisfy the engine's non-native includes. `ARDUINO_ARCH_ESP32` is not defined. The trees may be patched. This vendor step does not change engine behavior. The device `Engine::run` is patched to skip `update`. The native `run` still updates, then draws.

On this target [graphics/DisplayConfig.h](third_party/pixelroot32/include/graphics/DisplayConfig.h) names `DrawSurface` and [core/Engine.cpp](third_party/pixelroot32/src/core/Engine.cpp) aliases `pixelroot32::drivers` without opening that namespace. [host/pr32_config.h](host/pr32_config.h) is force-included and, for device C++ only, pulls in `graphics/DrawSurface.h` and opens `pixelroot32::drivers`. The same engine file calls `ESP.getFreeHeap` after [platforms/EngineConfig.h](third_party/pixelroot32/include/platforms/EngineConfig.h) includes `<Arduino.h>`. The shim's `ESP` reports the 8-bit heap. `millis`, `delay`, and the GPIO helpers are functions, so a method named `millis` in the engine still declares. The PC build defines `PLATFORM_NATIVE` and does not take this path.

Compile flags: C++17, no exceptions, no RTTI, `PIXELROOT32_NO_TFT_ESPI`, `PIXELROOT32_NO_I2S_AUDIO`, and `PIXELROOT32_ENABLE_AUDIO`, `PHYSICS`, `PARTICLES`, `UI_SYSTEM`, `TOUCH`, `SCENE_TRANSITIONS`, `CAMERA_EFFECTS`, and `STATIC_TILEMAP_FB_CACHE` all 0.

The PC build defines `PLATFORM_NATIVE` and uses the engine's SDL2 drawer. It does not compile the ST7789 drawer or the Arduino shim. The window title is ESPecter. Debug starts off. While it is on, both targets draw the present rate at the top-right, in the engine default `FONT_5X7` at size 1, and a white outline of each 240 cell that crosses the view. That glyph is 5×7 with 1 px of spacing, and size 1 is the smallest `drawText` still draws. The device also draws the pack percent at the top-left, same font and size, as `80%`. The PC build has no battery line. On the PC, backtick toggles the overlay and R resets the world. The number row stands in for the Waveshare buttons, in the order PWR, PLUS, BOOT. Key 1 toggles the stand-in backlight: the window goes black and, 30 s later, the sim stops until key 1 again. There is no fade and no light sleep. Key 2 toggles the overlay. Key 3 is BOOT: one press resets the world after 400 ms, and a second press in that window does not. Holding a key does not repeat.

## Hardware consequences

- Panel: write-only ST7789, 240×240 visible, RGB565, SPI mode 3 at 80 MHz. GPIO21 CS, GPIO38 CLK, GPIO39 MOSI, GPIO45 DC, GPIO40 RST, GPIO46 backlight. GPIO45 and GPIO46 are strapping pins. They are configured only after boot. No MISO. No TE. Tearing is a hardware fact. Present ships dirty rows and skips SPI when none changed.
- Backlight is LEDC low-speed channel 0 on GPIO46: 5 kHz, 10-bit. Duty starts at 0. The drawer fades it on after the panel is up. A full swing is requested at 420 ms. At 5 kHz the fade hardware holds each duty step for 2 cycles, so the glass takes about 410 ms. A reverse mid-fade keeps that duty per millisecond. PWR (GPIO5) is active low with an internal pull-up. The scan ignores it until the pin has been seen released, then toggles after two pressed samples 20 ms apart.
- One full RGB565 frame is 115200 bytes and about 12.3 ms on the wire. The drawer keeps two 120-row DMA bands, which together are that frame, and uses them as the bounce for the rows the mask names. The idle band fills while the other is on the wire. `app_main` takes one internal arena before the NimBLE central, while the 265 KiB internal hole is still one block. The pair and the 57600-byte 8bpp buffer are the first two slices of that block. After the central the leftover heap is smaller than that buffer. A second copy is not reserved. A band is 57600 bytes, so each color transfer is split at the 32768-byte DMA cap under one RAMWR.
- Internal SRAM is 512 KB. DMA and the BLE controller live here. PSRAM cannot hold them.
- PSRAM is 8 MB octal. The game arena is one 64 KB block here. The rest stays free so a stray heap allocation is visible.
- Flash is 16 MB quad, not OPI. `sdkconfig.defaults` sets that.
- `BAT_EN` (GPIO2) is driven high before any blocking call. USB Serial/JTAG dies while the chip is in light sleep. There is no I2C fuel gauge. Deep sleep is not used: it reboots the chip and drops the PSRAM arena. Light sleep keeps that block.
- VBAT is GPIO1 on ADC1 channel 0, attenuation 6 dB. The divider is ×3 (R27 200 kΩ / R32 100 kΩ). The device averages eight calibrated reads about once a second and maps pack millivolts on a resting single-cell curve from 3.27 V (0%) to 4.20 V (100%). Load sags that number. While `CHG_STAT` (GPIO3) is low the cell is charging, so the same curve reads high. A failed ADC leaves the corner blank.
- One I2C bus (GPIO41/42): QMI8658A `0x6B`, CST816D `0x15`, ES8311 `0x18`, ES7210 `0x40`. No second bus. This binary does not init I2C, so those devices stay down until one owner exists. Touch is one finger. The IMU has no magnetometer, so yaw is not observable.
- PLUS is GPIO4, active low, with an internal pull-up, sampled on the same 20 ms task as BOOT. A press toggles the debug overlay. It starts off. BOOT is GPIO0, active low, configured as an input with pull-up after the app is up. One accepted press waits 400 ms. A second press in that window, after a release, terminates the Pad Emu link and scans again. If the window closes with one press, the host resets the world. The sample ignores each pin until it has been seen released, then accepts one press after two pressed samples 20 ms apart. Holding does not repeat. PWR (GPIO5) is the power latch, not a game button. A press fades the backlight off or back on.
- Wi-Fi is not started. On IDF 5.5 `ESP_WIFI_ENABLED` has no prompt, so the wifi library stays linked because the BT component requires it. This binary does not call `esp_wifi_init`, so that DMA is not allocated.
- The speaker and ES8311 exist. PixelRoot32's I2S backend is a bare DAC, not this codec. Audio is not in this binary. See the sound section.
- CPU is 240 MHz (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`). ESP-IDF is 5.5 or newer. Target `esp32s3`.

## Display

Logical resolution equals the panel: 240×240, no upscale. That fits more of a level on the square glass. A full RGB565 frame is still about 12.3 ms on the wire. Present does not send one when the picture held. A 240-bit row mask, 30 bytes, static in the drawer, names the rows that changed. `sendBuffer` expands those runs through the palette into the idle 120-row band and DMA-ships that window. A run longer than 120 rows is split. The other band stays on the wire while the next run expands. `sendBuffer` returns with the last band still on the wire. The next present draws the same indices while that DMA finishes. A band is filled again only after its semaphore. An empty mask does not draw and does not call SPI, and it leaves the in-flight count alone. The present task then waits 1 ms. A frame with any dirty row does not take that wait.

The logical buffer is 240×240 8bpp, 57600 bytes, a slice of the internal arena. The engine draws through `DrawSurface` and does not allocate this buffer itself. The drawer reads that slice, because `DisplayType::CUSTOM` has no TFT sprite. Hot pixels stay out of PSRAM. The static tilemap framebuffer cache stays off. It would duplicate those 57600 bytes. The row mask is not a second frame.

Each present the engine calls `clearBuffer` before the scene draws. That call does not wipe the 57600 indices, and it does not rebuild the palette. The palette is the fixed 256-entry table in `palette_rgb565`, so an index keeps its color. On the device the scene's ground rectangle only lands on dirty rows, which restores them. Solids, the spike, the outlines, and the body are drawn after that, and the drawer ignores a clean row. A pixel left untouched keeps its previous index, and the glass keeps that color. `init` still wipes the indices once, before the panel is shown.

The mask is geometric, from the snapshot the sim already publishes. The host keeps the last camera, ground color, body pose and rect, spike, checkpoint, exit, shuttle, disk phase, and debug flag. There is no second copy of the solids. The first frame, a ground-color change, and the panel init after light sleep mark all 240 rows. A camera move marks the old and new screen Y of each solid, the spike, the outlines, the shuttle, the disk cap, and the body. A still camera marks only the old and new Y of the body, a hazard that moved, the shuttle, or the disk cap when the phase changed. Debug on with a camera move, or a debug toggle, marks all 240 rows, because the cell wireframe has vertical lines. Debug text with the camera still marks the top 8 rows, the `FONT_5X7` line.

While a USB host is on the cable, the present task prints one second for each core. The clock is `esp_timer_get_time`. Core 1 lines are `mask`, `ground`, `solids`, `body`, `debug`, `expand`, `spi` (the wait to reuse a band, counted on the loop that fills it), `engine` (the loop minus those), and `wait` (the 1 ms empty sleep). Their percents add to that core's measured loops. The header is that total, the loop count, the empty-mask count, the busy count, and the mean and max dirty rows of the busy frames only. Core 0 lines are `wait` (the 1 ms notify), `sim` (`sim_advance`), and `other`. Their percents add to Core 0. The print itself is a `core1 log` line and is outside both totals. The gate is the SOF frame index, sampled once a second. `usb_serial_jtag_is_connected` stays unused. A power bank sends no SOF, so the timers stay off. Closing the monitor while the cable stays in the PC keeps the log on. While the gate is off, a section call is one flag check.

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

The figure is those four slots. Index 0 is skipped. Static platforms are black, and so is the 4 px one-way lip. On the PC the ground is dark green `0x13C2`. On the device the ground is the link, the same three flat colors as the pademu status bar: red `(180, 0, 0)` while the central is down, amber `(220, 180, 0)` while it scans, and green once Pad Emu is linked. That green is `(0, 140, 32)`, darker than the pademu bar `(0, 180, 40)`. Those grounds, the spike, white, and black are pinned in the table. With debug on, the screen is the ground, black, the four body slots, white, and that magenta.

## Sound

Daedelus's Nidhogg score is sample-based. PixelRoot32's pulse, triangle, and noise voices stay compiled out (`PIXELROOT32_ENABLE_AUDIO` is 0 for good).

Later music and effects are short PCM clips in flash, played through the ES8311. Gate PA GPIO7 only while sound is coming out. One owner for I2S. Stream from flash. A 16 kHz mono 16-bit clip is 32 KB per second. No MP3 decoder, and no decoded song kept in RAM. Silence is a valid mix. None of this is built now.

## Gamepad

The Q36XDV is not on the board. The chip is BLE only, so Classic HID is not an option. The Q36 report map is still unread. This binary does not connect to the first advertisement it sees.

The phone stand-in is what the device reads. The app name is Pad Emu. It advertises service `f31a0001-7c4e-4a2b-9e15-6d8f0b2c4a10` and notifies a 3-byte report on `f31a0002-7c4e-4a2b-9e15-6d8f0b2c4a10`: a little-endian `uint16` of buttons, then a hat (`0` up, then clockwise, `8` neutral). [main/pad_link.c](main/pad_link.c) is the NimBLE central. It scans for that service, subscribes to the report, and copies each notify into a static mailbox. A notify also marks the link up, and the 2 s watch leaves that phase alone. [main/actions_pad.c](main/actions_pad.c) calls `pad_link_read` from `actions_capture` and fills the same snapshot the PC keyboard fills. `game/` does not include that reader. While the link is down the mailbox is clear and the body keeps simulating. A drop clears the mailbox and the central scans again. Two presses of BOOT within 400 ms do the same when a vanished peer never delivers a disconnect: the one connection slot is dropped, the mailbox is cleared, and the central scans again. That reset runs on the NimBLE event queue. One press, after the 400 ms window, resets the world instead. A connect that completes after the press is terminated too. Advertisements are not duplicate-filtered, so the same phone is seen again. The connection uses a 2 s supervision timeout and a 10 s connect attempt, with no bonding. A host timer every 2 s restarts a scan that died, and abandons a connect or discover that has sat for 8 s. A linked report fills the held fields below. This stand-in has no stick.

`sdkconfig.defaults` enables NimBLE: central, observer, one connection, GATT client, host pinned to core 0, pool allocations in internal RAM. Bluedroid is off. This binary does not call `esp_wifi_init`. `app_main` raises `BAT_EN`, starts NVS, starts this central, then the arena, then the scene. The sim and present tasks run at priority 2, under that host. Light sleep stops the central and brings it back scanning on wake.

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

- D-pad left and right: `move_left` / `move_right`. Opposite directions cancel. `move_up` stays in the snapshot and does not move the body. `move_down` does not move the body in open air; while held it releases the wall latch and falls through a one-way top.
- Left stick past a dead zone: `move_left` / `move_right` as well.
- South button (`face_a`): `jump` and `confirm`. The body jumps on the rising edge while the floor contact of that tick is set, or while the wall latch is set and the body is off the floor. Holding it does not jump again. Releasing it while rising uses the heavier gravity. `move_down` on that tick releases the latch first and skips one-way tops, so the edge does not wall-jump or jump off that lip, and the edge is still consumed.
- Menu (`start`): `pause`. B (`face_b`): `cancel`.
- No key and no controller on PC, or no Pad Emu link on the device: the snapshot stays clear. The body still falls and rests on the floor.

## Physics

N++ is the movement. The body is an 8×12 kinematic AABB. [game/world.c](game/world.c) integrates one fixed tick while alive: accelerate or friction on x, gravity on y, cap a latched fall, move x and respond, move y and respond, then a jump impulse, then the camera spring steps toward the horizontal middle and the ground line, then the pose. A pit or a spike stops that tick before the jump. `world_tick` takes no delta. One pixel is 256 subpixels. Position is a cell index plus a local offset in `[0, 240<<8)`. A negative cell uses division toward −∞. The cell is 240 px, one view. Velocity, the four face contacts, the wall latch (`-1` left, `1` right, `0` none), and the pose stay on the first world row. The camera origin and its velocity, in world subpixels, the held ground line, life, the death hold, the checkpoint stand, the shuttle x, its direction, the rider, the disk phase, the disk rider, and its tangent step sit after that SoA. The strip in [game/map.c](game/map.c) is the course. A tile is 16 px and one kind-byte: empty, set, spike, checkpoint, exit, one-way, or a floor slope. The sim keeps nine of those bytes in the level slice, the tile under the body center and the eight around it, and reloads them from the table when that center tile changes. Present does not read them.

The host turns wall-clock milliseconds into ticks. On the PC, `Engine::update` passes a whole-millisecond delta. On the device, the sim task reads `millis` itself. The host adds that delta to an accumulator and calls `world_tick` once per 12 ms, at most four times per wake. A zero delta adds nothing. Time left under 12 ms stays. If four ticks leave 12 ms or more, that remainder is dropped. Twelve milliseconds is the whole millisecond at the glass: a full frame is about 12.3 ms on the wire, about 81 Hz, and this clock cannot mark 12.3. The step is 83 Hz. Present rate does not scale velocity. The number in the corner is presents, not sim ticks. A light sleep does not feed the accumulator: the clock restarts when the sim resumes.

Feel is in pixels per second. `PIXPS_TO_SUBPIXPT` scales a speed onto the 12 ms step, and `PER_TICK2` scales an acceleration, because that one follows the square of the step. Horizontal accel and friction are 977 px/s². Max run is 171 px/s. Jump impulse is 500 px/s. Gravity is 1954 px/s² while rising with jump held, 3907 px/s² otherwise. Fall caps at 500 px/s, 6 px on this tick, under the body and under a 16 px tile. A held jump reaches about 64 px. Releasing jump early uses the heavier gravity. While the wall latch holds and the body is falling, vy caps at 62.5 px/s. A wall jump sets vx away from that edge and vy to 366.2 px/s. Held, that rise stays the arc of the previous tune; releasing early is about half. While speed is above the run cap, x input is ignored and vx bleeds by the run accel down to the cap. Only the wall jump produces that speed. This is the cage tune.

A set tile stops the body and zeros that axis. There is no view border. No bounce. Overlap on an axis is the strict interval test from [N Tutorial A](https://www.metanetsoftware.com/2016/n-tutorial-a-collision-detection-and-response) section 2: touching an edge is not a hit, so a body standing on a top does not grab the sides. The body moves on x and projects on x only, then moves on y and projects on y only. Solving the axes apart keeps a deep fall from being shoved sideways, which section 4 shows the shortest overlap can do. The stop is the face crossed on that move, so a rect thinner than the step is not tunneled (section 5). A body that starts the tick inside a set tile or the shuttle is pushed out on the current axis through the shorter face. The shallowest of those goes first, and that repeats until the body is out. Crossing a left face of a set tile or of the shuttle sets the wall latch to −1 and zeros vx. A right face sets it to 1. While the latch holds, left and right do not change vx. A ceiling, a set tile's underside, zeros vy and leaves the latch. A floor, a solid's top, clears the latch, so a corner jump is the floor jump. After the vertical move, the latch stays only while a one-subpixel nudge into that side still overlaps a face. Sliding off the bottom of a set tile releases it on that tick, before the jump. `move_down` held clears the latch, and a stop on that tick does not set it again. Releasing down does not re-latch until vx crosses that face again. Holding into the wall after the release does that on the following tick. A one-way tile is not a box. A falling move stops on its top when the feet started at or above that plane, and that stop is a floor: vy zeros, the latch clears, and the ground line takes the feet. Rising through it, or starting the tick already inside the tile, does not push the body out. `move_down` held skips those tops and still stops on set tiles. Releasing down does not pull the body back onto a plane it already passed. The x sweep, the latch, and the inside push never see a one-way. D-pad up does not move the body. With a clear snapshot the body still falls and rests on the floor, because the spawn column, x 96–144 of cell `(0, 0)`, is clear of the platforms.

The course is a flash strip of 8 view-cells, each 15 tiles wide and 16 tiles tall. There is no LCG. A floor cell has all 15 tiles of row 14 set, y 224–240. Cell 0 is that floor. Two walls are column 5 and column 9, rows 1 through 11, so the faces sit at x 96 and x 144 and the body can walk under them. The column between them stays empty. Platforms are one tile thick in the side pockets: left columns 0–3 at row 11 are one-way, and columns 0–2 at rows 8 and 5 stay set; right columns 11–14 at row 11, and 12–14 at rows 8 and 5, stay set. Each step is 48 px, inside the held jump. Cell 1 leaves the first three tiles of row 14 empty, the 48 px pit at world x 240–288, then the floor. One spike tile is column 22, row 13. One checkpoint tile is column 26, row 13. Touching it stores the stand under that tile, cell `(1, 0)`, local x 176 and y 212, feet on the floor. It does not store the live position. Cell 2 leaves the first three tiles of row 14 empty, the 48 px gap at world x 480–528. A 32×16 shuttle ping-pongs from x 464 to x 496 at 32 px/s, its top on the floor line. Cell 3 keeps that floor. Row 13, columns 45–56, is one wave: 45° up and down, the two-tile grade up and down, and the three-tile grade up and down. Each toe sits on the floor line. Column 58 is one more 45° rising right, and column 59 is empty, so the right edge is a cliff. From column 62, row 13, a 45° rises one tile up and to the right through column 74, row 1. From column 75, row 1, a 45° descends one tile down and to the right through column 87, row 13. One disk of radius 32 px has its center 16 px below the floor line, on column 96. The 16 px cap is a floor. The exit tile is column 104, row 13, on the floor after that cap. Cells 4 through 7 keep the floor. A read past the strip is empty. Feet below y 256 are death.

The shuttle is one kinematic box in the world tail. [shuttle_step] runs at the start of every tick, including dying and finished, and flips when it clamps at either end. [world_reset] snaps it to x 464, direction positive. A checkpoint respawn leaves that phase. The sweep tests the box beside the set tiles. A side latches. The top is a floor and the underside is a ceiling. `move_down` does not drop through it. The rider flag is set when the winning floor face is that top. On a tie with a set tile at the same height, the shuttle wins while the body still covers the slab. The next playing tick adds that step to the body before the solves. A jump clears the rider and keeps the body's vx. The body does not push the shuttle. Present draws it as one black rect.

The disk is one circle solid, solved after those faces. The body stays the 8×12 AABB. Overlap is the closest point of that box inside the radius. The push is along that point until it sits on the radius, with one integer square root. No float. A center below the feet is a floor: vy zeros, the latch clears, vx stays, and the jump and the ground line use it. `move_down` does not drop through it. A center beside the body is a wall: vx zeros and the latch matches that side. `move_down` still refuses the latch. A center above the head is a ceiling. The step stays under the radius, so the test is not swept. The box rests on one point and the other foot floats.

The center is 16 px below the floor, radius 32, so a 16 px cap sticks out on column 96. The floor tiles stay. Phase is 256 steps per turn, plus one every tick, including dying and finished. [world_reset] snaps it to 0. A checkpoint respawn leaves it. Clockwise, the top carries left. Rim speed is that step's arc, about 65 px/s. While the previous tick's floor was the disk, the next tick adds the horizontal tangent before the solves: full speed at the top, less toward the shoulders, none of the vertical component. A jump clears that rider. The body does not change the spin. Present fills the cap in black and draws one white spoke from the phase. The buried half is not drawn.

The spike, the checkpoint, and the exit do not block. Overlap uses the same strict test, after the vertical move and before the jump, against that kind in the apron. Spawn, the center of cell `(0, 0)`, is that checkpoint until the checkpoint tile is touched.

Feet below y 256, or a spike overlap, sets life to dying and the hold to 252 ms. That tick stops before the jump, the camera, and the pose, so the body stays on the frame it died in. While dying, the shuttle and the disk still step, then the tick only counts down. Input is ignored and the camera stays. At 0 the body is placed on the stored checkpoint the way spawn is placed: velocity, latch, pose, contacts, and the riders cleared, camera snapped, spring stopped, and the apron reloads around that tile. A jump still held on that tick does not fire. Death does not reload the apron and does not snap the shuttle or the disk. The exit zeros velocity and sets life to finished. Later ticks step the shuttle and the disk and return. `world_reset` clears the checkpoint back to spawn, snaps the shuttle, puts the disk on phase 0, reloads the apron there, and clears life.

The horizontal target puts the body center at the middle of the view. The vertical target puts the held ground line 32 px above the bottom. That line is the feet on a floor contact, sampled before the jump can clear the contact, and it also drops when the feet fall past it. A jump leaves the line where it was, so the floor stays put and the body rises into the view. Each tick adds three eighths of the remaining gap to the camera velocity, then keeps fifteen thirty-seconds of that velocity. That is the old half-gap and three-eighths damper brought across to 12 ms, so the pull per second stays. While running, the body sits about 9 px off the horizontal middle. `world_reset` snaps the origin onto that target and zeros the velocity. The host copies that origin into `Renderer::setDisplayOffset` once per present. Set tiles that cross the view are published as 16 px rects, and a one-way is the top 4 px of its tile, still black. A floor slope is the full 16 px tile plus a kind byte, filled as one black span per solid row, and the row mask still marks the whole tile. The shuttle is one black 32×16 rect. The disk cap is black horizontal spans above the floor, plus one white spoke. All of them are clipped before the draw. The spike is filled the same way, in magenta RGB565 `(31, 0, 31)`. The checkpoint and the exit are white outlines when they cross the view. The 16×16 sprite is drawn so its feet and center sit on the AABB, when that cell crosses the view. Index 0 is skipped, and so is a pixel that falls outside the view. The backdrop, the battery line, and the present rate use `setOffsetBypass`. The debug wireframe is `drawRectangle` on each 240 cell that crosses the view.

The pose is chosen at the end of the 12 ms tick, so the present rate does not drive it. A latch plays wall slide and faces away from that wall. A wall jump starts the dash sheet at the first cell; when that one-shot ends in the air, jump holds its last cell. Landing cancels dash, jump, and jump-turn. An open run-turn, jump-turn, or dash otherwise keeps advancing. A sign change of vx, with both the old and the new value non-zero, starts run-turn on the floor and jump-turn in the air. Leaving a stand is not a turn. Still airborne, jump plays once from the first cell and then holds the last. On the floor, speed at the run cap is run, speed from half that cap up to it is jog, a slower non-zero speed is walk, and zero is idle. Walk, jog, and run keep the frame across those lines. Each cell stays up for the time in the showcase sheets: walk, jump, and wall slide 120 ms, jog 80 ms, run, run-turn, and dash 60 ms, and jump-turn 60 ms then 120 ms. The 12 ms tick adds to that hold and keeps the leftover. A 60 ms cell and a 120 ms cell land on exact multiples. An 80 ms jog cell takes 7 ticks. Idle does not advance. The sheets face right, and the host mirrors x when the face is −1. Run-turn and jump-turn are stored facing right, so that mirror is the only one. The blit writes each nonzero index through `drawPixel`. `SpriteAnimation` stays unused: it has no duration and no one-shot. `drawMultiSprite` cannot flip. `getSpriteBuffer` stays null, because the device buffer is a palette index and the engine fast path would write a packed byte instead.

A floor slope is not a box. The body stays the 8×12 AABB. The three grades run both ways: 45° in one tile, 8 px of rise on each of two tiles, and 16 px of rise across three tiles. The gentle rise in subpixels is `(segment * tile + local_x) / 3`, and the end of the third tile lands on the top edge. The low end is the bottom edge of the tile. Support is the higher surface under the two bottom corners, so the box stays out of the solid. On a 45° ramp the downhill foot floats by up to 8 px. After the x stop on set tiles and the shuttle, a rise of at most this tick's `|dx|` plus one pixel lifts the feet. That one pixel covers the `/3` truncation. While the surface rises in the direction of travel, the run cap is the flat cap times `run / hyp`, the cosine of that step, so a steeper grade holds a slower top speed. Downhill adds the fall gravity times `sin * cos` each tick, in the direction of travel, up to the flat cap times `hyp / run`. A steeper grade accelerates harder and holds a higher top speed. The high edge, where the surface meets the top of the tile, is a wall only when this move crosses it from the outside and the feet stay below the slope surface: vx zeros and the latch matches a set-tile face. `move_down` still refuses the latch. A low edge is not a wall. The y sweep treats the surface as a floor. Falling onto it zeros vy, sets the floor, and clears the latch, so the jump and the ground line use it. `move_down` does not drop through it. While the previous tick was a floor and the surface is at most the 6 px fall cap below the feet, the body snaps down onto it. An empty gap is not a surface. A body that starts the y solve inside the solid is pushed up to the surface, and only up. The underside is the bottom edge of the tile, a horizontal ceiling, with no slide along it. Ceiling grades, half tiles, and corners stay later.

Mechanic reference, not code to port: `d:\Repos\refs\school-n--master`. The latch nudge is that demo's probe into the wall normal, and an inside push takes the shallowest overlap. Do not take its classes, `std::vector`, per-overlap `new`, variable `deltaTime`, skeleton, or its speed constants.

`PIXELROOT32_ENABLE_PHYSICS` stays 0. One collision truth, and it is game code. The ninja is kinematic. SoA rows live in the arena.

## Memory

Three stores:

| Store | Size | What stays there |
| --- | ---: | --- |
| Internal SRAM | 512 KB | The internal arena, then the leftover heap for BLE and the other IDF residents |
| PSRAM | 8 MB octal | The game arena, and nothing else from this binary |
| Flash | 16 MB quad | Code, read-only data, later PCM |

Who may allocate:

- The device has two arenas. `app_main` takes the internal block with `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL` before the ADC, NVS, and the NimBLE central, then the PSRAM block after the central. Neither block is freed. There is no `arena_free`. There is no rewind. Per-frame work overwrites the scratch slice in place.
- Game code allocates only through the PSRAM arena.
- The internal arena holds the DMA bands, the 8bpp buffer, the draw snapshot, the sim, present, backlight, and pad-reset stacks, their four `StaticTask_t` blocks, the `Engine`, and the drawer. The present task placement-news the `Engine` and the drawer into those slices and does not destroy the `Engine`. `operator delete` on the drawer does not return those bytes to the heap.
- ESP-IDF still allocates from the leftover internal heap: the NimBLE controller, host pools, and host task, NVS, the ADC unit, SPI DMA descriptors, the `esp_lcd` panel, the counting semaphore in the drawer, the dark `esp_timer`, the pad-link semaphore created on the first light sleep, and the FreeRTOS idle and timer stacks.

`arena` is a base, a capacity, and an offset. A slice bumps that offset. PSRAM slices align to 8 bytes. Internal slices align to 16 bytes, which the task stacks require. Bands are the first internal slice, on the DMA-capable pointer. If the offset would pass the capacity, boot fails.

The PC build uses one `malloc` of the PSRAM size, also never freed, and the same PSRAM carve. It does not take the internal block. Its `Engine` stays on the stack, and its draw snapshot stays in static storage.

| Resident | Where | Bytes | Owner | Notes |
| --- | --- | ---: | --- | --- |
| BLE controller and host | internal heap | unmeasured | ESP-IDF | NimBLE, internal pools. Fill from the first boot log |
| FreeRTOS idle and timer stacks, including the BT host task | internal heap | unmeasured | ESP-IDF | |
| Internal arena | internal, DMA | 208896 | `arena` | One block, taken before the central. Never freed |
| Present band | internal arena | 115200 | ST7789 drawer | Two 120-row bands of 240 RGB565, one frame. First slice. The idle band fills during the other's DMA |
| Logical buffer | internal arena | 57600 | ST7789 drawer | 240×240 8bpp. Not a second RGB565 frame |
| Draw snapshot | internal arena | 8948 | host | Two copies of the draw view, 256 AABB slots with a kind byte, plus the shuttle rect and the disk phase. Seqlock, constructed in the slice |
| Sim task stack | internal arena | 3072 | host | Core 0, priority 2. The 12 ms clock lives here |
| Present task stack | internal arena | 4096 | host | Core 1, priority 2. Runs the Engine that lives in the engine slice |
| Backlight task stack | internal arena | 3072 | ST7789 drawer | Polls PWR. The fade itself is LEDC hardware |
| Pad reset task stack | internal arena | 2048 | `pad_link` | Polls BOOT and PLUS. A double BOOT runs on the NimBLE event queue. A single BOOT is a mailbox the host reads |
| Task TCBs | internal arena | 1408 | host, drawer, `pad_link` | Four slices of 352. `sizeof(StaticTask_t)` is 340 on this IDF |
| `Engine` | internal arena | 4096 | host | Placement-new on the present task. The task does not destroy it |
| Drawer | internal arena | 256 | host | Placement-new on the present task. `operator delete` is a no-op |
| Internal spare | internal arena | 9088 | `arena` | Tail after the drawer. 12 bytes of alignment sit between the snapshot and the sim stack. Growing a slice takes from here and updates this table |
| Body sheet | flash | 9984 | `body_sheet` | 39 frames, 8bpp indices, 16×16. Index 0 is skip |
| Palette | flash | 512 | `palette` | 256 RGB565 entries. The ST7789 drawer reads this table |
| Game arena | PSRAM | 65536 | `arena` | Never freed |
| `world` slice | PSRAM arena | 4096 | `world` | SoA. The body keeps a cell index, local subpixel position, velocity, face contact, the wall latch, and the pose (face, clip, frame, and hold) on the first row. The camera origin, its velocity, the held ground line, life, the death hold, the checkpoint stand, the shuttle x, its direction, the rider, the disk phase, the disk rider, and its tangent step sit after the rows |
| `level` slice | PSRAM arena | 32768 | `world` | The 3×3 apron: the center tile column and row, then nine kind-bytes. The rest of the slice is unused |
| `scratch` slice | PSRAM arena | 16384 | reserved | Overwritten in place. The offset does not move |
| Spare | PSRAM arena | 12288 | `arena` | Growing a slice takes from here and updates this table |

Boot log, after the engine buffer exists: each slice of both arenas, including spare and any alignment gap, then internal heap total, free, and largest block, PSRAM heap total and free, and the flash app against its partition. Palette and body sheet bytes are in that same log.

## Runtime

`app_main` drives `BAT_EN` high, takes the internal arena, opens the VBAT ADC, starts NVS and the NimBLE central, creates the PSRAM arena, then `engine_host_run`. On the device that call builds the draw snapshot in its slice, publishes the spawn view, and returns. The sim task is pinned to core 0. The present task is pinned to core 1, placement-news the drawer and the Engine into their slices, and calls `Engine::run`. That device call paints one frame and does not call `update`. On the PC, `Engine::run` still loops update then present until the window closes, on one thread. The PC `main` creates the arena and calls the same host. Neither target inits I2C, Wi-Fi, the codec, or the IMU.

The sim publishes a draw snapshot in internal RAM after each advance. Present copies one stable view and draws only that. A torn slot is retried. The sim does not take a lock, and present does not call `world_*`, `pad_link_read`, or `battery_percent`. The link ground and the battery percent ride in the snapshot. The present rate stays on the present task.

The drawer fades the backlight on after the panel is up. PWR fades it off or back on. Present keeps drawing through the fade. When the fade ends at duty 0, the present task drains the SPI band already on the wire and does not start another. No draw and no SPI while the duty stays 0. The sim keeps the 12 ms step for 30 s, including held input. A PWR press in that window fades the backlight on, cancels the timer, and present resumes as the duty climbs. If the 30 s elapse with the duty still 0, the sim stops ticking. It waits until PWR is released, stops the NimBLE central so a connection event cannot wake the chip, holds `BAT_EN` high, and enters light sleep. Wake is GPIO5 low. The chip does not reboot. The same arena pointer is the world. The backlight fades on, the central scans again, and both loops resume. Light sleep isolates every GPIO, so the drawer holds CS, CLK, MOSI, DC, and RST for that stretch. On wake it releases those pads and sends the panel init again, including invert and display-on, without pulsing RST. Present then draws the snapshot. The PC build has no panel fade and no light sleep. Key 1 is that PWR edge: the window paints black immediately, the sim keeps stepping for 30 s, and the next press brings the view back.

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
| 2026-10-16 | Sim is pinned to core 0 and present to core 1. Device `Engine::run` does not call `update`. Present draws the latest snapshot. Duty 0 stops present; 30 s later the sim light-sleeps until PWR. The PSRAM arena stays |
| 2026-10-16 | Present uses two 120-row DMA bands, one RGB565 frame, reserved before the NimBLE central. The 8bpp buffer is reserved in that same call. A second copy is not reserved |
| 2026-10-16 | On the PC, key 1 is PWR, key 2 is PLUS, and key 3 is BOOT. Key 1 blacks the window and pauses the sim after 30 s |
| 2026-10-16 | Light sleep holds the ST7789 pads. Wake repeats the panel init so the glass is not left black |
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
| 2026-10-16 | PixelRoot32 1.12.0 (`826be881`) and PixelRoot32-APU v2.0.0 (`b603b3a9`) are vendored source snapshots. Submodules are gone. Upstream history is not imported. Docs, assets, examples, and tests are not copied. The trees may be patched |
| 2026-10-16 | One 204 KiB internal arena is taken before the ADC, NVS, and NimBLE. It holds the bands, the 8bpp buffer, the draw snapshot, four task stacks and their TCBs, the Engine, and the drawer. NimBLE stays on the leftover internal heap |
| 2026-10-16 | The boot log lists every arena slice and the internal, PSRAM, and flash parts after the engine buffer exists |
| 2026-10-16 | Present ships dirty rows through the 120-row bands. An empty mask skips SPI and waits 1 ms. A USB host on the cable prints draw, expand, spi, and rest once a second |
| 2026-10-16 | The frame log is per core. Core 1 splits mask, ground, solids, body, debug, expand, spi, engine, and the empty wait. Core 0 splits sim, its wait, and the rest. A percent is that core's own second |
| 2026-10-16 | Sim tick is 12 ms, the whole millisecond at the glass. Accelerations scale with the square of the step and velocities with the step, so the cage keeps its speed. Death holds 21 ticks |
| 2026-10-16 | Cage speeds are px/s and accelerations are px/s². Macros scale them onto the 12 ms tick. Death holds 252 ms |
| 2026-10-16 | `sendBuffer` returns with the last band on the wire. The next present draws the same indices during that DMA. Duty 0 drains the band before the present task parks |
| 2026-10-16 | The course is a flash strip of 16 px tiles. The sim keeps a 3×3 apron in the level slice and reloads it when the occupied tile changes. A floor cell has its bottom row of 15 tiles set |
| 2026-10-16 | The lowest left pocket is one-way. A fall from above lands on the top plane, down falls through, and present draws a 4 px black lip. Wedges stay later |
| 2026-10-16 | One kinematic shuttle crosses the 48 px gap at the start of cell 2. A rider is carried by its step. Engine physics stays off |
| 2026-10-16 | Floor slopes ride the 8×12 AABB. Three grades, both ways. A step within the move lifts the feet and keeps vx. The high edge is a wall. Present fills the solid in black |
| 2026-10-16 | Uphill, the run cap is the flat cap times the cosine of the step. Downhill does not add speed |
| 2026-10-16 | A 45° runs from column 62, row 13, to column 74, row 1. The exit sits on row 0 over that top |
| 2026-10-16 | Downhill adds fall gravity times sin*cos, up to the flat cap over cos. A 45° descends from column 75 to column 87, and the exit is column 89 |
| 2026-10-16 | One disk under the floor at column 96. The cap is a floor and carries the tangent. The exit moves to column 104 |
