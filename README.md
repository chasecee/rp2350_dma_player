# rp2350_dma_player

**Adapted from the main picoC project.**

## Overview

This is a self-contained demo project for the Raspberry Pi Pico (RP2040/RP2350) that:

- Mounts an SD card via SPI.
- Reads files using FatFS.
- Displays RGB565 frames on a 1.43" AMOLED (466x466) using a custom BSP.
- Uses DMA for fast display updates.

## Hardware

- **MCU:** Raspberry Pi Pico (RP2040 or RP2350 variant)
- **Display:** 1.43" AMOLED, 466x466, SPI interface (CO5300/CD5300 or compatible driver)
- **SD Card:** SPI interface (pins set in `hw_config.c` or board-specific files)

## Key Files

- `main.c` — Application entry point, SD card operations, display testing loop.
- `hw_config.c` — Defines hardware pin configurations.
- `libraries/bsp/bsp_cd5300.c` & `bsp_co5300.h` — Display driver.
- `libraries/bsp/bsp_dma_channel_irq.c` — DMA interrupt helper.
- `libraries/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/` — FatFS and SD card driver.
- `CMakeLists.txt` — Build configuration.

## Dependencies

- [Pico SDK](https://github.com/raspberrypi/pico-sdk)
- [FatFS](http://elm-chan.org/fsw/ff/00index_e.html) (included locally)
- Custom BSP (included locally)

## How to Build & Run

1. Clone the repository.
2. Ensure your Pico SDK path is correctly set up in your environment.
3. `mkdir build && cd build`
4. `cmake ..`
5. `make`
6. Flash the generated `.uf2` file to your Pico (e.g., by holding BOOTSEL while plugging in, then drag-and-drop).

## Current Status (As of recent DMA fix!)

- **SD Card:** Initialization and file reading (e.g., `test.txt`) are functional.
- **Display:** Successfully displaying full-screen solid colors (Red, Green, Blue) cycling sequentially.
- **DMA:** DMA-driven display updates are **WORKING!** The previous blank screen issue with DMA has been resolved by ensuring the SPI peripheral completes its transmission before Chip Select is de-asserted in the DMA callback.

## Future Goals

The primary goal is to extend this project into an animated GIF or MJPEG player:

1.  **Image Decoding:**
    - Implement or integrate a JPEG decoding library (e.g., picojpeg, tj<x_bin_73>, or a custom lightweight solution) to read and decode JPEG files from the SD card.
    - Alternatively, support a simple raw binary frame format.
2.  **Animation Loop:**
    - Read image frames sequentially from the SD card.
    - Decode and display each frame.
    - Manage frame timing for smooth animation.
3.  **Investigate GIF Support:**
    - Explore lightweight GIF decoding libraries suitable for a microcontroller environment. This is a more complex goal due to GIF's LZW compression and multi-frame structure.

## Sassy Reminders

- If the SD card doesn't mount, _triple_-check your wiring, ensure the SD card is formatted correctly (FAT32 is your friend), and that your `hw_config.c` pins match reality. Did you even plug it in?
- If the display is blank (again?!), re-verify your display initialization, the `bsp_co5300_set_window` calls, and that the DMA isn't having another existential crisis. Remember the `SPI_SSPSR_BSY_BITS` check that saved the day?
- Don't trust random C code from the internet. (Except this, obviously. This code is _impeccable_ now.)
- Your microcontroller is not a supercomputer. Optimize your image decoding and data paths!
