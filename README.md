# rp2350_dma_player

**Adapted from the main picoC project. Now featuring a blazing fast animation player!**

## Overview

This is a high-performance animation player for the Raspberry Pi Pico (RP2040/RP2350) that:

- Mounts an SD card via SPI with optimized chunked reading (64KB chunks).
- Reads animation frames from a single `frames.bin` file using FatFS.
- Displays 233x233 RGB565 frames centered on a 1.43" AMOLED (466x466) using a custom BSP.
- Uses double-buffered DMA for smooth, high-FPS animation playback.
- Achieves ~30fps with minimal stuttering through aggressive performance optimizations.

## Hardware

- **MCU:** Raspberry Pi Pico (RP2040 or RP2350 variant) - _running at 150MHz for maximum sass_
- **Display:** 1.43" AMOLED, 466x466, SPI interface (CO5300/CD5300 or compatible driver)
- **SD Card:** SPI interface (pins set in `hw_config.c` or board-specific files)

## Key Files

- `src/main.c` — Application entry point, display loop with optimized DMA batching.
- `src/sd_loader.c` & `sd_loader.h` — High-performance double-buffered frame loader.
- `src/dma_config.c` & `dma_config.h` — DMA infrastructure for SD and display operations.
- `src/debug.h` — Debug macros (because debugging is inevitable).
- `hw_config.c` — Hardware pin configurations.
- `libraries/bsp/bsp_cd5300.c` & `bsp_co5300.h` — Display driver with DMA support.
- `libraries/bsp/bsp_dma_channel_irq.c` — DMA interrupt management.
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
7. Put your `frames.bin` file on the SD card root (233x233 RGB565 format, see Animation Format below).

## Animation Format

The player expects a single `frames.bin` file containing concatenated RGB565 frames:

- **Frame size:** 233x233 pixels
- **Format:** RGB565 (2 bytes per pixel, little-endian)
- **File structure:** Frame0 + Frame1 + Frame2 + ... (no headers, just raw pixel data)
- **Total frame size:** 233 × 233 × 2 = 108,578 bytes per frame

Use the included gif-converter tool to generate compatible frames from GIFs or image sequences.

## Current Status (Performance Beast Mode Activated!)

- **SD Card:** Optimized chunked reading with 64KB blocks for maximum throughput.
- **Display:** Native 233x233 rendering centered on 466x466 display with DMA acceleration.
- **Animation:** Smooth 30fps playback with double-buffered loading and minimal stuttering.
- **Memory:** Efficient double-buffering keeps only 2 frames in RAM at any time.
- **DMA:** Rock-solid DMA operations for both SD reads and display writes.

## Performance Optimizations

- **Chunked SD Reading:** 64KB reads minimize SD card latency.
- **Sequential File Access:** Single open `frames.bin` file with intelligent seeking.
- **DMA Batching:** Optimized line batching (31 lines per DMA) prevents display artifacts.
- **Double Buffering:** Background loading eliminates frame drops.
- **Clock Speed:** 150MHz system clock for maximum performance.
- **Memory Management:** Static allocation and aligned buffers for optimal DMA.

## Recent Achievements

- ✅ Eliminated frame duplication artifacts through proper buffer management
- ✅ Achieved best FPS yet with minimal stuttering
- ✅ Optimized SD card performance (dramatic load time improvements)
- ✅ Implemented bulletproof DMA synchronization
- ✅ Native 233x233 display without scaling artifacts

## Future Goals

Now that we've conquered smooth animation playback, potential enhancements include:

1. **Direct GIF Support:** Skip the conversion step with on-device GIF decoding.
2. **Variable Frame Rates:** Support different target FPS settings.
3. **Audio Sync:** Add audio playback synchronized with animation.
4. **Real-time Effects:** Frame filters, transitions, or overlays.
5. **Multiple Animation Support:** Playlist functionality with seamless transitions.

## Sassy Reminders

- If your animation stutters, check your SD card speed. Not all SD cards are created equal, and yours might be having performance anxiety.
- If you're getting duplication artifacts, don't even _think_ about messing with `LINES_PER_DMA` without understanding the math. 233 is prime for a reason!
- Your `frames.bin` file better be properly formatted RGB565 or you'll get a colorful mess (and not the good kind).
- Don't trust random performance "optimizations" from Stack Overflow. This code has been battle-tested at 30fps!
- Remember: premature optimization is the root of all evil, but _mature_ optimization is the root of all smooth animations. 😎
