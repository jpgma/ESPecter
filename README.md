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

Opens a window titled ESPecter with one 16×16 rectangle, a black floor seventeen cells wide, and four black platforms in the spawn cell. Debug starts off. Backtick toggles it: the present rate at the top-right in a 5×7 font, and a white outline of each 240 cell on screen. The keyboard mirrors the Q36 face: WASD is the d-pad, JIKL is X, Y, A, and B, Q and E are L and L2, U and O are R2 and R, and F, G, and H are View, Home, and Menu. A and D run, and K jumps. Landing on a black rectangle is a floor, so the jump stays vertical and the body can run along the top. A vertical face latches a slow slide, and A and D do not push while it holds. S releases the latch; the body falls at full speed until horizontal speed crosses that face again. K on the rising edge, while latched and off the floor, leaves on a diagonal. Hitting the underside stops the rise. W does not move the body. The view springs so the body stays near the center, with a little overshoot when it stops. Past either end of the floor the body falls with no bound. R resets the world: the body returns to the center of the first cell, the view snaps there, and the four platforms are dealt again. The floor stays. DS4Windows, in its default Xbox 360 profile, fills the same snapshot: the left stick or d-pad runs, d-pad down releases a wall latch, and the south button jumps, including the diagonal off a latched wall. With neither, the body falls and rests on the floor. Close the window to exit. This build does not open a Bluetooth radio.

## Board

```text
flash.bat
flash.bat --port COM5
monitor.bat
```

`flash.bat` builds and flashes. If the PixelRoot32 headers are missing it runs `git submodule update --init`. If `build` exists without `CMakeCache.txt`, it deletes that directory so `set-target` can run. `flash.bat`, `pc.bat`, and `monitor.bat` print how long each step took. The panel shows the same rectangle, the black floor, and the four platforms. Debug starts off. PLUS toggles it: the battery percent at the top-left, the present rate at the top-right in a 5×7 font, and a white outline of each 240 cell on screen. The ground is red before the scan, amber while it looks, and a darker green than the pademu bar once Pad Emu links. The body falls to the floor. Once linked, left and right run and the south button jumps. Landing on a black rectangle is a floor. A vertical face latches a slow slide. D-pad down releases it. The south button on a rising press, while latched and off the floor, leaves on a diagonal. Hitting the underside stops the rise. D-pad up does not move the body. The view springs so the body stays near the center. If the phone drops, the link falls in about two seconds, the ground goes amber, and the central looks again. One press of BOOT resets the world after 400 ms: the body returns to the first cell and the four platforms are dealt again. Two presses inside that window drop the Pad Emu link and scan again. That is how a vanished emulator is dropped without a disconnect: the ground leaves green, the scan is amber, and it turns green again when Pad Emu notifies. The world does not reset on that double press. Holding BOOT does not repeat. The backlight fades on, and PWR fades it off or back on. The log shows the BLE controller, then `linked`, plus the arena capacity and offset, free internal heap, the largest free internal block, and free PSRAM. The rectangle still appears with no phone in range. Pad Emu is the phone app in the Waveshare board repo (`pad.bat` there). The device does not init I2C, Wi-Fi, the codec, or the IMU.
