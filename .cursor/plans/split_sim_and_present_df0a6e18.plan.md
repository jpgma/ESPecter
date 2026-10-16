---
name: Split sim and present
overview: Pin the sim to core 0 and present to core 1 so the glass never waits on a tick. Present draws the latest tick while the backlight is up, stops when duty hits 0, and the sim follows into light sleep 30s later so the PSRAM arena is still there on wake.
todos:
  - id: engine-present
    content: Device Engine::run skips update so present cannot step the sim
    status: completed
  - id: snapshot
    content: Seqlock draw snapshot in the host; draw reads the stable view
    status: completed
  - id: tasks
    content: Pin sim to core 0 and present to core 1; PC stays one thread
    status: completed
  - id: dark-sleep
    content: Stop present at duty 0, stop sim 30s later, light-sleep until PWR
    status: completed
  - id: architecture
    content: Record the split, the snapshot, the light sleep, and the decision log
    status: completed
isProject: false
---

# Split sim and present

The device loop in [`third_party/pixelroot32/src/core/Engine.cpp`](third_party/pixelroot32/src/core/Engine.cpp) calls `update()` and then `draw()` / `present()` on the same thread. [`host/engine_host.cpp`](host/engine_host.cpp) steps `world_tick` inside that `update`, so a long tick stalls the glass and a 12 ms SPI present stalls the 16 ms clock. ESPET’s useful rule here is the split: the publisher never waits, and present never waits. The rest of that model does not fit this pixel sim.

## What to take, and what to leave

- Take the pin: sim on core 0, present on core 1. A stalled sim holds the last published tick. Present ships a frame while the backlight duty is above 0.
- Leave interpolation. The body and camera are integer pixels at 62.5 Hz, and the wire is already about 12.3 ms, so a blend between ticks would not buy a visible frame.
- Leave the still-frame SPI skip. The corner number is presents. A frozen exit would drop that number to 0.
- Leave the world SoA in the PSRAM arena. Only a draw snapshot moves to internal RAM, so present does not read the live slice.
- Leave task create out of the vendored engine. `PR32_DEFAULT_MAIN_CORE` is unused by this binary. Affinity is a product decision and stays in the host, next to the drawer. The engine patch is only “device `run` does not step the sim.”
- Leave NimBLE on core 0 at its current priority. Sim and present sit at priority 2, above the unpinned backlight and pad tasks (priority 1) and under the BT host. Those two tasks stay unpinned; they already sleep 20 ms.

## Device shape

```mermaid
flowchart LR
  simTask["sim core 0"] -->|"seqlock publish"| slot["snapshot internal RAM"]
  slot -->|"load once per frame"| presentTask["present core 1"]
  presentTask --> glass["draw then SPI present"]
```

[`main/app_main.c`](main/app_main.c) stays the starter. On device, [`engine_host_run`](host/engine_host.h) calls `world_init`, publishes the spawn view once, then starts the two tasks and returns. `app_main` returns with it. The main task is not the present loop.

- Sim task, pinned to core 0, stack 3072. It sleeps 1 ms, reads `millis` (the `esp_timer` clock already in [`compat/Arduino.h`](compat/Arduino.h)), and calls the existing `sim_advance`. The 16 ms step, the 4-tick cap, and the dropped remainder stay as written in the physics section. The clock is no longer the gap since the last present.
- Present task, pinned to core 1, stack 4096. It owns `Engine` construction, `init`, and the `run` loop. `sendBuffer` already blocks on the SPI semaphore, so idle on core 1 still runs during the wire time.

The PC target does not spawn tasks. `Engine::run` still calls `update` then `draw`. `update` publishes, `draw` reads the same snapshot, on one thread.

## Snapshot

One slot in internal BSS, not in the 64 KB arena. A `std::atomic` sequence in the host: odd while the sim writes, even when stable. `volatile` is not a barrier on this SMP. Present copies a consistent slot into a second static view at the start of the frame and draws only that. A torn read retries. The sim never takes a lock.

The slot holds what `draw` reads today: camera origin, body rect, clip / frame / face, the static AABBs, spike, checkpoint, exit, the debug flag, and on device the link ground and the battery percent. Cap the AABB array at the existing 256. Export that cap from [`game/world.h`](game/world.h) so the host does not invent a second number. `draw` stops calling `world_*`, `pad_link_read`, and `battery_percent`. Those stay on the sim side.

FPS stays on the present task only. It is not part of the slot.

## Engine patch

In the device branch of `Engine::run`, remove the `update()` call. Leave `processEvents`, `draw`, `present`, and `yield`. Update the `run` comment in [`third_party/pixelroot32/include/core/Engine.h`](third_party/pixelroot32/include/core/Engine.h) so the device path is present-only. The native loop is unchanged.

New host functions get the one-line Portuguese comment with a stable id. [`architecture.md`](architecture.md) gains the runtime split, the snapshot row in the memory table, the light-sleep rule, and a decision-log line in the same change.

## Backlight off, then light sleep

The PSRAM arena cannot survive deep sleep. Deep sleep powers off internal SRAM and the octal PSRAM controller, then boots from ROM. [`app_main`](main/app_main.c) would run again, and `heap_caps_malloc` would return a new block. RTC slow memory is 8 KB, smaller than the 64 KB arena and smaller than the level slice. A flash copy would be a second allocation after reboot, not the same bytes.

Light sleep is the mode that keeps this arena. `esp_light_sleep_start` returns in the same tasks. The pointer from boot is still the world, the camera, and the checkpoint. DRAM and the octal PSRAM stay powered, so nothing is copied to RTC or flash. The decision record should say light sleep, not deep sleep.

[`host/st7789_drawer.cpp`](host/st7789_drawer.cpp) already fades LEDC on PWR. A full swing is about 410 ms, so the press is not the moment the glass goes dark. The fade-end callback is. When that callback sees duty 0:

- Present does not start another frame. An in-flight SPI band finishes; aborting it would tear the panel GRAM. After that, the present task waits. No draw, no SPI, no palette expand.
- A 30 s `esp_timer` starts. The sim keeps the 16 ms step for those 30 s, including held input, so a quick PWR press resumes the same motion.
- PWR fading back on before the timer fires cancels it. Present resumes as the fade starts, while the duty is still climbing.

When the 30 s elapses with duty still 0, the sim task stops calling `world_tick`. The world bytes stay where they are. It waits until PWR has been released, stops the NimBLE central so a connection event cannot wake the chip, holds `BAT_EN` (GPIO2) high, and enters light sleep. Wake is GPIO5 low. USB Serial/JTAG is down for that stretch, same as the hardware note already says for deep sleep.

On wake the chip has not rebooted. The sim task fades the backlight on, starts the central scanning again, and both loops resume. The existing drop path covers the lost Pad Emu link. The ST7789 is not reset; its GRAM still holds the last frame, and the next present draws a full one.

The PC build has no backlight and no sleep. The gate is device-only.
