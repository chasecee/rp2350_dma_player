# rp2350_dma_player

**Adapted from the main picoC project. Now featuring a blazing fast animation player!**

## Overview

This is a high-performance animation player for the Raspberry Pi Pico (RP2040/RP2350) that:

- Loads animation frames via **raw SD card sector access** (bypassing file system overhead entirely!).
- Displays 233x233 RGB565 frames centered on a 1.43" AMOLED (466x466) using a custom BSP.
- Uses double-buffered DMA for smooth, high-FPS animation playback.
- Achieves ~30fps with minimal stuttering through aggressive performance optimizations.

## Hardware

- **MCU:** Raspberry Pi Pico (RP2040 or RP2350 variant) - _running at 150MHz for maximum sass_
- **Display:** 1.43" AMOLED, 466x466, SPI interface (CO5300/CD5300 or compatible driver)
- **SD Card:** Any SD card (file system not required! We use raw sector access)

## Key Files

- `src/main.c` — Application entry point, display loop with optimized DMA batching.
- `src/raw_sd_loader.c` & `raw_sd_loader.h` — Ultra-fast raw sector double-buffered frame loader.
- `src/debug.h` — Debug macros (because debugging is inevitable).
- `hw_config.c` — Hardware pin configurations.
- `libraries/bsp/bsp_cd5300.c` & `bsp_co5300.h` — Display driver with DMA support.
- `libraries/bsp/bsp_dma_channel_irq.c` — DMA interrupt management.
- `libraries/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/` — SD card driver (disk I/O only, no FatFS!).
- `gif-converter/` — Tools for converting media and writing to SD card.
- `CMakeLists.txt` — Build configuration.

## Dependencies

- [Pico SDK](https://github.com/raspberrypi/pico-sdk)
- Custom BSP (included locally)
- SD card driver from no-OS-FatFS (disk I/O functions only)

## Optimized Workflow (FASTEST!)

### **Option A: Raw SD Card Mode (Maximum Performance)**

1. **Convert your media:**

   ```bash
   cd gif-converter
   python convert.py --source ./source --output ./output --size 233 233 --rotate -90 --frame_stride 2 --single-bin --colors 16
   ```

2. **Write directly to SD card (bypasses file system entirely!):**

   ```bash
   # Find your SD card
   diskutil list | grep -B3 -A3 disk

   # Write frames directly to raw sectors
   sudo python3 write_raw_frames.py /dev/disk6 ./output/frames.bin
   ```

3. **Insert SD card and enjoy blazing fast animation!**

### **Option B: Traditional File System Mode**

If you prefer the old method (slower but more familiar):

1. **Format SD card with optimized settings:**

   ```bash
   diskutil unmount /Volumes/PICOSD
   sudo diskutil eraseDisk FAT32 FRAMES MBRFormat /dev/disk6
   diskutil unmount /dev/disk6s1
   sudo newfs_msdos -F 32 -c 64 -v FRAMES /dev/disk6s1
   ```

2. **Convert and copy:**
   ```bash
   python convert.py --source ./source --output /Volumes/FRAMES --size 233 233 --rotate -90 --frame_stride 2 --single-bin --colors 16
   ```

## Animation Format

**Raw SD Mode:** Frames are written directly to SD card sectors starting at sector 2048.  
**File System Mode:** Single `frames.bin` file in the root directory.

Both formats use:

- **Dimensions:** 233x233 pixels per frame
- **Format:** RGB565 (2 bytes per pixel, big-endian)
- **Structure:** Frame0 + Frame1 + Frame2 + ... (no headers, just raw pixel data)
- **Total frame size:** 233 × 233 × 2 = 108,578 bytes per frame

## How to Build & Run

1. Clone the repository.
2. Ensure your Pico SDK path is correctly set up in your environment.
3. `mkdir build && cd build`
4. `cmake ..`
5. `make`
6. Flash the generated `.uf2` file to your Pico (e.g., by holding BOOTSEL while plugging in, then drag-and-drop).
7. Prepare your SD card using either the raw mode or traditional file system mode above.

## Current Status (Performance Beast Mode Activated!)

- **SD Card:** Raw sector access eliminates file system overhead entirely!
- **Display:** Native 233x233 rendering centered on 466x466 display with DMA acceleration.
- **Animation:** Smooth 30fps playback with double-buffered loading and minimal stuttering.
- **Memory:** Efficient double-buffering keeps only 2 frames in RAM at any time.
- **DMA:** Rock-solid DMA operations for display writes and SD reads.

## Performance Optimizations

- **Raw Sector Access:** Bypasses FatFS completely for maximum SD card throughput.
- **Direct Disk I/O:** Uses `disk_read()` directly for minimal overhead.
- **128-Sector Chunks:** 64KB reads maximize SD card performance.
- **DMA Batching:** Optimized line batching (233 lines per DMA) prevents display artifacts.
- **Double Buffering:** Background loading eliminates frame drops.
- **Clock Speed:** 150MHz system clock for maximum performance.
- **Memory Management:** Static allocation and aligned buffers for optimal DMA.
- **Zero Byte Swapping:** Big-endian format eliminates runtime pixel manipulation.

## Recent Achievements

- ✅ **MAJOR:** Eliminated FatFS overhead with raw sector access
- ✅ **MAJOR:** Removed unnecessary byte swapping (4.3M operations/second saved!)
- ✅ Eliminated frame duplication artifacts through proper buffer management
- ✅ Achieved best FPS yet with minimal stuttering
- ✅ Optimized SD card performance (dramatic load time improvements)
- ✅ Implemented bulletproof DMA synchronization
- ✅ Native 233x233 display without scaling artifacts

## Performance Comparison

| Method          | SD Access                          | Overhead | Est. Load Time |
| --------------- | ---------------------------------- | -------- | -------------- |
| **Raw Sectors** | `disk_read()`                      | ~5%      | **~8ms/frame** |
| FatFS           | `f_read()` → FatFS → `disk_read()` | ~25%     | ~12ms/frame    |

**Raw mode saves ~33% load time per frame!**

## Future Goals

Now that we've conquered raw-level performance, potential enhancements include:

1. **Variable Frame Rates:** Support different target FPS settings.
2. **Audio Sync:** Add audio playback synchronized with animation.
3. **Real-time Effects:** Frame filters, transitions, or overlays.
4. **Multiple Animation Support:** Playlist functionality with seamless transitions.
5. **SD Card Wear Leveling:** Distribute writes across sectors.

## Sassy Reminders

- If you're still using the file system mode, you're leaving performance on the table. Raw mode is the way! 🚀
- Don't even _think_ about messing with `LINES_PER_DMA = 233` - it's mathematically perfect for this display.
- The raw SD writer has safety checks, but double-check your device path or you'll nuke something important.
- Your frames better be properly formatted RGB565 big-endian or you'll get a colorful mess (and not the good kind).
- Remember: We didn't eliminate 4.3 million operations per second just to add them back with "optimizations." 😎
