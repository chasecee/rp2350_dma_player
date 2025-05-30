cd /gif-converter
python3 -m venv .venv
source .venv/bin/activate
pip install pillow imageio imageio-ffmpeg

diskutil list | grep -B3 -A3 "PICOSD"
diskutil unmount /Volumes/PICOSD
sudo diskutil eraseDisk FAT32 PICOSD MBRFormat /dev/disk6
