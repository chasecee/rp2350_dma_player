cd gif-converter
python3 -m venv .venv
source .venv/bin/activate
pip install pillow imageio imageio-ffmpeg

diskutil list | grep -B3 -A3 "PICOSD"
diskutil unmount /Volumes/PICOSD
sudo diskutil eraseDisk FAT32 FRAMES MBRFormat /dev/disk6

# SD Card Formatting for Direct Offset-Seeking (macOS)

1. Find your SD card disk number:

   ```sh
   diskutil list | grep -B3 -A3 "PICOSD"
   ```

   (Look for something like `/dev/disk6` and make sure it's your SD card!)

2. Unmount the SD card volume (replace `PICOSD` with your volume name if different):

   ```sh
   diskutil unmount /Volumes/PICOSD
   ```

3. Erase and partition the SD card as FAT32 (this will nuke everything on it!):

   ```sh
   sudo diskutil eraseDisk FAT32 FRAMES MBRFormat /dev/disk6
   ```

   - `FRAMES` is the new volume label. Change it if you want to be extra.

4. Unmount the new partition (macOS loves to auto-mount it):

   ```sh
   diskutil unmount /dev/disk6s1
   ```

5. (Optional but recommended) Reformat with a big cluster size for fast sequential access:

   ```sh
   sudo newfs_msdos -F 32 -c 64 -v FRAMES /dev/disk6s1
   ```

   - `-c 64` gives you 32KB clusters. Perfect for frame streaming.

6. Remount the SD card (or just unplug/replug if you want macOS to do it for you).
   ```sh
   diskutil mount /dev/disk6s1
   ls /Volumes
   ```

# Usage: Convert and Copy

To output all frames into a single file for direct offset-seeking:

```sh
python convert.py --source ./source --output /Volumes/FRAMES --size 156 --rotate -90 --frame_stride 2 --single-bin
```

- This will create `/Volumes/FRAMES/frames.bin` and a `manifest.txt` for debugging.

If you want the old behavior (individual .bin files), just leave off `--single-bin`.

# Straight to sd card

python convert.py --source ./source --output /Volumes/PICOSD --size 156 --rotate -90 --frame_stride 2
