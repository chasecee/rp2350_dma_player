"""
GIF/MP4 Converter for LVGL Projects

Usage:
    python convert.py --source ./source --output ./output [--size 32] [--size 32 24] [--rotate {0,90,180,-90}]

Virtual Environment Setup:
    python3 -m venv .venv
    source .venv/bin/activate
    pip install pillow imageio imageio-ffmpeg

This script will:
- Take all GIFs and MP4s from the source directory
- Rotate frames if specified (0, 90, 180, or -90 degrees)
- Resize and center-crop them to the specified size (default 466x466, no squishing)
- Replace transparency with black (for GIFs)
- Convert to RGB565 format (16-bit color)
- Save frames as binary files to the output directory
- Generate a JPEG thumbnail of the first frame
"""

import os
import argparse
from PIL import Image
import imageio
import struct # Added for packing binary data

FILES_PER_SUBDIR = 500 # Max number of frames per subdirectory

def crop_and_resize(img, size=(20, 20)):
    # Resize to fill, then center-crop
    aspect = img.width / img.height
    target_aspect = size[0] / size[1]
    if aspect > target_aspect:
        # Wider than target: resize height, crop width
        new_height = size[1]
        new_width = int(aspect * new_height)
    else:
        # Taller than target: resize width, crop height
        new_width = size[0]
        new_height = int(new_width / aspect)
    img = img.resize((new_width, new_height), Image.LANCZOS)
    left = (new_width - size[0]) // 2
    top = (new_height - size[1]) // 2
    img = img.crop((left, top, left + size[0], top + size[1]))
    return img

def force_black_palette_index_0(img):
    palette = img.getpalette()
    # Check if black is already index 0
    if palette[0:3] == [0, 0, 0]:
        return img  # Already black
    # Find black in the palette
    try:
        black_index = next(i for i in range(256) if palette[i*3:i*3+3] == [0, 0, 0])
    except StopIteration:
        # No black in palette, add it at index 0
        palette[0:3] = [0, 0, 0]
        img.putpalette(palette)
        return img
    # Swap black to index 0
    palette[0:3], palette[black_index*3:black_index*3+3] = palette[black_index*3:black_index*3+3], palette[0:3]
    img.putpalette(palette)
    # Remap pixels: swap all 0s and black_index in the image data
    data = list(img.getdata())
    new_data = []
    for px in data:
        if px == 0:
            new_data.append(black_index)
        elif px == black_index:
            new_data.append(0)
        else:
            new_data.append(px)
    img.putdata(new_data)
    return img

def process_gif(input_path, base_output_dir, size=(466, 466), rotation=0, global_frame_idx_offset=0):
    reader = imageio.get_reader(input_path)
    base_name = os.path.splitext(os.path.basename(input_path))[0]
    generated_manifest_entries = [] # List to store generated filenames for the manifest
    
    frame_count_this_file = 0
    first_frame_img_for_master_thumb = None # Added to capture first frame image

    for i, frame_data in enumerate(reader):
        current_global_frame_idx = global_frame_idx_offset + i
        subdir_num = current_global_frame_idx // FILES_PER_SUBDIR
        subdir_name = f"{subdir_num:03d}" # e.g., "000", "001"
        
        actual_output_subdir = os.path.join(base_output_dir, subdir_name)
        os.makedirs(actual_output_subdir, exist_ok=True)
        
        img = Image.fromarray(frame_data)
        img = img.convert('RGBA')
        
        # Apply rotation if needed
        if rotation != 0:
            img = img.rotate(rotation, expand=True)
        
        # Crop and resize using the provided size
        img_resized = crop_and_resize(img, size) 
        
        # Handle transparency: composite onto a black background
        background = Image.new('RGBA', img_resized.size, (0, 0, 0, 255))
        img_composited = Image.alpha_composite(background, img_resized)
        img_final_rgb = img_composited.convert('RGB')

        # Capture the first frame for a potential master thumbnail
        if i == 0:
            first_frame_img_for_master_thumb = img_final_rgb.copy()

        # Prepare to write binary RGB565 data
        bin_filename = f"{base_name}-{i}.bin"
        out_path = os.path.join(actual_output_subdir, bin_filename)
        
        pixel_data_bin = bytearray()
        
        for y_coord in range(size[1]):
            for x_coord in range(size[0]):
                r, g, b = img_final_rgb.getpixel((x_coord, y_coord))
                
                # Convert RGB888 to RGB565 standard
                # R (5 bits), G (6 bits), B (5 bits)
                r5 = (r >> 3) & 0x1F
                g6 = (g >> 2) & 0x3F
                b5 = (b >> 3) & 0x1F
                rgb565_standard = (r5 << 11) | (g6 << 5) | b5
                
                # Byte swap for CO5300 expectation (e.g., 0xF800 (standard red) -> 0x00F8)
                # This creates the uint16_t value that will be in the C array
                rgb565_swapped_val = ((rgb565_standard & 0x00FF) << 8) | \
                                     ((rgb565_standard & 0xFF00) >> 8)
                
                # Pack as little-endian unsigned short (2 bytes)
                pixel_data_bin.extend(struct.pack('<H', rgb565_swapped_val))
                
        with open(out_path, 'wb') as f_bin:
            f_bin.write(pixel_data_bin)
            
        print(f"Saved frame {i} as RGB565 .bin: {out_path}")
        # Store the filename relative to the base_output_dir (for manifest)
        manifest_entry = os.path.join(subdir_name, bin_filename)
        generated_manifest_entries.append(manifest_entry.replace(os.path.sep, '/'))
        frame_count_this_file += 1
        
    reader.close()
    if frame_count_this_file == 0:
        print(f"No frames processed from {input_path}")
    return generated_manifest_entries, frame_count_this_file, first_frame_img_for_master_thumb

def main():
    parser = argparse.ArgumentParser(description="Convert GIFs and MP4s to raw RGB565 binary frames (byte-swapped for CO5300) and generate a manifest.txt.")
    parser.add_argument('--source', type=str, required=True, help='Source directory containing GIFs and MP4s')
    parser.add_argument('--output', type=str, required=True, help='Output directory for .bin frames')
    parser.add_argument('--size', nargs='+', type=int, default=[466], 
                        help='Target size W (for square WxW) or W H (default: 466 for 466x466)')
    parser.add_argument('--rotate', type=int, choices=[0, 90, 180, -90], default=0,
                        help='Rotation angle in degrees (default: 0)')
    args = parser.parse_args()

    # Parse size argument
    parsed_size_arg = args.size
    if len(parsed_size_arg) == 1:
        output_size = (parsed_size_arg[0], parsed_size_arg[0])
    elif len(parsed_size_arg) >= 2:
        output_size = (parsed_size_arg[0], parsed_size_arg[1])
    else: # Should be caught by nargs='+' or default=[466]
        # This case should ideally not be reached due to argparse config
        print("Warning: Could not parse size argument, defaulting to 466x466.")
        output_size = (466, 466)

    os.makedirs(args.output, exist_ok=True)
    all_manifest_entries = [] # List to store all frame filenames from all GIFs/MP4s
    master_frame_counter = 0 # Keep track of total frames processed for subdirectory naming
    master_thumbnail_saved = False # Flag for the new master thumbnail
    script_dir = os.path.dirname(os.path.abspath(__file__)) # Get script's directory

    for fname in os.listdir(args.source):
        if fname.lower().endswith(('.gif', '.mp4')):
            in_path = os.path.join(args.source, fname)
            print(f"Processing {'video' if fname.lower().endswith('.mp4') else 'GIF'}: {fname}")
            
            manifest_entries, num_frames_processed, first_frame_image = process_gif(
                in_path, 
                args.output, 
                size=output_size, 
                rotation=args.rotate,
                global_frame_idx_offset=master_frame_counter
            )
            all_manifest_entries.extend(manifest_entries)
            master_frame_counter += num_frames_processed

            if first_frame_image and not master_thumbnail_saved:
                try:
                    master_thumb_path = os.path.join(script_dir, "master_thumbnail.jpg")
                    # first_frame_image is already at output_size due to crop_and_resize
                    first_frame_image.save(master_thumb_path, "JPEG", quality=85) # Removed .resize()
                    print(f"Saved master thumbnail: {master_thumb_path}")
                    master_thumbnail_saved = True
                except Exception as e:
                    print(f"Error saving master thumbnail: {e}")

    # After processing all GIFs/MP4s, write the manifest file
    if all_manifest_entries:
        manifest_path = os.path.join(args.output, 'manifest.txt')
        with open(manifest_path, 'w') as mf:
            for frame_file_rel_path in all_manifest_entries:
                # Ensure path separator is '/' and correct newline (already handled in process_gif)
                mf.write(frame_file_rel_path + '\n')
        print(f"Generated manifest.txt at {manifest_path} with {len(all_manifest_entries)} entries.")
    else:
        print("No frames were processed, so no manifest.txt was generated.")

    # --- Add this section to write the run command ---
    try:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        run_command_file_path = os.path.join(script_dir, "run-command.txt")
        command_to_write = "python convert.py --source ./source --output ./output --size 156 --rotate -90"
        with open(run_command_file_path, 'w') as rcf:
            rcf.write(command_to_write + '\n')
        print(f"Updated run command in: {run_command_file_path}")
    except Exception as e:
        print(f"Error writing run-command.txt: {e}")
    # --- End of section ---

if __name__ == '__main__':
    main()
