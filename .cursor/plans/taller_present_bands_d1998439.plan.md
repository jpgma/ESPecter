---
name: Taller present bands
overview: Reserve two 120-row DMA bands (115200 bytes, one RGB565 frame) before NimBLE fragments internal RAM, so a present pays two address windows instead of fifteen.
todos:
  - id: alloc-early
    content: Add st7789_alloc_bands and call it from app_main before NVS and NimBLE
    status: completed
  - id: bands-120
    content: Point the drawer at that buffer, 120-row bands, max_transfer_sz 32768
    status: completed
  - id: architecture
    content: Update architecture.md band size, placement, and decision log
    status: completed
isProject: false
---

# Taller present bands

This boot (app `a756408`) shows the hole before anything in `app_main` allocates: `heap_init` at `3FCA7180` len `00042590` (265 KiB). After NimBLE and the current 16-row bands, the log is `internal free 158263 largest 83968`. [`pad_link_start`](main/app_main.c) is what splits that 265 KiB region. A 120-row pair is 115200 bytes and fits in the early hole. It does not fit in the 83968 hole left once the central is up.

Two 120-row bands together are one RGB565 frame, not a second copy. The 57600-byte 8bpp buffer stays. Scene draw still finishes the whole view before the first SPI byte: the ground rect writes every row, so draw cannot start a band early.

```mermaid
flowchart LR
  boot[app_main] --> bands["st7789_alloc_bands 115200"]
  bands --> ble[pad_link_start]
  ble --> present[present task]
  present --> draw[scene draw 8bpp]
  draw --> spi["two draw_bitmap windows"]
```

## Allocate before BLE

Add `st7789_alloc_bands` in [`host/st7789_drawer.cpp`](host/st7789_drawer.cpp), declared in the existing `extern "C"` header. It `heap_caps_malloc`s `240 * 120 * 2 * sizeof(uint16_t)` with `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL` and keeps the pointer. [`main/app_main.c`](main/app_main.c) calls it after `battery_start` and before `nvs_start` / `pad_link_start`. A null result aborts, same as the current framebuffer failure.

[`st7789_drawer::init`](host/st7789_drawer.cpp) uses that pointer. It no longer allocates the bands. `k_band_rows` becomes 120.

## SPI

One band is 57600 bytes. The S3 DMA length register caps a transaction at 32768 bytes (`SPI_LL_DMA_MAX_BIT_LEN`). Set `max_transfer_sz` to 32768. `esp_lcd` already splits a larger color buffer into chunks under one RAMWR and runs `on_color_trans_done` only on the last chunk, so the counting semaphore in `sendBuffer` still matches one `draw_bitmap`. The loop itself stays: fill the idle band while the other is on the wire, then two windows instead of fifteen.

## Record

Update [`architecture.md`](architecture.md) in the same change: the hardware bullet, the display note, and the present-band row (15360 becomes 115200, two 120-row bands). State that the pair is one RGB565 frame, taken before the central, and that a second copy is not reserved. Add a decision-log line. Leave the measured free-heap cells alone until a new boot log exists.
