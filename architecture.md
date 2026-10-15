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

The PC build defines `PLATFORM_NATIVE` and uses the engine's SDL2 drawer. It does not compile the ST7789 drawer or the Arduino shim. The window title is ESPecter. The host draws the measured frame rate in the corner of that window. The device scene does not.

## Hardware consequences

- Panel: write-only ST7789, 240×240 visible, RGB565, SPI mode 3 at 80 MHz. GPIO21 CS, GPIO38 CLK, GPIO39 MOSI, GPIO45 DC, GPIO40 RST, GPIO46 backlight. GPIO45 and GPIO46 are strapping pins. They are configured only after boot. No MISO. No TE. Tearing is a hardware fact. Dirty rectangles are the later mitigation.
- One full RGB565 frame is 115200 bytes and about 12.3 ms on the wire. The drawer presents 1:1 in one 16-row DMA band (7680 bytes). A second full frame is not reserved.
- Internal SRAM is 512 KB. DMA and the BLE controller live here. PSRAM cannot hold them.
- PSRAM is 8 MB octal. The game arena is one 64 KB block here. The rest stays free so a stray heap allocation is visible.
- Flash is 16 MB quad, not OPI. `sdkconfig.defaults` sets that.
- `BAT_EN` (GPIO2) is driven high before any blocking call. USB Serial/JTAG dies in deep sleep. There is no I2C fuel gauge. Deep sleep is not used.
- One I2C bus (GPIO41/42): QMI8658A `0x6B`, CST816D `0x15`, ES8311 `0x18`, ES7210 `0x40`. No second bus. This binary does not init I2C, so those devices stay down until one owner exists. Touch is one finger. The IMU has no magnetometer, so yaw is not observable.
- PLUS is GPIO4 and BOOT is GPIO0, reserved as a later on-device fallback. PWR (GPIO5) is the power latch, not a game button.
- Wi-Fi stays off. Its DMA cannot live in PSRAM, and this game does not need it.
- The speaker and ES8311 exist. PixelRoot32's I2S backend is a bare DAC, not this codec. Audio is not in this binary. See the sound section.
- CPU is 240 MHz (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`). ESP-IDF is 5.5 or newer. Target `esp32s3`.

## Display

Logical resolution equals the panel: 240×240, no upscale. That fits more of a level on the square glass. The wire cost of a full present stays about 12.3 ms, because the panel always receives 240×240 RGB565.

The logical buffer is 240×240 8bpp, 57600 bytes, in internal RAM. The engine draws through `DrawSurface` and does not allocate this buffer itself. The ST7789 drawer does, because `DisplayType::CUSTOM` has no TFT sprite. Hot pixels stay out of PSRAM. The static tilemap framebuffer cache stays off. It would duplicate those 57600 bytes.

## Look

Reference is Nidhogg (2014), not Nidhogg 2. Flat colors, chunky silhouettes, a different small palette per level. Do not copy that game's sprites, palettes, or tracks.

- Per screen: start at 4 colors, hard cap 8. No gradients, no dithering, no separate outline pass.
- Sprites are 1bpp or 2bpp. RGB565 is only the wire format inside the drawer.
- Danger is one accent color, and those pixels are the hazard volume.
- The body is an AABB. Nidhogg's pixel-precise sword hits are not the collision model.
- The sketch's wheel, spikes, and walkways become bold shapes.
- The view is square. When a camera exists, it leads in the facing direction.
- The ninja is about 16 pixels tall (about 1.8 mm on this 1.54" panel). The boilerplate rectangle is 16×16. Later animation is idle, run, jump, death. No skeletal animation.

The first scene uses two flat colors: dark green ground and a white figure.

## Sound

Daedelus's Nidhogg score is sample-based. PixelRoot32's pulse, triangle, and noise voices stay compiled out (`PIXELROOT32_ENABLE_AUDIO` is 0 for good).

Later music and effects are short PCM clips in flash, played through the ES8311. Gate PA GPIO7 only while sound is coming out. One owner for I2S. Stream from flash. A 16 kHz mono 16-bit clip is 32 KB per second. No MP3 decoder, and no decoded song kept in RAM. Silence is a valid mix. None of this is built now.

## Gamepad

The Q36XDV is not on the board. The chip is BLE only, so Classic HID is not an option. `sdkconfig.defaults` enables Bluedroid, BLE-only. `app_main` raises `BAT_EN`, starts the controller and the host, then the arena, then the scene. It does not connect to the first HID it sees. The pad's switch X name is `Xbox Wireless Controller`. The report map is still unread, so the device writes a clear action snapshot and the rectangle stays still.

The PC build does not open a Bluetooth radio and does not link Bluedroid. DS4Windows owns the radio and, in its default profile, exposes an Xbox 360 pad. [pc/actions_ds4.c](pc/actions_ds4.c) is compiled only into the PC target. It reads the keyboard and the first SDL game controller, and ORs them. A virtual DualShock profile is not the one this sim supports. The device links [game/actions.c](game/actions.c), which clears the snapshot and does not read a keyboard.

[game/actions.h](game/actions.h) is the shared snapshot. The host calls `actions_capture` once per update. Held play buttons are `dpad_up`, `dpad_down`, `dpad_left`, `dpad_right`, `face_y`, `face_x`, `face_a`, `face_b`, `shoulder_l`, `trigger_l2`, `trigger_r2`, `shoulder_r`, `select`, `home`, and `start`. The reader then derives `move_up`, `move_down`, `move_left`, `move_right`, `jump`, `pause`, `confirm`, and `cancel`. The left stick past a dead zone also sets `move_left` / `move_right`. The Q36 has no stick; that axis is only the Xbox stand-in.

Keyboard placement follows the Q36 face. The Type-C port, LED, reset hole, X/D/V slide, and Pair are not play buttons, so they have no key.

| Q36 | Key | Snapshot |
| --- | --- | --- |
| D-pad up, left, down, right | W A S D | `dpad_up` `dpad_left` `dpad_down` `dpad_right` |
| Y, X, A, B | I J K L | `face_y` `face_x` `face_a` `face_b` |
| L, L2 | Q E | `shoulder_l` `trigger_l2` |
| R2, R | U O | `trigger_r2` `shoulder_r` |
| View (−), Home, Menu (+) | F G H | `select` `home` `start` |

Derived, for either source:

- D-pad: `move_up` / `move_down` / `move_left` / `move_right`. The rectangle steps on that axis. Opposite directions cancel.
- Left stick past a dead zone: `move_left` / `move_right` as well.
- South button (`face_a`): `jump` and `confirm`. The rectangle steps up while held. D-pad down cancels that step.
- Menu (`start`): `pause`. B (`face_b`): `cancel`.
- No key and no controller: the snapshot stays clear and the rectangle stays centered.

## Physics

N++ is the movement. This binary does not run or jump in the physics sense. The rectangle step on PC is an input test, not the solver.

Solver to follow: [N Tutorial A](https://www.metanetsoftware.com/2016/n-tutorial-a-collision-detection-and-response). Tile world, move then respond, horizontal and vertical apart, swept tests so a fast fall does not tunnel.

Mechanic reference, not code to port: `d:\Repos\refs\school-n--master`. Variable jump, heavier fall, wall slide and wall jump, square tiles first, wedges later, hazards as overlap. Do not take its classes, `std::vector`, per-overlap `new`, variable `deltaTime`, skeleton, or its speed constants.

`PIXELROOT32_ENABLE_PHYSICS` stays 0. One collision truth, and it is game code. The ninja is kinematic. SoA rows live in the arena. The first physics slice, when it exists, is solid square tiles plus the body AABB.

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
| BLE controller and host | internal | unmeasured | ESP-IDF | Fill from the first boot log |
| FreeRTOS stacks, including the BT host task | internal | unmeasured | ESP-IDF | |
| Present band | internal, DMA | 7680 | ST7789 drawer | 16 rows of 240 RGB565. One band, reused |
| Logical buffer | internal | 57600 | ST7789 drawer | 240×240 8bpp. Not a second RGB565 frame |
| Game arena | PSRAM | 65536 | `arena` | Never freed |
| `world` slice | arena | 4096 | `world` | SoA. The rectangle uses the first row |
| `level` slice | arena | 32768 | reserved | Empty in this binary |
| `scratch` slice | arena | 16384 | reserved | Overwritten in place. The offset does not move |
| Spare | arena | 12288 | `arena` | Growing a slice takes from here and updates this table |

Boot log, after the engine buffer exists: free internal heap, largest free internal block, free PSRAM, arena capacity, arena offset.

## Runtime

`app_main` drives `BAT_EN` high, starts BLE, creates the arena, then `engine_host_run`. The drawer turns the backlight on after the panel is up. The PC `main` creates the arena and calls the same host. Neither target inits I2C, Wi-Fi, the codec, or the IMU.

## Decision log

| Date | Decision |
| --- | --- |
| 2026-10-15 | ESP-IDF 5.5 or newer, target `esp32s3`, 16 MB quad flash, octal PSRAM, 240 MHz |
| 2026-10-15 | PixelRoot32 1.12.0 as a submodule, plus PixelRoot32-APU v2.0.0 because the headers require it. Custom `esp_lcd` drawer and an Arduino shim. The engine tree is not patched |
| 2026-10-15 | Logical and physical resolution 240×240, no scale |
| 2026-10-15 | Physics is a later kinematic step from Metanet tutorial A. Engine physics stays compiled out. The school demo is a mechanic reference only |
| 2026-10-15 | Bluedroid BLE-only is on from the first device boot. Wi-Fi is off. HID report map is still open. PC reads a DS4Windows Xbox pad and does not open a radio |
| 2026-10-15 | PC keyboard ORs into the same snapshot as the Xbox pad. Keys follow the Q36 face. Pair, the mode slide, the LED, the reset hole, and Type-C are not play buttons. The device snapshot stays clear |
| 2026-10-15 | Fixed timestep length and feel constants stay open until something actually jumps |
| 2026-10-15 | One 64 KB PSRAM arena, carved once, never freed. BLE's byte cost stays unmeasured until the boot log |
| 2026-10-15 | Nidhogg 1 art: 4-color screens, 1bpp or 2bpp, no copied assets. NES audio voices stay off. Later sound is PCM on the ES8311 |
| 2026-10-15 | Touch and IMU stay off. Frame-time budgets stay open |
| 2026-10-15 | Build scripts live at the repo root. The PC window is titled ESPecter and draws its current frame rate |
