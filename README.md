# ESPecter

A small 2D platformer for the Waveshare ESP32-S3-Touch-LCD-1.54. Decisions live in [architecture.md](architecture.md).

## Prerequisites

- Git, with the submodules below.
- For the board: ESP-IDF 5.5 or newer. `flash.bat` uses `IDF_PATH`, or `%USERPROFILE%\esp\esp-idf` if that is unset.
- For the PC: CMake and a C++ compiler. The PC script fetches SDL2. It does not need MSYS2.

## Checkout

`pc.bat` needs the submodules on disk. `flash.bat` runs the same init when the engine headers are missing.

```text
git submodule update --init
```

Pins:

- `third_party/pixelroot32` at tag `1.12.0`
- `third_party/pixelroot32-apu` at tag `v2.0.0` (the engine headers include it even with audio compiled out)

## PC

```text
pc.bat
```

Opens a window titled ESPecter with the 16×16 figure and a fixed course. Cell `(0, 0)` has a black floor, two tall walls beside the spawn column, and six black platforms in the side pockets. The walls stop 24 px above the floor, so a jump from the floor catches the face, and the 48 px gap between the faces is short enough for a wall jump to reach the other face. A 48 px pit separates that floor from a second floor. On the second floor a magenta spike fills the bottom 8 px of the body, a white checkpoint outline stands past it, and a white exit outline stands at the far end. Debug starts off. Backtick toggles it: the present rate at the top-right in a 5×7 font, and a white outline of each 240 cell on screen. The keyboard mirrors the Q36 face: WASD is the d-pad, JIKL is X, Y, A, and B, Q and E are L and L2, U and O are R2 and R, and F, G, and H are View, Home, and Menu. A and D run, and K jumps. Standing still is idle. Below the run cap the floor plays walk, and the cap plays the run loop. Reversing on the ground or in the air plays that turn once. Leaving a wall plays the dash sheet once. Landing on a black rectangle is a floor, so the jump stays vertical and the body can run along the top. A vertical face latches a slow slide, and A and D do not push while it holds. S releases the latch; the body falls at full speed until horizontal speed crosses that face again. K on the rising edge, while latched and off the floor, leaves on a diagonal. Hitting the underside stops the rise. W does not move the body. The view keeps the body on the horizontal middle and the ground under its feet along the bottom. A jump leaves that ground where it is. Past either floor, or off the left of the first cell, the feet drop below the slab and the body dies. The spike does the same. The body holds for about a quarter second, then returns to the last checkpoint with the camera snapped there. Spawn is that checkpoint until the outline is touched. Touching the exit stops the body until R restarts the course and clears the checkpoint. R also returns the body to the center of the first cell and snaps the view. The room does not change. DS4Windows, in its default Xbox 360 profile, fills the same snapshot: the left stick or d-pad runs, d-pad down releases a wall latch, and the south button jumps, including the diagonal off a latched wall. With neither, the body falls and rests on the floor. Close the window to exit. This build does not open a Bluetooth radio.

## Board

```text
flash.bat
flash.bat --port COM5
monitor.bat
```

`flash.bat` builds and flashes. If the PixelRoot32 headers are missing it runs `git submodule update --init`. If `build` exists without `CMakeCache.txt`, it deletes that directory so `set-target` can run. `flash.bat`, `pc.bat`, and `monitor.bat` print how long each step took. The panel shows the same course: the figure, the two floors, the walls, the six platforms, the magenta spike, and the white checkpoint and exit. Debug starts off. PLUS toggles it: the battery percent at the top-left, the present rate at the top-right in a 5×7 font, and a white outline of each 240 cell on screen. The ground is red before the scan, amber while it looks, and a darker green than the pademu bar once Pad Emu links. The body falls to the floor. Once linked, left and right run and the south button jumps. Standing is idle, a slow floor speed is walk, full run is the run loop, reversing plays the matching turn once, and leaving a wall plays the dash sheet once. Landing on a black rectangle is a floor. A vertical face latches a slow slide. D-pad down releases it. The south button on a rising press, while latched and off the floor, leaves on a diagonal. Hitting the underside stops the rise. D-pad up does not move the body. The view keeps the body on the horizontal middle and the ground under its feet along the bottom. A jump leaves that ground where it is. Past either floor, or off the left of the first cell, the feet drop below the slab and the body dies. The spike does the same. The body holds for about a quarter second, then returns to the last checkpoint with the camera snapped there. Spawn is that checkpoint until the outline is touched. Touching the exit stops the body until one BOOT restarts the course. If the phone drops, the link falls in about two seconds, the ground goes amber, and the central looks again. One press of BOOT restarts the course after 400 ms: the body returns to the first cell, the checkpoint clears back to spawn, and the view snaps there. The room does not change. Two presses inside that window drop the Pad Emu link and scan again. That is how a vanished emulator is dropped without a disconnect: the ground leaves green, the scan is amber, and it turns green again when Pad Emu notifies. The world does not reset on that double press. Holding BOOT does not repeat. The backlight fades on, and PWR fades it off or back on. The log shows the BLE controller, then `linked`, plus the arena capacity and offset, free internal heap, the largest free internal block, and free PSRAM. The figure still appears with no phone in range. Pad Emu is the phone app in the Waveshare board repo (`pad.bat` there). The device does not init I2C, Wi-Fi, the codec, or the IMU.
