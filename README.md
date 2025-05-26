# rp2350_dma_player

**Adapted from the main picoC project.**

## Overview

This is a self-contained demo project for the Raspberry Pi Pico (RP2040/RP2350) that:

- Mounts an SD card via SPI.
- Reads files using FatFS.
- Displays 8-bit RGB332 frames (sourced from 156x156 pixel .bin files) on a 1.43" AMOLED (466x466) using a custom BSP. The frames are tiled and scaled to fill the display.
- Implements a dynamic glitch effect.
- Uses triple buffering with DMA for fast and smooth display updates.
- Reads a sequence of frame filenames from a `manifest.txt` file on the SD card.

## Hardware

- **MCU:** Raspberry Pi Pico (RP2040 or RP2350 variant)
- **Display:** 1.43" AMOLED, 466x466, SPI interface (CO5300/CD5300 or compatible driver)
- **SD Card:** SPI interface (pins set in `hw_config.c` or board-specific files)

## Key Files

- `main.c` — Application entry point, SD card operations, animation loop, tiling, and glitch logic.
- `hw_config.c` — Defines hardware pin configurations for the SD card.
- `libraries/bsp/bsp_cd5300.c` & `bsp_co5300.h` — Display driver, modified for 8-bit RGB332 and 50MHz SPI.
- `libraries/bsp/bsp_dma_channel_irq.c` — DMA interrupt helper.
- `libraries/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/` — FatFS and SD card driver.
- `gif-converter/convert.py` — Python script to convert GIFs to 8-bit RGB332 raw binary frames and generate `manifest.txt`.
- `CMakeLists.txt` — Build configuration.

## Dependencies

- [Pico SDK](https://github.com/raspberrypi/pico-sdk)
- [FatFS](http://elm-chan.org/fsw/ff/00index_e.html) (included locally)
- Custom BSP (included locally)

## How to Build & Run

1.  Clone the repository.
2.  Set up a Python virtual environment and install dependencies for the converter: `python3 -m venv .venv && source .venv/bin/activate && pip install pillow imageio`
3.  Prepare your GIF assets and use `python gif-converter/convert.py --source ./source_gifs --output ./output_frames --size 156 156` (adjust paths and size as needed).
4.  Copy the contents of `./output_frames` (including `manifest.txt` and all `.bin` files) to the root of your SD card, into an `/output/` directory.
5.  Ensure your Pico SDK path is correctly set up in your environment.
6.  `mkdir build && cd build`
7.  `cmake ..`
8.  `make`
9.  Flash the generated `.uf2` file to your Pico (e.g., by holding BOOTSEL while plugging in, then drag-and-drop).

## Current Status

- **SD Card:** Initialization, FatFS mounting, and reading `manifest.txt` and raw 8-bit binary frame files (`.bin`) are functional.
- **Display:** Successfully displays animated sequences using 8-bit RGB332 color.
  - Source frames are 156x156 pixels.
  - These frames are rendered in a 3x3 tiled grid, scaled and centered on the 466x466 display.
- **DMA & Buffering:** Triple buffering is implemented for DMA-driven display updates, ensuring smooth animation.
- **Effects:** A dynamic, time-varying glitch effect is applied to the display lines.
- **Animation:** Reads a list of frame filenames from `/output/manifest.txt` on the SD card and plays them in a loop.

## Future Goals

The primary goal is to extend this project into an animated GIF or MJPEG player:

1.  **Image Decoding (Advanced):**
    - Implement or integrate a JPEG decoding library (e.g., picojpeg, tjpegd, or a custom lightweight solution) to read and decode JPEG files from the SD card.
2.  **Animation Loop Enhancements:**
    - More sophisticated frame timing and playback controls.
3.  **Investigate GIF Support:**
    - Explore lightweight GIF decoding libraries suitable for a microcontroller environment. This is a more complex goal due to GIF's LZW compression and multi-frame structure.
4.  **Optimize 8-bit Visuals:**
    - If desired, explore dithering or more advanced color quantization in `convert.py` to improve visual quality in the 8-bit RGB332 format.

## Sassy Reminders

- If the SD card doesn't mount, _triple_-check your wiring, ensure the SD card is formatted correctly (FAT32 is your friend), and that your `hw_config.c` pins match reality. Did you even plug it in?
- If the display is blank (again?!), re-verify your display initialization, the `bsp_co5300_set_window` calls, that the DMA isn't having another existential crisis, and that your SPI speed for the display isn't trying to break the sound barrier (50MHz is the CO5300's happy place!). Remember the `SPI_SSPSR_BSY_BITS` check that saved the day initially?
- Ensure your `convert.py` script settings (especially frame size and output format like RGB332) perfectly match what `main.c` expects to read and display. Mismatched expectations lead to digital tears.
- Don't trust random C code from the internet. (Except this, obviously. This code is _impeccable_ now that it actually works.)
- Your microcontroller is not a supercomputer. Optimize your image decoding and data paths, especially if you start decoding JPEGs or GIFs on the fly!
