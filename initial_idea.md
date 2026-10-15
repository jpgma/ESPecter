# ESPecter --- Architecture Notes

> **Decision record:** [architecture.md](architecture.md). This file stays the design proposal.

> **Status:** Initial proposal / living document\
> **Target:** Waveshare ESP32-S3 device, ESP-IDF, PixelRoot32 Game
> Engine\
> **Genre:** Small 2D side-scrolling precision platformer\
> **Working principle:** Build a playable, deterministic core first; add
> hardware interactions only after the baseline game is reliable.

## 1. Vision

ESPecter is a compact 2D platformer in which a ninja traverses
trap-ridden landscapes. Levels are obstacle courses made from narrow
platforms, gaps, moving machinery, hazards, secrets, and checkpoints.
The initial visual direction is readable, high-contrast pixel art with
compact sprites and a deliberately small palette.

The level sketch suggests a world of connected platforms and
environmental mechanisms: rotating wheels, elevated walkways, suspended
or moving platforms, hazards, and small visual landmarks. These should
be gameplay-readable first and decorative second.

### Design pillars

-   **Precision and momentum:** movement should feel responsive,
    predictable, and fair.
-   **Readable danger:** traps communicate their timing and collision
    area clearly.
-   **Small but systemic:** a few reusable mechanisms should combine
    into varied levels.
-   **Deterministic simulation:** the same inputs over the same
    simulation steps should produce the same result.
-   **Embedded-first:** bounded memory, minimal runtime allocation, and
    measurable frame time.
-   **Optional physical interaction:** the game must remain fully
    playable with the BLE gamepad alone.

## 2. Target hardware and assumptions

The initial target is a Waveshare ESP32-S3 device with a display,
capacitive touch, and a 6-axis IMU. Confirm the exact board revision and
peripherals before locking driver choices.

Known/expected inputs: - Q36XDV low-power BLE HID gamepad. - Capacitive
touch controller. - Accelerometer and gyroscope (IMU).

Hardware details to verify during bring-up: - Exact Waveshare model and
LCD controller/resolution. - Touch-controller interface and driver
availability. - IMU part number, I2C address, range, and sample rate. -
Available PSRAM and the actual memory configuration. - Battery/charging
behavior and acceptable sleep/wake strategy. - BLE HID report format and
reconnect behavior of the Q36XDV.

**Important:** do not assume the engine's existing display or touch
backend matches this exact board. Keep board-specific code behind
ESP-IDF / engine adapter boundaries.

## 3. Technology and responsibilities

### ESP-IDF

Owns the device lifecycle and hardware services: - startup,
configuration, logging, watchdogs, and error handling; - BLE host and
HID report reception; - display, touch, and IMU drivers where not
already provided by PixelRoot32; - FreeRTOS task and queue
configuration; - NVS for settings and small persistent records; - power
management and sleep behavior, if needed.

### PixelRoot32 Game Engine

Use PixelRoot32 for the engine facilities it already supports, after
checking the current API and ESP-IDF integration path: - scene lifecycle
and game loop; - rendering, sprites, tilemaps, camera, and
transitions; - engine input abstractions where they fit; -
collision/physics primitives where suitable; - optional audio and UI, if
enabled and within resource budgets.

PixelRoot32 documents a layered architecture from hardware/drivers
through abstractions and systems to scenes. Prefer extending or adapting
those layers instead of bypassing the engine throughout the game.

### ESPecter game layer

Owns game-specific behavior: - ninja controller and movement state; -
game rules, hazards, moving mechanisms, and triggers; - level
representation and level loading; - checkpoint, death, respawn, and
progression; - input mapping and optional motion/touch gestures; -
gameplay tuning constants and debug overlays.

### Architecture rule

Gameplay code should not call raw ESP-IDF BLE, I2C, GPIO, or display
APIs. Hardware services publish normalized data; game systems consume
it. Likewise, board-specific code should not contain ninja or level
rules.

## 4. Proposed repository layout

Adjust this to the actual ESP-IDF component layout and PixelRoot32
integration once the first build is working.

``` text
ESPecter/
├── CMakeLists.txt
├── sdkconfig.defaults
├── README.md
├── architecture.md
├── main/
│   ├── CMakeLists.txt
│   └── app_main.cpp
├── components/
│   ├── especter_game/
│   │   ├── CMakeLists.txt
│   │   ├── game/
│   │   │   ├── game_app.*
│   │   │   ├── game_state.*
│   │   │   └── game_config.h
│   │   ├── player/
│   │   │   ├── ninja_controller.*
│   │   │   └── ninja_state.h
│   │   ├── physics/
│   │   │   ├── movement_tuning.h
│   │   │   ├── collision_queries.*
│   │   │   └── hazard_collision.*
│   │   ├── world/
│   │   │   ├── level.*
│   │   │   ├── tile_definitions.h
│   │   │   └── mechanisms/
│   │   ├── scenes/
│   │   ├── input/
│   │   │   ├── game_input.*
│   │   │   └── input_mapping.h
│   │   └── debug/
│   ├── especter_platform/
│   │   ├── CMakeLists.txt
│   │   ├── board_display.*
│   │   ├── board_touch.*
│   │   ├── board_imu.*
│   │   └── platform_services.*
│   └── ble_gamepad/
│       ├── CMakeLists.txt
│       ├── ble_hid_host.*
│       └── hid_normalizer.*
├── assets/
│   ├── sprites/
│   ├── tiles/
│   ├── levels/
│   └── audio/
├── test/
│   ├── movement/
│   ├── collision/
│   └── level_rules/
└── docs/
    ├── controls.md
    └── level-format.md
```

This is a conceptual layout, not a requirement to create every directory
immediately. Keep the first implementation small; introduce a module
when it has a real responsibility.

## 5. Runtime architecture

### Main game loop

Prefer a fixed simulation step, initially targeting **60 updates per
second** if profiling shows it is sustainable. Rendering/presentation
may run at the same rate or skip frames when necessary. The exact target
should be decided from measurements on the actual display driver.

1.  Read the latest normalized input snapshot.
2.  Advance gameplay simulation in fixed-size steps.
3.  Update player movement and resolve world collisions.
4.  Update moving platforms, hazards, triggers, and level state.
5.  Render the current world and HUD through PixelRoot32.
6.  Present the frame through the engine/display backend.
7.  Record timing and memory statistics in debug builds.

If a frame is late, do not make physics behavior depend on an arbitrary
variable delta. Use a bounded accumulator and cap catch-up work to avoid
a spiral of death. Define what happens when the cap is exceeded (for
example, drop excess accumulated time in release builds and log it in
debug builds).

### Tasks and concurrency

Start with the simplest engine-compatible execution model. Do not create
a task per subsystem by default.

-   **Game/render loop:** owns mutable gameplay state and performs
    simulation/rendering.
-   **BLE callback/host task:** receives HID reports and writes compact
    input state to a queue or protected snapshot.
-   **IMU sampling:** initially sample at a modest rate and publish the
    latest filtered reading; avoid blocking the game loop on I2C.
-   **Touch input:** use the board/engine event path, normalized into
    game actions.

Only add task pinning or more elaborate synchronization after profiling.
No BLE callback or peripheral task should mutate player, physics, or
scene objects directly.

## 6. Coordinate systems and level data

Use a stable **world coordinate system** independent of display
resolution.

-   World positions and velocities describe the level and entities.
-   Screen coordinates are produced by the camera.
-   Tile coordinates identify static level cells.
-   Collision geometry is independent of sprite artwork.

Choose and document a tile size after confirming the logical render
resolution and art scale. A tilemap is a good fit for static terrain;
moving machinery and interactive objects should be separate entities.

### Initial level model

A level can contain: - static solid terrain; - one-way platforms; -
lethal hazards (spikes, blades, pits, etc.); - moving platforms and
rotating mechanisms; - switches, doors, and trigger volumes; -
checkpoints and exits; - optional decorative foreground/background
layers.

Use compact, data-driven definitions where practical. Avoid storing
duplicate collision data in sprite objects. For moving objects, keep
explicit state such as position, velocity, phase, direction, and
activation status.

The first level format can be compiled into firmware as `constexpr`
arrays or generated C++ data. A text/binary asset format can be added
later if loading from flash is useful.

## 7. Physics and movement model

Use the classic *N* tutorials as conceptual references for collision
detection, broad-phase filtering, and fast-moving objects. They are
design references, not a mandate to reproduce *N* exactly. PixelRoot32
already advertises AABB-based physics and kinematic actors; first
determine whether its existing behavior meets the game's needs before
implementing a second physics engine.

### Initial player model

Represent the ninja with a compact axis-aligned collision box separate
from the sprite. Start with kinematic platformer movement rather than
general rigid-body simulation.

Core state: - position and velocity; - grounded flag and ground
normal/contact; - facing direction; - jump state / jump buffer /
coyote-time counters; - alive, dying, and respawning state; - current
moving-platform attachment, if any.

Core movement features: - horizontal acceleration and maximum speed; -
ground and air control; - gravity and terminal fall speed; - jump
impulse; - optional variable jump height based on button hold; - coyote
time and jump buffering, tuned conservatively; - collision response for
floors, ceilings, and walls; - one-way platform handling; -
moving-platform carry and velocity inheritance; - death detection and
deterministic respawn.

Use a fixed timestep and explicit tuning constants. Keep movement tuning
separate from collision implementation so the feel can be adjusted
without rewriting the solver.

### Collision strategy

1.  Query nearby collision candidates using the engine's collision
    system or a small grid broad phase.
2.  Resolve player-vs-solid terrain with swept movement or substeps as
    needed.
3.  Detect hazard overlap separately from solid collision response.
4.  Handle triggers and checkpoints as non-solid volumes.
5.  Give fast or thin hazards special attention to avoid tunneling.

Start with AABBs for the player, tiles, and most hazards. Use circles or
segment tests only where they noticeably improve a mechanic. Test corner
cases: narrow corridors, high-speed falls, wall contact, platform edges,
one-way platforms, and moving platforms.

### Moving machinery

Model the central wheel idea as a mechanism with a deterministic phase.
For an initial version, its visuals may rotate while collision is
represented by a simpler predictable hazard volume. Add physically
rotating collision geometry only if the gameplay requires it; it is more
expensive to tune and validate.

## 8. Input architecture

All sources feed one normalized action state. Game logic must not depend
on which physical device produced an action.

Suggested actions: - `MOVE_LEFT` - `MOVE_RIGHT` - `JUMP` - `PAUSE` -
`CONFIRM` - `CANCEL` - optional `RESTART`

The input layer should expose held, pressed-this-step, and
released-this-step states. This avoids missed jumps and prevents
device-specific event timing from leaking into the controller.

### BLE HID gamepad

-   Treat the Q36XDV as an external, potentially disconnectable input
    source.
-   Confirm its advertised services, HID report descriptor, button
    mapping, and connection interval using a small hardware test before
    designing around it.
-   Convert HID reports into normalized actions.
-   Handle disconnects safely: clear held buttons, stop repeating stale
    movement, and allow reconnect.
-   Avoid pairing/reconnection work in the game loop.
-   Keep a simple on-device control fallback if practical, so BLE
    failure does not make development impossible.

BLE HID support depends on the ESP-IDF version and BLE host stack
selected. Verify whether the needed HID-host role and profile are
supported by the chosen stack before committing to a particular
implementation.

### Touch

Touch is optional and should not be required for precision movement.
Possible uses: - menu navigation and pause; - tapping to restart after
death; - optional on-screen buttons if the gamepad is unavailable; -
level selection or simple debug controls.

Keep touch coordinates and calibration in the platform adapter;
translate gestures into the same action interface.

### Accelerometer and gyroscope

Motion input is experimental and disabled by default until calibrated.
Good first experiments: - tilt to nudge a menu cursor; - tilt to adjust
a small aiming/lean mechanic; - a deliberate shake gesture for a
non-critical action.

Do not map raw IMU readings directly to player velocity. Filter
readings, calibrate a neutral pose, apply dead zones and limits, and
ensure motion controls cannot accidentally cause death. The core
platformer must remain playable without the IMU.

## 9. Rendering and art

Use PixelRoot32 sprites, tilemaps, camera, and rendering facilities
wherever possible. Keep the art readable at the physical display size.

Recommended visual priorities: - strong contrast between safe ground and
lethal surfaces; - clear silhouettes for the ninja and hazards; -
restrained palette and limited animation frames; - separate background,
gameplay, foreground, and HUD layers; - camera movement that anticipates
travel but does not obscure hazards.

Start with placeholder rectangles and simple sprites. Confirm collision
and camera behavior before producing a large art set. Use viewport
culling and static tilemap caching if supported and beneficial on the
target configuration.

## 10. Memory and performance rules

-   No unbounded allocations in the per-frame update or draw path.
-   Prefer fixed-size buffers, preallocated entity pools, and bounded
    level sizes.
-   Avoid copying large sprite/tile data into RAM when it can remain in
    flash and the engine supports flash-backed assets.
-   Track free heap, minimum-ever free heap, largest free block, stack
    high-water marks, update time, render time, and presentation time.
-   Treat BLE, display DMA, and PSRAM use as shared resource
    constraints; measure rather than assume.
-   Enable engine subsystems only when used. Begin with the smallest
    practical PixelRoot32 configuration.
-   Keep debug logging out of high-frequency loops.

Initial performance target: stable, responsive gameplay at the chosen
logical resolution with no missed input, visible frame pacing problems,
or memory leaks during a long play session. Establish numerical budgets
after a baseline build is profiled.

## 11. Persistence and game states

Initial game states: - boot / initialization; - title screen; - level
start; - playing; - dying; - respawning; - paused; - level complete.

Use NVS only for small persistent settings at first: - input
configuration; - calibration values; - unlocked levels or best results,
if desired.

Do not write flash every frame. Save on meaningful state transitions and
handle failed writes without corrupting the active game session.

## 12. Testing and observability

Develop the rules so important logic can be tested without the physical
display.

Unit-test or simulate: - movement and jump timing; - collision
resolution; - one-way platforms; - moving-platform carry; - hazard
detection; - checkpoint and respawn transitions; - input edge detection
and disconnect handling; - level boundary and exit triggers.

Use PixelRoot32's native/PC simulation path if compatible with the
chosen project setup. Hardware tests are still required for display
timing, BLE behavior, IMU calibration, power, and long-duration
stability.

Add a debug overlay, toggled in development builds, showing FPS/frame
time, player position/velocity, grounded state, collision box, current
input actions, and heap statistics.

## 13. Milestones

### M0 --- Toolchain and board bring-up

-   [ ] Confirm exact Waveshare board and ESP-IDF version.
-   [ ] Build a minimal ESP-IDF application.
-   [ ] Confirm display output and basic input/peripheral access.
-   [ ] Integrate PixelRoot32 with a documented, reproducible build.
-   [ ] Record memory use and a baseline frame rate.

### M1 --- Playable graybox

-   [ ] Draw a static test room and simple ninja placeholder.
-   [ ] Implement normalized input actions.
-   [ ] Implement run, jump, gravity, and solid collision.
-   [ ] Add camera follow and level boundaries.
-   [ ] Reach a stable frame rate on hardware.

### M2 --- N-style precision platforming

-   [ ] Add jump buffering and coyote time if desired.
-   [ ] Add one-way platforms and lethal hazards.
-   [ ] Add death, respawn, and checkpoints.
-   [ ] Test fast movement and collision edge cases.
-   [ ] Create one short, complete level.

### M3 --- Mechanisms and level variety

-   [ ] Add moving platforms.
-   [ ] Add a rotating-wheel or timed-hazard prototype.
-   [ ] Add switches, triggers, and doors.
-   [ ] Build a small set of reusable level pieces.

### M4 --- Hardware input

-   [ ] Connect to the Q36XDV as a BLE HID host.
-   [ ] Validate mapping, latency, reconnect, and stale-button clearing.
-   [ ] Add optional touch actions.
-   [ ] Prototype filtered IMU input behind a setting.

### M5 --- Content and polish

-   [ ] Replace placeholders with consistent pixel art.
-   [ ] Add menus, pause, and settings.
-   [ ] Add sound only after performance and power budgets are
    understood.
-   [ ] Run a long-duration stability test and tune power behavior.

## 14. Open decisions and risks

-   **Engine integration:** verify the currently supported PixelRoot32
    integration path with ESP-IDF. The engine's public examples
    prominently use PlatformIO; do not assume an ESP-IDF component
    wrapper exists or is maintained.
-   **Display backend:** confirm compatibility with the exact Waveshare
    LCD and its driver/controller.
-   **Physics ownership:** prefer PixelRoot32's kinematic/AABB
    facilities unless a concrete gap justifies custom code. Avoid two
    competing sources of collision truth.
-   **BLE HID host:** validate the Q36XDV's actual HID behavior and
    ESP-IDF stack support early.
-   **Memory budget:** measure with display buffers, engine caches, BLE,
    and assets enabled together.
-   **IMU usefulness:** keep it optional; avoid making a precision
    platformer dependent on noisy motion input.
-   **Audio:** optional for the first playable build; audio drivers and
    buffers can consume meaningful memory and integration time.

## 15. Reference material

-   PixelRoot32 Game Engine repository:
    https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Game-Engine
-   PixelRoot32 architecture: https://docs.pixelroot32.org/architecture/
-   PixelRoot32 getting started: https://docs.pixelroot32.org/
-   Metanet Software development tutorials:
    https://www.metanetsoftware.com/dev
-   N Tutorial A --- Collision Detection and Response:
    https://www.metanetsoftware.com/2016/n-tutorial-a-collision-detection-and-response

Use the Metanet tutorials to study collision detection, broad-phase
filtering, fast-moving objects, and related geometry. Adapt the
principles to the engine's existing APIs and the constraints of the
ESP32-S3 rather than copying a desktop-oriented implementation
wholesale.

------------------------------------------------------------------------

## First implementation rule

**Do not start by building every subsystem.** First get the exact board
building under ESP-IDF with PixelRoot32 rendering a scene. Then make a
rectangle run and jump on a single floor. Everything else should be
added only after that vertical slice is measured and repeatable.
