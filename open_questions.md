# ESPecter --- Open Questions & Agent Research Brief

> **Status:** Research backlog / living document\
> **Project:** ESPecter, a small 2D side-scrolling platformer for a
> Waveshare ESP32-S3 device\
> **Primary stack under consideration:** ESP-IDF + PixelRoot32 Game
> Engine\
> **Physics reference:** Metanet Software's classic *N* development
> tutorials\
> **Purpose:** Delegate bounded research tasks to agents, collect
> evidence, and turn findings into implementation decisions.

------------------------------------------------------------------------

## 1. Research goals

Resolve the highest-risk unknowns before committing to a project
structure or implementing game systems.

The research should establish:

1.  Whether PixelRoot32 can be integrated cleanly with the selected
    ESP-IDF version and exact Waveshare board.
2.  Which existing engine features should be reused versus adapted or
    implemented.
3.  A practical physics and collision design for *N*-inspired precision
    platforming.
4.  How to receive input from the Q36XDV BLE HID gamepad reliably.
5.  What the display, touch controller, and IMU require on the exact
    board revision.
6.  Realistic memory, frame-time, power, and stability budgets.
7.  A minimal vertical slice that can be built and tested before larger
    systems are developed.

**Research principle:** prefer primary sources, current repository code,
reproducible experiments, and explicit evidence over assumptions or
generic ESP32 advice.

## 2. Instructions for research agents

Each agent should work on a bounded topic and return a concise decision
brief, not a broad essay.

For every important claim: - Cite the source URL and the relevant file,
symbol, section, or line range when possible. - Distinguish **verified
facts**, **reasonable inferences**, and **unknowns**. - Record the
date/version/commit checked for version-sensitive information. - Prefer
official documentation, upstream source code, issue trackers,
datasheets, and minimal reproducible tests. - Do not claim compatibility
just because two projects both use ESP32 or ESP-IDF. - Do not invent
APIs, configuration options, measured performance, or hardware
specifications. - If evidence is inconclusive, state exactly what test
would settle the question. - Recommend the smallest experiment that can
de-risk the decision. - Avoid making changes to the main architecture or
source code unless explicitly assigned.

### Required output from each agent

``` markdown
## Topic
### Executive conclusion
One short paragraph with the current recommendation.

### Findings
- Finding — evidence/source — confidence.

### Options considered
| Option | Advantages | Risks/costs | Evidence |
|---|---|---|---|

### Recommendation
A specific decision, or a clear statement that no decision is justified yet.

### Minimal validation experiment
Steps, expected result, and what failure would imply.

### Open questions
Only questions still unresolved.

### Sources
Links, versions/commits, and access dates.
```

## 3. Priority overview

Use these priorities to sequence work. P0 items block foundational
implementation; P1 items influence core design; P2 items can wait until
a playable prototype exists.

  -------------------------------------------------------------------------------
  ID                Priority          Research area             Decision to
                                                                unlock
  ----------------- ----------------- ------------------------- -----------------
  R-01              P0                PixelRoot32 + ESP-IDF     Build system and
                                      integration               engine
                                                                integration
                                                                strategy

  R-02              P0                Exact Waveshare           Display driver
                                      board/display             and logical
                                                                resolution

  R-03              P0                Engine                    Reuse vs. custom
                                      rendering/input/physics   implementation
                                      APIs                      

  R-04              P0                BLE HID host + Q36XDV     Gamepad
                                                                connectivity
                                                                strategy

  R-05              P1                *N*-style movement and    Player physics
                                      collision                 design

  R-06              P1                Memory and performance    Runtime
                                                                architecture and
                                                                asset budgets

  R-07              P1                Level and entity          Static terrain
                                      representation            and mechanism
                                                                model

  R-08              P1                Touch and IMU             Optional
                                                                interaction
                                                                strategy

  R-09              P1                Testing and simulation    Host tests and
                                      workflow                  hardware test
                                                                plan

  R-10              P2                Art/asset pipeline        Sprite, tile, and
                                                                level authoring
                                                                workflow

  R-11              P2                Power, battery, and sleep Product behavior
                                                                after core
                                                                gameplay works

  R-12              P2                Audio and feedback        Whether and how
                                                                to add
                                                                audio/haptics
  -------------------------------------------------------------------------------

------------------------------------------------------------------------

## 4. Research tracks

## R-01 --- PixelRoot32 integration with ESP-IDF

**Priority:** P0\
**Suggested owner:** Build-system / engine-integration agent

### Questions

-   What build systems and target configurations does the current
    PixelRoot32 repository officially support?
-   Does the current repository contain an ESP-IDF component, CMake
    integration, or documented ESP-IDF workflow?
-   Is the documented PlatformIO workflow dependent on Arduino, ESP-IDF,
    or a specific framework configuration?
-   Which ESP-IDF versions, ESP-IDF targets, and toolchain versions are
    supported or known to work?
-   What third-party libraries, compiler flags, C++ standard, and
    configuration macros are required?
-   Does the engine assume a particular display backend, input API,
    filesystem, timer, or audio subsystem?
-   Can the engine be integrated without maintaining a large fork?
-   What is the smallest reproducible build that initializes the engine
    and renders one frame on the target board?
-   Which upstream examples are closest to the intended configuration?
-   What integration risks should be resolved before creating the
    permanent component structure?

### Required deliverable

Recommend one path: 1. Use an existing supported ESP-IDF integration. 2.
Adapt the engine as a local ESP-IDF component. 3. Maintain a small,
clearly isolated compatibility layer or fork. 4. Reconsider the engine
if integration cost is disproportionate.

Include exact source paths and a minimal build/test recipe.

------------------------------------------------------------------------

## R-02 --- Exact Waveshare board and display

**Priority:** P0\
**Suggested owner:** Board bring-up agent

### Questions

-   What is the exact Waveshare product name and hardware revision?
-   What LCD controller, native resolution, color depth, bus, and
    orientation does it use?
-   Which ESP-IDF driver or upstream component supports that controller?
-   Is there an existing PixelRoot32 display adapter for it?
-   Does the board use SPI, parallel RGB, or another interface? What are
    the pin assignments and timing constraints?
-   Is display DMA available and required for acceptable performance?
-   How much memory do one framebuffer and double-buffered framebuffers
    consume at the native resolution and selected pixel format?
-   Is partial-screen rendering or dirty-rectangle presentation
    supported by the display driver and engine?
-   What are the display refresh limits and realistic frame-rate
    expectations?
-   How are backlight control, display initialization, rotation, and
    sleep handled?
-   What is the exact touch controller and how is it connected?
-   What is the exact IMU part number and bus configuration?
-   What PSRAM configuration is present, and which buffers should or
    should not be allocated there?

### Required deliverable

A verified hardware table:

  Item                         Verified value   Evidence   Confidence / caveat
  ---------------------------- ---------------- ---------- ---------------------
  Board model/revision         TBD                         
  LCD controller/resolution    TBD                         
  Display bus/pins             TBD                         
  Touch controller/interface   TBD                         
  IMU model/interface          TBD                         
  Flash/PSRAM                  TBD                         
  Power/battery details        TBD                         

Do not fill the table from a similar-looking board; use the exact
product page, schematic, datasheet, or a hardware probe.

------------------------------------------------------------------------

## R-03 --- PixelRoot32 engine capabilities and extension points

**Priority:** P0\
**Suggested owner:** Engine API audit agent

### Questions

-   How are the game loop, scenes, rendering, camera, sprites, and
    tilemaps structured in the current source?
-   Which rendering formats and sprite/animation features are actually
    supported?
-   Is camera scrolling supported, and how are world and screen
    coordinates represented?
-   What collision and physics systems exist today?
-   Does the engine support kinematic actors, AABB collisions, one-way
    platforms, moving platforms, triggers, and collision filtering in
    the current revision?
-   Are collision callbacks, custom queries, or extension hooks
    available?
-   How are assets embedded, loaded, cached, and freed?
-   Can unused subsystems be disabled at compile time?
-   Which APIs allocate memory during update or rendering?
-   Is there an existing input abstraction suitable for normalized game
    actions?
-   Which APIs are stable and documented versus examples or internal
    implementation details?
-   What is the least invasive way to add missing platformer behavior?

### Required deliverable

An API capability matrix:

  --------------------------------------------------------------------------
  Need           Existing API / Coverage       Gap            Proposed
                 source                                       action
                 location                                     
  -------------- -------------- -------------- -------------- --------------
  Main loop      TBD                                          

  Sprite         TBD                                          
  rendering                                                   

  Tilemap        TBD                                          

  Camera         TBD                                          

  AABB collision TBD                                          

  One-way        TBD                                          
  platforms                                                   

  Moving         TBD                                          
  platforms                                                   

  Input          TBD                                          
  abstraction                                                 

  Asset loading  TBD                                          
  --------------------------------------------------------------------------

Conclude with a clear list of features to reuse and features that
require a prototype or custom implementation.

------------------------------------------------------------------------

## R-04 --- Q36XDV BLE HID gamepad

**Priority:** P0\
**Suggested owner:** BLE / input agent

### Questions

-   What exact Q36XDV model/revision is being used, and what BLE
    services does it advertise?
-   Does it use standard HID over GATT (HOGP), a proprietary GATT
    profile, or another mode?
-   What does its HID report descriptor contain? Which reports encode
    axes, buttons, and connection/battery state?
-   Can the selected ESP-IDF version and BLE host stack act as a
    compatible HID host?
-   What existing ESP-IDF examples or maintained libraries can be
    adapted?
-   Does support require NimBLE, Bluedroid, or a particular ESP-IDF
    configuration?
-   What are realistic connection, input-report, and reconnect
    latencies?
-   How should pairing/bonding, reconnect, disconnect, and stale-button
    state be handled?
-   Can BLE coexist with the display workload and other device
    peripherals without unacceptable latency or memory pressure?
-   What is the simplest fallback input method for development if the
    gamepad is unavailable?

### Required deliverable

1.  A verified protocol summary based on the device or captured reports.
2.  A recommended ESP-IDF host-stack and implementation path, with
    version constraints.
3.  A minimal experiment: scan, connect, subscribe/read reports, map one
    button, detect disconnect, and reconnect.
4.  A normalized mapping proposal for `MOVE_LEFT`, `MOVE_RIGHT`, `JUMP`,
    `PAUSE`, `CONFIRM`, and `CANCEL`.

**Risk note:** do not infer BLE HID-host compatibility from
documentation about a device acting as a HID peripheral.

------------------------------------------------------------------------

## R-05 --- *N*-style platformer physics and collision

**Priority:** P1\
**Suggested owner:** Gameplay-physics agent

### Questions

-   Which Metanet tutorials are most relevant to this game's collision
    and movement requirements?
-   Which concepts from the tutorials map directly to PixelRoot32's
    existing collision system?
-   Should the player be a kinematic actor with a custom movement
    controller, or can the engine's current controller provide the
    needed feel?
-   What fixed simulation timestep is practical on this target?
-   How should collision resolution handle horizontal and vertical
    motion, corners, thin hazards, and high-speed falls?
-   Does the engine perform swept tests or only overlap tests? If
    unknown, how can this be established?
-   How should one-way platforms distinguish landing from jumping
    through the underside?
-   How should moving platforms carry the player and transfer velocity?
-   How should jump buffering, coyote time, variable jump height, and
    input edges be represented?
-   How should hazard overlap be separated from solid collision
    response?
-   What collision data structure is appropriate for small tile-based
    levels?
-   Which edge cases should become deterministic regression tests?

### Required deliverable

A short physics design note with: - recommended controller model; -
coordinate and unit conventions; - timestep and movement update order; -
collision strategy and known limitations; - pseudocode for the critical
update path; - edge-case test matrix; - explicit engine gaps that
justify custom code.

Use *N* as a reference for principles, not a requirement to duplicate
its implementation or feel exactly.

------------------------------------------------------------------------

## R-06 --- Memory, frame time, and long-run stability

**Priority:** P1\
**Suggested owner:** Embedded performance agent

### Questions

-   What are the actual flash, internal RAM, and PSRAM budgets on the
    target board?
-   How much memory does the engine consume before game assets and BLE
    are enabled?
-   What framebuffer configuration is supported, and what are the memory
    costs?
-   Which data must live in DMA-capable/internal memory?
-   Can sprites and level data be read directly from flash, or are
    runtime copies required?
-   Which engine subsystems create dynamic allocations?
-   What are plausible update, render, and display-present budgets for
    the chosen resolution and frame rate?
-   What happens to frame time when BLE and IMU sampling run
    concurrently?
-   What measurements should be captured from the start (heap, largest
    free block, stack high-water marks, frame-time percentiles)?
-   What is a practical 30--60 minute soak test, and what constitutes a
    failure?
-   Which watchdog constraints affect long-running rendering or
    peripheral operations?

### Required deliverable

A resource-budget worksheet with **measured**, **estimated**, and
**unknown** values clearly distinguished. Propose initial budgets only
when backed by board facts or measurements, and provide a benchmark plan
to refine them.

------------------------------------------------------------------------

## R-07 --- Level data and reusable mechanisms

**Priority:** P1\
**Suggested owner:** Game-world architecture agent

### Questions

-   Should static terrain be represented by a tilemap, collision grid,
    authored polygons, or a hybrid?
-   What tile size is appropriate for the logical resolution and
    character scale?
-   How should visual tiles be separated from collision properties?
-   What compact representation should be used for spikes, moving
    platforms, switches, doors, checkpoints, exits, and rotating
    mechanisms?
-   Should levels be compiled into firmware initially, or should a small
    asset format be introduced immediately?
-   How should moving objects store phase, direction, speed, bounds, and
    activation state?
-   What level-size and entity-count limits should be explicit?
-   How can a level be authored or validated on a desktop without
    requiring the device for every iteration?
-   How should checkpoints and respawn locations be encoded?
-   How can mechanisms remain deterministic under a fixed timestep?

### Required deliverable

A minimal level schema and one example graybox level inspired by the
sketch: platforms at different heights, a gap, one moving platform, one
timed hazard, a checkpoint, and an exit. Avoid designing a
general-purpose editor before the first level works.

------------------------------------------------------------------------

## R-08 --- Touch and IMU interactions

**Priority:** P1\
**Suggested owner:** Sensor-input agent

### Questions

-   What touch-controller driver and calibration support are available
    for the exact board?
-   What IMU driver is available, and what sample rates/ranges are
    supported?
-   What are the device axes relative to the screen and board
    orientation?
-   What filtering, neutral-pose calibration, dead zone, and sensitivity
    controls are appropriate?
-   Can sensor sampling happen asynchronously without blocking the game
    loop?
-   Which optional actions genuinely benefit from touch or motion?
-   Can the core game remain fully playable with only the BLE gamepad?
-   How can motion input be prevented from introducing accidental
    movement or deaths?
-   What power and memory overhead do continuous IMU sampling and
    filtering add?

### Required deliverable

A short experiment plan for one low-risk interaction (such as
tilt-controlled menu navigation), with a clear success criterion. Do not
recommend motion-controlled precision movement unless testing shows that
it is controllable and enjoyable.

------------------------------------------------------------------------

## R-09 --- Test strategy and desktop iteration

**Priority:** P1\
**Suggested owner:** Testing / developer-experience agent

### Questions

-   Can the gameplay logic compile and run in a host-side test target
    independently of ESP-IDF hardware drivers?
-   Does PixelRoot32 provide a PC/simulation path, and can it share the
    same game code?
-   Which logic can be unit-tested without rendering?
-   How should fixed-step movement tests assert position, velocity,
    grounded state, and death/respawn behavior?
-   Can collision scenarios be generated as small deterministic test
    levels?
-   What is the fastest reliable hardware smoke-test loop?
-   What debug overlay is most useful without materially affecting
    performance?
-   How should logs, crash information, heap statistics, and build
    metadata be collected?
-   What automated checks should run for every change?

### Required deliverable

A test pyramid covering: 1. host-side pure-logic tests where feasible;
2. engine-level integration tests; 3. device smoke tests; 4.
BLE/display/IMU hardware tests; 5. a long-duration stability test.

Include the initial regression scenarios for movement and collisions.

------------------------------------------------------------------------

## R-10 --- Art and asset pipeline

**Priority:** P2\
**Suggested owner:** Game-art pipeline agent

### Questions

-   Which image formats and color depths does PixelRoot32 actually
    support?
-   Does the engine support sprite sheets, animation frames,
    transparency, and tile atlases?
-   What conversion or embedding tools are available?
-   What are the RAM and flash costs of candidate sprite/tile formats?
-   What logical resolution and pixel scale make the ninja and hazards
    readable on the display?
-   How should assets be named and organized to keep level data stable?
-   Can asset conversion be part of the build system?
-   Which animation frames are essential for the first playable level?

### Required deliverable

A minimal asset pipeline recommendation and a tiny test asset set: ninja
idle/run/jump/death placeholders, solid tile, one-way platform, spike,
moving platform, and exit marker. Optimize only after format
compatibility is verified.

------------------------------------------------------------------------

## R-11 --- Power, battery, and sleep

**Priority:** P2\
**Suggested owner:** Power-management agent

### Questions

-   What battery and charging circuitry are present on the exact board?
-   What current draw is expected with the display, BLE connection, IMU,
    and CPU active?
-   Can display brightness/backlight be controlled?
-   What sleep modes are practical while preserving expected wake
    behavior?
-   Does BLE gamepad reconnection work after the device wakes?
-   What should happen when battery level is low?
-   Which power measurements are possible with the available hardware?
-   Should power optimization wait until gameplay performance and input
    reliability are established?

### Required deliverable

A measured or source-backed power plan. Separate known board behavior
from assumptions and postpone complex sleep behavior until the core game
is stable.

------------------------------------------------------------------------

## R-12 --- Audio and feedback

**Priority:** P2\
**Suggested owner:** Optional multimedia agent

### Questions

-   Does the exact board expose a speaker, amplifier, buzzer, or
    suitable audio output?
-   Does PixelRoot32 support audio on the target, and what
    drivers/dependencies are needed?
-   What is the RAM/CPU cost of the available audio path?
-   Is audio important enough for the first playable prototype?
-   Can visual feedback alone communicate jump, checkpoint, damage, and
    death clearly?
-   What should happen when audio is disabled or unavailable?

### Required deliverable

A go/no-go recommendation for adding audio after the graybox milestone.
Do not add an audio subsystem just because the engine supports one.

------------------------------------------------------------------------

## 5. Cross-cutting questions

These questions should be resolved by combining findings from multiple
tracks.

### Build and dependency policy

-   Which ESP-IDF version will be pinned for the first prototype?
-   How will PixelRoot32 and third-party dependencies be pinned (commit,
    release, component version)?
-   What is the update policy for upstream changes?
-   How will local patches be documented and isolated?

### Simulation and rendering

-   Is the simulation rate fixed independently of display presentation?
-   What happens if rendering falls behind?
-   How will input snapshots be synchronized with simulation ticks?
-   Which code owns the mutable world state?
-   What maximum catch-up work is permitted after a late frame?

### Error handling

-   What happens if BLE disconnects during play?
-   What happens if a sensor fails to initialize?
-   Should the game continue with reduced functionality if touch or IMU
    is unavailable?
-   Which initialization failures are fatal versus recoverable?

### Reproducibility

-   Can a new contributor build and flash the project from a clean
    checkout?
-   Are board configuration, dependency versions, and setup steps
    documented?
-   Can the first test scene be reproduced with a single documented
    command sequence?

------------------------------------------------------------------------

## 6. Decision log

Update this table when research produces evidence strong enough to
support a decision. Keep links to the detailed agent report.

  Date         Decision                                      Evidence                         Confidence   Revisit when
  ------------ --------------------------------------------- -------------------------------- ------------ --------------
  2026-10-15   ESP-IDF 5.5+, esp32s3, 16 MB flash, octal     architecture.md                  high         A newer IDF breaks the drawer
               PSRAM, 240 MHz
  2026-10-15   PixelRoot32 1.12.0 submodule, APU v2.0.0,     no IDF component in the engine;  high         Upstream ships an IDF component
               custom esp_lcd drawer, Arduino shim          DisplayType::CUSTOM
  2026-10-15   240×240 logical and physical, no scale       architecture.md                  high         Art no longer fits the glass
  2026-10-15   Physics is ours, tutorial A, engine physics  architecture.md                  high         A graybox shows the engine solver is enough
               off. School demo is mechanics only
  2026-10-15   Bluedroid BLE-only on at boot. Wi-Fi off.    architecture.md                  high         HID map is captured
               Report map still open. PC reads DS4Windows
  2026-10-15   Device host is NimBLE central for Pad Emu.   architecture.md                  high         A Q36 report is captured
               Q36 HID map still unread. Wi-Fi off
  2026-10-15   Sim tick is 16 ms, at most 4 per update.     architecture.md                  medium       The cage feel is retuned
               First cage tune. BOOT resets the Pad Emu
               link. Present rate is drawn top-right
  2026-10-15   64 KB PSRAM arena, carved once, never freed  architecture.md memory table     high         A slice no longer fits
  2026-10-15   Nidhogg 1 art. NES voices off. Later PCM     architecture.md                  high         A speaker test
               on ES8311
  2026-10-15   Touch and IMU off. Frame budgets open        not brought up                   medium       A sensor experiment
  2026-10-15   Wall latch, slide cap 256, diagonal 1448.   architecture.md                  medium       The cage feel is retuned
               Speed above the run cap bleeds by 64.
               Down releases the latch
  2026-10-15   Static AABBs, cap 32, in the level slice.    architecture.md                  medium       The cage feel is retuned
               Four faces use the latch, floor, and
               ceiling. The sweep stops on the crossed face
  2026-10-15   Positions are a 240 cell plus a local     architecture.md                  medium       The cell camera or the
               offset. No view borders. Solids cap 256.                           platform bands change
               One 17-cell floor. world_reset re-rolls
               four platforms. The camera slides to the
               body-center cell. PLUS toggles debug.
               One BOOT resets the world; two within
               400 ms reset Pad Emu
  2026-10-16   The camera springs toward the body center. architecture.md                  medium       The spring tune changes
               A twelfth of the gap joins the velocity;
               six eighths of it remains
  2026-10-16   world_reset re-rolls two tall walls beside  architecture.md                  medium       The wall gap or the
               the spawn column and six platforms in the                           pocket stacks change
               side pockets. The column interior stays
               clear
  2026-10-16   The camera keeps the body on the horizontal architecture.md                  medium       The ground margin or the
               middle and the ground line 32 px above the                          spring tune changes
               bottom. A jump does not lift the view
  2026-10-16   The course is fixed: a wall-jump slot,     architecture.md                  medium       The course layout
               a pit, one spike, a checkpoint, and an                            or the death hold
               exit. Death holds 16 ticks, then the
               last checkpoint. The exit freezes
               until reset. The LCG is gone

## 7. Research execution plan

Run the following in parallel where possible:

### First wave --- blockers

-   **Agent A:** R-01, engine integration and build feasibility.
-   **Agent B:** R-02, exact board/display/touch/IMU hardware facts.
-   **Agent C:** R-04, Q36XDV BLE HID protocol and ESP-IDF host
    compatibility.
-   **Agent D:** R-03, PixelRoot32 API and subsystem audit.

### Second wave --- design after first-wave findings

-   **Agent E:** R-05, platformer physics and collision design.
-   **Agent F:** R-06, memory/performance budget and benchmark design.
-   **Agent G:** R-07 + R-09, level schema and testing workflow.

### Later wave --- optional features

-   **Agent H:** R-08, touch/IMU experiments.
-   **Agent I:** R-10, asset pipeline.
-   **Agent J:** R-11 + R-12, power and audio.

Agents may identify dependencies on other tracks, but should still
report what can be established independently. Avoid duplicate research
by assigning one owner per track.

## 8. Definition of research complete

The project is ready for implementation when the team can answer, with
evidence:

-   [ ] Which exact board and ESP-IDF version are targeted?
-   [ ] Can PixelRoot32 build and render a scene under the selected
    setup?
-   [ ] What is the smallest viable display backend?
-   [ ] Which engine physics/input/rendering APIs will be reused?
-   [ ] Can the Q36XDV be read as a BLE HID host, or is there a
    confirmed alternative?
-   [ ] What are the player's collision and movement update rules?
-   [ ] What are the initial memory and frame-time measurement methods?
-   [ ] What is the smallest graybox level used to validate the core
    loop?
-   [ ] Can movement/collision logic be tested repeatedly?
-   [ ] Are all remaining uncertainties explicitly recorded rather than
    hidden in assumptions?

**The goal is not to eliminate every unknown.** The goal is to remove
the blockers, make assumptions visible, and turn remaining uncertainty
into small experiments with observable outcomes.
