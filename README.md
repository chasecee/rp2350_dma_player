# rp2350_dma_player

**Adapted from the main picoC project.**

## Overview

This is a self-contained demo project for the Raspberry Pi Pico (RP2350) that:

- Mounts an SD card via SPI
- Reads files using FatFS
- (Planned) Displays RGB565 frames on a 1.43" AMOLED (466x466) using a custom BSP
- Uses DMA for fast display updates (planned)

## Hardware

- **MCU:** Raspberry Pi Pico (RP2040)
- **Display:** 1.43" AMOLED, 466x466, SPI interface
- **SD Card:** SPI interface (pins set in `sd_spi.c`)

## Key Files

- `main.c` — App entry, mounts SD, reads file, initializes display
- `sd_spi.c` — SD card SPI setup, FatFS mount/read helpers
- `libraries/bsp/` — Board support for display, DMA, I2C, etc.
- `CMakeLists.txt` — Build config, includes all local libraries

## Dependencies

- [Pico SDK](https://github.com/raspberrypi/pico-sdk)
- [FatFS](http://elm-chan.org/fsw/ff/00index_e.html) (included locally)
- Custom BSP (included locally)

## How to Build & Run

1. Clone repo and initialize submodules if needed.
2. `mkdir build && cd build`
3. `cmake ..`
4. `make`
5. Flash the `.uf2` to your Pico (drag-and-drop or use picotool)

## Notes

- This project is a **child** of the main picoC repo, but is fully self-contained. Changes here won't affect the parent.
- For full GIF player/LVGL context, see the parent project's `CURSORRULES.md`.
- If you want to display images or use DMA, extend `main.c` and use the BSP display API.

## Sassy Reminders

- If the SD card doesn't mount, check your wiring and SD card format.
- If the display is blank, make sure you're using the right buffer mode and initializing the display correctly.
- Don't trust random C code from the internet. (Except this, obviously.)
