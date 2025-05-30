"""
GIF/MP4 Converter for LVGL Projects

Usage:
    python convert.py --source ./source --output ./output [--size 156] [--size 156 156] [--rotate {0,90,180,-90}]

Virtual Environment Setup:
    python3 -m venv .venv
    source .venv/bin/activate
    pip install pillow imageio imageio-ffmpeg

This script will:
- Take all GIFs and MP4s from the source directory
- Rotate frames if specified (0, 90, 180, or -90 degrees)
- Resize and center-crop them to the specified size (default 156x156, no squishing)
- Replace transparency with black (for GIFs)
- Convert to 8-bit RGB332 format
- Save frames as binary files directly to the output directory (no subdirectories for better SD card performance)
- Generate a JPEG thumbnail of the first frame
- Create a manifest.txt with all frame filenames
"""

import os
import argparse
from PIL import Image
import imageio

# FILES_PER_SUBDIR = 250 # Max number of frames per subdirectory - REMOVED, no subdirs anymore
# Ensure Image.FASTOCTREE is available, or use its integer value (2) if needed.
# from PIL import Image # Image.FASTOCTREE should be available directly

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

def process_media_file(input_path, base_output_dir, size=(466, 466), rotation=0, global_frame_idx_offset=0, frame_stride=1):
    reader = imageio.get_reader(input_path)
    base_name = os.path.splitext(os.path.basename(input_path))[0]
    generated_manifest_entries = []
    processed_frames_in_this_file_count = 0
    first_frame_img_for_master_thumb = None
    
    for original_frame_idx, frame_data in enumerate(reader):
        if original_frame_idx % frame_stride != 0:
            continue # Skip this frame

        # Calculate global index based on processed frames
        current_global_frame_idx = global_frame_idx_offset + processed_frames_in_this_file_count
        
        # No more subdirectories - put directly in output dir
        actual_output_dir = base_output_dir
        
        img = Image.fromarray(frame_data)
        img = img.convert('RGBA')
        
        if rotation != 0:
            img = img.rotate(rotation, expand=True)
        
        img_resized = crop_and_resize(img, size)
        
        background = Image.new('RGBA', img_resized.size, (0, 0, 0, 255))
        img_composited = Image.alpha_composite(background, img_resized)
        img_final_rgb888 = img_composited.convert('RGB')

        if processed_frames_in_this_file_count == 0 and first_frame_img_for_master_thumb is None and global_frame_idx_offset == 0:
            first_frame_img_for_master_thumb = img_final_rgb888.copy()

        # Use global frame index in filename for better ordering
        bin_filename = f"frame-{current_global_frame_idx:05d}.bin"
        out_path = os.path.join(base_output_dir, bin_filename)
        
        pixels_rgb332 = []
        for r, g, b in img_final_rgb888.getdata():
            r_3bit = (r >> 5) & 0x07 # Max 111 (7)
            g_3bit = (g >> 5) & 0x07 # Max 111 (7)
            b_2bit = (b >> 6) & 0x03 # Max 11 (3)
            rgb332_byte = (r_3bit << 5) | (g_3bit << 2) | b_2bit
            pixels_rgb332.append(rgb332_byte)
        
        pixel_data_bytes = bytes(pixels_rgb332)
                
        with open(out_path, 'wb') as f_bin:
            f_bin.write(pixel_data_bytes)
            
        print(f"Saved 8-bit RGB332 frame (original index {original_frame_idx}) as .bin: {out_path} (Global Processed Index: {current_global_frame_idx})")
        manifest_entry = bin_filename
        generated_manifest_entries.append(manifest_entry)
        processed_frames_in_this_file_count += 1
        
    reader.close()
    if processed_frames_in_this_file_count == 0:
        print(f"No frames processed from {input_path} with stride {frame_stride}")
    return generated_manifest_entries, processed_frames_in_this_file_count, first_frame_img_for_master_thumb

def main():
    parser = argparse.ArgumentParser(description="Convert GIFs and MP4s to 8-bit direct RGB332 binary frames and generate a manifest.txt.")
    parser.add_argument('--source', type=str, required=True, help='Source directory containing GIFs and MP4s')
    parser.add_argument('--output', type=str, required=True, help='Output directory for .bin frames')
    parser.add_argument('--size', nargs='+', type=int, default=[156], 
                        help='Target size W (for square WxW) or W H (default: 156 for 156x156)')
    parser.add_argument('--rotate', type=int, choices=[0, 90, 180, -90], default=0,
                        help='Rotation angle in degrees (default: 0)')
    parser.add_argument('--frame_stride', type=int, default=1, help='Process one frame every N frames (default: 1, process all). Value > 0.')
    args = parser.parse_args()

    if args.frame_stride <= 0:
        print("Error: --frame_stride must be a positive integer.")
        return

    # Parse size argument
    parsed_size_arg = args.size
    if len(parsed_size_arg) == 1:
        output_size = (parsed_size_arg[0], parsed_size_arg[0])
    elif len(parsed_size_arg) >= 2:
        output_size = (parsed_size_arg[0], parsed_size_arg[1])
    else: # Should be caught by nargs='+' or default=[466]
        # This case should ideally not be reached due to argparse config
        print("Warning: Could not parse size argument, defaulting to 156x156.")
        output_size = (156, 156)

    os.makedirs(args.output, exist_ok=True)
    
    # Pre-create all files with full size to reduce fragmentation
    print("Pre-allocating all output files to reduce fragmentation...")
    total_expected_files = 4000  # User says at least 3.5k frames
    
    if total_expected_files > 0:
        try:
            empty_frame = bytes([0] * (output_size[0] * output_size[1]))  # All black frame
            print(f"Pre-allocating {total_expected_files} files (this may take a moment)...")
            for i in range(0, total_expected_files, 100):  # Progress indicator every 100 files
                for j in range(min(100, total_expected_files - i)):
                    temp_path = os.path.join(args.output, f"frame-{i+j:05d}.bin")
                    with open(temp_path, 'wb') as f:
                        f.write(empty_frame)
                print(f"  Pre-allocated {i+100} files...", end='\r')
            print(f"\nPre-allocated {total_expected_files} files")
            
            # Now delete them - they served their purpose of allocating contiguous space
            print("Cleaning up pre-allocated files...")
            for i in range(total_expected_files):
                temp_path = os.path.join(args.output, f"frame-{i:05d}.bin")
                try:
                    os.remove(temp_path)
                except:
                    pass
            print("Cleaned up pre-allocated files")
        except Exception as e:
            print(f"Warning: Pre-allocation failed: {e}")
    
    all_manifest_entries = [] # List to store all frame filenames from all GIFs/MP4s
    master_frame_counter = 0 # Keep track of total frames processed for global naming
    master_thumbnail_saved = False # Flag for the new master thumbnail
    first_file_processed = True # To ensure master thumbnail is from the very first processed frame of the first file
    script_dir = os.path.dirname(os.path.abspath(__file__)) # Get script's directory
    
    for fname in os.listdir(args.source):
        if fname.lower().endswith(('.gif', '.mp4')):
            in_path = os.path.join(args.source, fname)
            print(f"Processing {'video' if fname.lower().endswith('.mp4') else 'GIF'}: {fname} with stride {args.frame_stride}")
            
            manifest_entries, num_frames_processed, first_frame_image = process_media_file(
                in_path, 
                args.output, 
                size=output_size, 
                rotation=args.rotate,
                global_frame_idx_offset=master_frame_counter,
                frame_stride=args.frame_stride
            )
            
            all_manifest_entries.extend(manifest_entries)
            master_frame_counter += num_frames_processed

            if first_frame_image and first_file_processed and num_frames_processed > 0: # Ensure thumbnail is from the actual first *processed* frame
                try:
                    # Save thumbnail in the script's directory, not output directory
                    master_thumb_path = os.path.join(script_dir, "master_thumbnail.jpg")
                    first_frame_image.save(master_thumb_path, "JPEG", quality=85)
                    print(f"Saved master thumbnail: {master_thumb_path}")
                    master_thumbnail_saved = True
                    first_file_processed = False # Only save one master thumbnail
                except Exception as e:
                    print(f"Error saving master thumbnail: {e}")
            elif num_frames_processed > 0: # If frames were processed from this file but it wasn't the first, ensure first_file_processed is false
                first_file_processed = False

    # After processing all GIFs/MP4s, write the manifest file
    if all_manifest_entries:
        manifest_path = os.path.join(args.output, 'manifest.txt')
        with open(manifest_path, 'w') as mf:
            for frame_file_rel_path in all_manifest_entries:
                mf.write(frame_file_rel_path + '\n')
        print(f"Generated manifest.txt at {manifest_path} with {len(all_manifest_entries)} entries.")
    else:
        print("No frames were processed, so no manifest.txt was generated.")

    # --- Add this section to write the run command ---
    try:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        run_command_file_path = os.path.join(script_dir, "run-command.txt")
        command_to_write = f"python convert.py --source ./source --output ./output --size {output_size[0]} {output_size[1]} --rotate {args.rotate} --frame_stride {args.frame_stride}"
        with open(run_command_file_path, 'w') as rcf:
            rcf.write(command_to_write + '\n')
        print(f"Updated run command in: {run_command_file_path}")
    except Exception as e:
        print(f"Error writing run-command.txt: {e}")
    # --- End of section ---

if __name__ == '__main__':
    main()
