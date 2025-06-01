# GIF to Raw SD Card Animation Converter

This toolchain converts GIFs/MP4s to sector-aligned binary frames and writes them directly to raw SD card sectors for maximum performance, completely bypassing the filesystem.

## Setup

```bash
cd gif-converter
python3 -m venv .venv
source .venv/bin/activate
pip install pillow imageio imageio-ffmpeg
```

## SD Card Preparation

1. **Find your SD card:**

   ```bash
   diskutil list | grep -B3 -A3 "disk"
   ```

   Look for your SD card (e.g., `/dev/disk6`)

2. **Unmount if mounted:**

   ```bash
   diskutil unmount /dev/disk6s1
   ```

3. **Note:** No filesystem formatting needed! We write directly to raw sectors.

## Complete Workflow

### Step 1: Convert to Sector-Aligned Frames

```bash
python convert.py --source ./source --output ./output --size 233 233 --rotate -90 --frame_stride 2 --single-bin --colors 16
```

**Parameters:**

- `--size 233 233`: Native frame resolution (will be scaled 2x to 466x466)
- `--rotate -90`: Rotate frames (0, 90, 180, -90)
- `--frame_stride 2`: Use every 2nd frame (reduces file size)
- `--single-bin`: Output one `frames.bin` file with sector padding
- `--colors 16`: 16-bit RGB565 format

This creates:

- `./output/frames.bin` - Sector-aligned binary frames
- `./output/manifest.txt` - Frame offset information
- `master_thumbnail.jpg` - Preview of first frame

### Step 2: Write to Raw SD Card Sectors

```bash
sudo python3 write_raw_frames.py /dev/rdisk6 ./output/frames.bin
```

**Important:**

- Use `/dev/rdisk6` (not `/dev/disk6`) for faster raw access
- Writes starting at sector 2048 (after partition table)
- Each frame is padded to 213 sectors (109,056 bytes) for optimal performance

### Step 3: Verify the Write

```bash
sudo python3 verify_raw_frames.py /dev/rdisk6 ./output/frames.bin
```

This reads back the data and verifies byte-for-byte accuracy.

### Step 4: Update Firmware Frame Count

Update `src/main.c` with the actual frame count:

```c
num_frames = 3403; // Use the number reported by convert.py
```

### Step 5: Test on Device

Insert SD card into your RP2350 device and test!

## Performance Benefits

✅ **Sector-aligned frames** - No partial sector reads  
✅ **Raw device access** - Bypasses filesystem overhead  
✅ **Optimal clustering** - 213 sectors per frame (clean sector math)  
✅ **DMA-friendly** - Large contiguous reads  
✅ **Memory efficient** - 233x233 frames scale to 466x466 display

## Technical Details

- **Frame data starts:** Sector 2048 (1MB offset)
- **Raw frame size:** 108,578 bytes (233×233×2)
- **Padded frame size:** 109,056 bytes (213 sectors)
- **Padding per frame:** 478 bytes
- **SD read size:** 128 sectors per chunk (64KB)

## Troubleshooting

- **Permission denied:** Make sure to use `sudo` for raw device access
- **Device not found:** Check `diskutil list` for correct device path
- **Verification failed:** Re-run the write process, SD card may be faulty
- **Display artifacts:** Ensure frame count in firmware matches convert.py output
