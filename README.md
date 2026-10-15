# ESPecter

A small 2D platformer for the Waveshare ESP32-S3-Touch-LCD-1.54. Decisions live in [architecture.md](architecture.md).

## Prerequisites

- Git, with the submodules below.
- For the board: ESP-IDF 5.5 or newer. `flash.bat` uses `IDF_PATH`, or `%USERPROFILE%\esp\esp-idf` if that is unset.
- For the PC: CMake and a C++ compiler. The PC script fetches SDL2. It does not need MSYS2.

## Checkout

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

Opens a window titled ESPecter with one 16×16 rectangle and the current frame rate. The keyboard mirrors the Q36 face: WASD is the d-pad, JIKL is X, Y, A, and B, Q and E are L and L2, U and O are R2 and R, and F, G, and H are View, Home, and Menu. The rectangle steps with the d-pad and rises on the south button. DS4Windows, in its default Xbox 360 profile, fills the same snapshot: left stick or d-pad slides, the south button raises it. With neither, the rectangle stays centered. Close the window to exit. This build does not open a Bluetooth radio.

## Board

```text
flash.bat
flash.bat --port COM5
monitor.bat
```

`flash.bat` builds and flashes. The panel shows the same rectangle, still, with the backlight on. The log shows the BLE controller, the arena capacity and offset, free internal heap, the largest free internal block, and free PSRAM. No gamepad is required. The device does not init I2C, Wi-Fi, the codec, or the IMU.
