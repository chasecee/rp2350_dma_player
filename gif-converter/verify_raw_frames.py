#!/usr/bin/env python3
"""
Raw SD Card Frame Verifier

This script reads back frames from raw SD card sectors and compares
them with the original frames.bin file to verify the write was successful.

Usage:
    sudo python3 verify_raw_frames.py /dev/rdisk6 frames.bin

Requirements:
    - Root access (sudo)
    - Original frames.bin file
    - SD card with raw frames written
"""

import os
import sys
import stat
import argparse
import subprocess
import re

# SD card parameters (must match write_raw_frames.py)
RAW_START_SECTOR = 2048
BYTES_PER_SECTOR = 512
FRAME_SIZE_BYTES = 233 * 233 * 2  # 233x233 RGB565 (raw frame size)
# Use sector-aligned frame size for optimal performance (must match convert.py and raw_sd_loader.c)
PADDED_FRAME_SIZE_BYTES = ((FRAME_SIZE_BYTES + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR) * BYTES_PER_SECTOR

def get_device_info_macos(device_path):
    """Get device size using macOS diskutil command"""
    try:
        disk_match = re.search(r'r?disk(\d+)', device_path)
        if not disk_match:
            raise ValueError(f"Cannot extract disk identifier from {device_path}")
        
        disk_id = f"disk{disk_match.group(1)}"
        result = subprocess.run(['diskutil', 'info', disk_id], 
                              capture_output=True, text=True)
        if result.returncode != 0:
            raise ValueError(f"diskutil failed: {result.stderr}")
        
        for line in result.stdout.split('\n'):
            if 'Disk Size:' in line:
                bytes_match = re.search(r'\((\d+) Bytes\)', line)
                if bytes_match:
                    return int(bytes_match.group(1))
        
        raise ValueError("Could not find disk size in diskutil output")
        
    except Exception as e:
        raise ValueError(f"Error getting device size: {e}")

def verify_raw_frames(device_path, frames_path):
    """Verify frames on raw SD card match the original file"""
    
    # Check original file
    if not os.path.exists(frames_path):
        raise FileNotFoundError(f"Original frames file {frames_path} does not exist")
    
    frames_size = os.path.getsize(frames_path)
    num_frames = frames_size // PADDED_FRAME_SIZE_BYTES
    print(f"Original file: {num_frames} frames ({frames_size:,} bytes)")
    print(f"Frame size: {FRAME_SIZE_BYTES} bytes raw, {PADDED_FRAME_SIZE_BYTES} bytes padded")
    print(f"Padding per frame: {PADDED_FRAME_SIZE_BYTES - FRAME_SIZE_BYTES} bytes")
    
    if frames_size % PADDED_FRAME_SIZE_BYTES != 0:
        print(f"WARNING: File size {frames_size} is not evenly divisible by padded frame size {PADDED_FRAME_SIZE_BYTES}")
    
    # Check device
    if not os.path.exists(device_path):
        raise FileNotFoundError(f"Device {device_path} does not exist")
    
    device_size = get_device_info_macos(device_path)
    start_byte_offset = RAW_START_SECTOR * BYTES_PER_SECTOR
    
    print(f"Device: {device_path} ({device_size:,} bytes)")
    print(f"Reading from sector {RAW_START_SECTOR} (offset {start_byte_offset:,})")
    
    # Compare data in chunks
    chunk_size = 1024 * 1024  # 1MB chunks
    bytes_verified = 0
    mismatches = 0
    
    print(f"Verifying {frames_size:,} bytes...")
    
    try:
        with open(device_path, 'rb') as device_file:
            with open(frames_path, 'rb') as frames_file:
                # Seek to start position on device
                device_file.seek(start_byte_offset)
                
                while bytes_verified < frames_size:
                    # Read chunk from both files
                    remaining = frames_size - bytes_verified
                    read_size = min(chunk_size, remaining)
                    
                    device_chunk = device_file.read(read_size)
                    frames_chunk = frames_file.read(read_size)
                    
                    if len(device_chunk) != len(frames_chunk):
                        print(f"ERROR: Read size mismatch at offset {bytes_verified}")
                        return False
                    
                    # Compare chunks
                    if device_chunk != frames_chunk:
                        # Find first mismatch for detailed error
                        for i, (d, f) in enumerate(zip(device_chunk, frames_chunk)):
                            if d != f:
                                offset = bytes_verified + i
                                print(f"MISMATCH at byte {offset}: device=0x{d:02x}, file=0x{f:02x}")
                                mismatches += 1
                                if mismatches >= 10:  # Limit error spam
                                    print("Too many mismatches, stopping verification.")
                                    return False
                                break
                    
                    bytes_verified += len(device_chunk)
                    progress = (bytes_verified / frames_size) * 100
                    print(f"Verified: {progress:.1f}% ({bytes_verified:,}/{frames_size:,} bytes)", end='\r')
                
        if mismatches == 0:
            print(f"\n✅ SUCCESS: All {bytes_verified:,} bytes verified successfully!")
            print(f"✅ Raw frames write was successful!")
            print(f"✅ {num_frames} frames are ready for playback!")
            return True
        else:
            print(f"\n❌ FAILED: Found {mismatches} mismatches")
            return False
            
    except PermissionError:
        print("ERROR: Permission denied. Make sure to run with sudo.")
        return False
    except Exception as e:
        print(f"ERROR: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description="Verify frames written to raw SD card sectors")
    parser.add_argument("device", help="SD card device path (e.g., /dev/rdisk6)")
    parser.add_argument("frames_file", help="Original frames.bin file to compare against")
    
    args = parser.parse_args()
    
    try:
        success = verify_raw_frames(args.device, args.frames_file)
        return 0 if success else 1
    except Exception as e:
        print(f"ERROR: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main()) 