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

The PC build defines `PLATFORM_NATIVE` and uses the engine's SDL2 drawer. It does not compile the ST7789 drawer or the Arduino shim. The window title is ESPecter. Both targets draw the present rate at the top-right, in the engine default `FONT_5X7` at size 1. That glyph is 5×7 with 1 px of spacing, and size 1 is the smallest `drawText` still draws. The device also draws the pack percent at the top-left, same font and size, as `80%`. The PC build has no battery line.

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
- PLUS is GPIO4 and stays unread. BOOT is GPIO0, active low, configured as an input with pull-up after the app is up. A press terminates the Pad Emu link and scans again. The sample ignores the pin until it has been seen released, then accepts one press after two pressed samples 20 ms apart. Holding it does not repeat. PWR (GPIO5) is the power latch, not a game button. A press fades the backlight off or back on.
- Wi-Fi is not started. On IDF 5.5 `ESP_WIFI_ENABLED` has no prompt, so the wifi library stays linked because the BT component requires it. This binary does not call `esp_wifi_init`, so that DMA is not allocated.
- The speaker and ES8311 exist. PixelRoot32's I2S backend is a bare DAC, not this codec. Audio is not in this binary. See the sound section.
- CPU is 240 MHz (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`). ESP-IDF is 5.5 or newer. Target `esp32s3`.

## Display

Logical resolution equals the panel: 240×240, no upscale. That fits more of a level on the square glass. The wire cost of a full present stays about 12.3 ms, because the panel always receives 240×240 RGB565.

The logical buffer is 240×240 8bpp, 57600 bytes, in internal RAM. The engine draws through `DrawSurface` and does not allocate this buffer itself. The ST7789 drawer does, because `DisplayType::CUSTOM` has no TFT sprite. Hot pixels stay out of PSRAM. The static tilemap framebuffer cache stays off. It would duplicate those 57600 bytes.

Each present the engine calls `clearBuffer` before the scene draws. That call resets the palette and does not wipe the 57600 indices. The scene's first draw is a full-view rectangle, so a second write of those bytes would sit on the CPU while the SPI bus is idle. A pixel left untouched keeps its previous index, and after the palette reset that index can name another color. `init` still wipes the indices once, before the panel is shown.

## Look

Reference is Nidhogg (2014), not Nidhogg 2. Flat colors, chunky silhouettes, a different small palette per level. Do not copy that game's sprites, palettes, or tracks.

- Per screen: start at 4 colors, hard cap 8. No gradients, no dithering, no separate outline pass.
- Sprites are 1bpp or 2bpp. RGB565 is only the wire format inside the drawer.
- Danger is one accent color, and those pixels are the hazard volume.
- The body is an AABB. Nidhogg's pixel-precise sword hits are not the collision model.
- The sketch's wheel, spikes, and walkways become bold shapes.
- The view is square. When a camera exists, it leads in the facing direction.
- The ninja is about 16 pixels tall (about 1.8 mm on this 1.54" panel). The boilerplate rectangle is 16×16. Later animation is idle, run, jump, death. No skeletal animation.

The figure is white. On the PC the ground is dark green. On the device the ground is the link, the same three flat colors as the pademu status bar: red `(180, 0, 0)` while the central is down, amber `(220, 180, 0)` while it scans, and green once Pad Emu is linked. That green is `(0, 140, 32)`, darker than the pademu bar `(0, 180, 40)`.

## Sound

Daedelus's Nidhogg score is sample-based. PixelRoot32's pulse, triangle, and noise voices stay compiled out (`PIXELROOT32_ENABLE_AUDIO` is 0 for good).

Later music and effects are short PCM clips in flash, played through the ES8311. Gate PA GPIO7 only while sound is coming out. One owner for I2S. Stream from flash. A 16 kHz mono 16-bit clip is 32 KB per second. No MP3 decoder, and no decoded song kept in RAM. Silence is a valid mix. None of this is built now.

## Gamepad

The Q36XDV is not on the board. The chip is BLE only, so Classic HID is not an option. The Q36 report map is still unread. This binary does not connect to the first advertisement it sees.

The phone stand-in is what the device reads. The app name is Pad Emu. It advertises service `f31a0001-7c4e-4a2b-9e15-6d8f0b2c4a10` and notifies a 3-byte report on `f31a0002-7c4e-4a2b-9e15-6d8f0b2c4a10`: a little-endian `uint16` of buttons, then a hat (`0` up, then clockwise, `8` neutral). [main/pad_link.c](main/pad_link.c) is the NimBLE central. It scans for that service, subscribes to the report, and copies each notify into a static mailbox. A notify also marks the link up, and the 2 s watch leaves that phase alone. [main/actions_pad.c](main/actions_pad.c) calls `pad_link_read` from `actions_capture` and fills the same snapshot the PC keyboard fills. `game/` does not include that reader. While the link is down the mailbox is clear and the body keeps simulating. A drop clears the mailbox and the central scans again. A press of BOOT does the same when a vanished peer never delivers a disconnect: the one connection slot is dropped, the mailbox is cleared, and the central scans again. The reset runs on the NimBLE event queue. A connect that completes after the press is terminated too. Advertisements are not duplicate-filtered, so the same phone is seen again. The connection uses a 2 s supervision timeout and a 10 s connect attempt, with no bonding. A host timer every 2 s restarts a scan that died, and abandons a connect or discover that has sat for 8 s. A linked report fills the held fields below. This stand-in has no stick.

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
- No key and no controller on PC, or no Pad Emu link on the device: the snapshot stays clear. The body still falls and rests on the bottom edge.

## Physics

N++ is the movement. The body is a 16×16 kinematic AABB. [game/world.c](game/world.c) integrates one fixed tick: accelerate or friction on x, gravity on y, cap a latched fall, move x and respond, move y and respond, then a jump impulse. `world_tick` takes no delta. One pixel is 256 subpixels, stored as `int` on the first world row, with velocity, the four border contacts, and the wall latch (`-1` left, `1` right, `0` none).

The host turns wall-clock milliseconds into ticks. `Engine::update` passes a whole-millisecond delta. The host adds it to an accumulator and calls `world_tick` once per 16 ms, at most four times per update. A zero delta adds nothing. Time left under 16 ms stays. If four ticks leave 16 ms or more, that remainder is dropped. Sixteen milliseconds is the step this clock can mark, 62.5 Hz, not 1000/60. Present rate does not scale velocity. The number in the corner is presents, not sim ticks.

Feel, per tick: horizontal accel and friction 64 subpixels, max run 768 (3 px, about 188 px/s). Jump impulse −2048. Gravity 128 while rising with jump held, 256 otherwise. Fall caps at 2048, under the body, so a later 16 px tile is not skipped in one tick. A held jump reaches about 64 px. Releasing jump early uses the heavier gravity. While the wall latch holds and the body is falling, vy caps at 256 (1 px), starting the tick after the grab. A wall jump sets vx away from that edge and vy to −1448, the integer of 2048 / sqrt(2). Held, that rise is about 32 px; releasing early is about half. While |vx| is above 768, x input is ignored and vx loses 64 per tick down to the run cap. Only the wall jump produces that speed. This is the cage tune.

The four borders of the view stop the body and zero that axis. No bounce. Crossing the left or right edge sets the wall latch and zeros vx. While the latch holds, left and right do not change vx. The ceiling zeros vy and leaves the latch. The floor clears the latch, so a corner jump is the floor jump. `move_down` held clears the latch, and a clamp on that tick does not set it again. Releasing down does not re-latch until vx crosses that edge. Holding into the wall after the release does that on the following tick. The floor is the bottom edge, not a drawn tile. D-pad up does not move the body. With a clear snapshot the body still falls and rests on the bottom edge.

Tiles are still later: [N Tutorial A](https://www.metanetsoftware.com/2016/n-tutorial-a-collision-detection-and-response), move then respond, horizontal and vertical apart, swept tests so a fast fall does not tunnel. Clamping into the view is exact for these four edges.

Mechanic reference, not code to port: `d:\Repos\refs\school-n--master`. Square tiles, wedges, and hazards as overlap are still later. Do not take its classes, `std::vector`, per-overlap `new`, variable `deltaTime`, skeleton, or its speed constants.

`PIXELROOT32_ENABLE_PHYSICS` stays 0. One collision truth, and it is game code. The ninja is kinematic. SoA rows live in the arena. The next physics slice is solid square tiles plus this body AABB.

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
| Pad reset task stack | internal | 2048 | `pad_link` | Polls BOOT. The reset runs on the NimBLE event queue |
| Logical buffer | internal | 57600 | ST7789 drawer | 240×240 8bpp. Not a second RGB565 frame |
| Game arena | PSRAM | 65536 | `arena` | Never freed |
| `world` slice | arena | 4096 | `world` | SoA. The body keeps subpixel position, velocity, border contact, and the wall latch on the first row |
| `level` slice | arena | 32768 | reserved | Empty in this binary |
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
