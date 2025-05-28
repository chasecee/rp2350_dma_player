"""
GIF Converter for LVGL Projects

Usage:
    python convert.py --source ./source --output ./output [--size 32] [--size 32 24] [--colors 16] [--frame-stride 2]

Virtual Environment Setup:
    python3 -m venv .venv
    source .venv/bin/activate
    pip install pillow imageio

This script will:
- Take all GIFs from the source directory
- Resize and center-crop them to the specified size (default 20x20, no squishing)
- Replace transparency with black
- Reduce color palette (default 16 colors, customizable) - Note: Color reduction might be less effective before RGB332 conversion.
- Drop frames with stride (default 1, customizable)
- Optimize/compress them
- Save them to the output directory as RGB332 .bin files
"""

import os
import argparse
from PIL import Image
import imageio
import struct # Added for packing binary data

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

def process_media_file(input_path, output_dir, rgb332_palette_img, size=(466, 466), rotation=None, max_frames=None):
    reader = imageio.get_reader(input_path)
    base_name = os.path.splitext(os.path.basename(input_path))[0]
    generated_files = [] # List to store generated filenames
    
    frame_count = 0
    for i, frame_data in enumerate(reader):
        if max_frames is not None and i >= max_frames: # Check frame limit
            print(f"Reached max_frames limit ({max_frames}), stopping processing for {input_path}.")
            break

        img = Image.fromarray(frame_data)
        img = img.convert('RGBA')
        
        # Apply rotation if specified
        if rotation:
            img = img.rotate(rotation, expand=True) # expand=True to avoid cropping during rotation

        # Crop and resize using the provided size
        img_resized = crop_and_resize(img, size) 
        
        # Handle transparency: composite onto a black background
        background = Image.new('RGBA', img_resized.size, (0, 0, 0, 255))
        img_composited = Image.alpha_composite(background, img_resized)
        img_final_rgb = img_composited.convert('RGB')

        # Quantize to RGB332 palette with dithering
        img_quantized = img_final_rgb.quantize(palette=rgb332_palette_img, dither=Image.FLOYDSTEINBERG)

        # Save the first frame as a JPEG thumbnail (using the dithered image for preview)
        if i == 0:
            thumbnail_path = os.path.join(output_dir, f"{base_name}-thumbnail.jpg")
            # For JPEG, convert back to RGB as JPEG doesn't support palette directly
            img_quantized.convert('RGB').save(thumbnail_path, "JPEG") 
            print(f"Saved thumbnail: {thumbnail_path}")

        # Prepare to write binary RGB332 data
        out_path = os.path.join(output_dir, f"{base_name}-{i}.bin")
        
        # Get the pixel data (already RGB332 indices)
        pixel_data_bin = bytes(img_quantized.getdata())
                
        with open(out_path, 'wb') as f_bin:
            f_bin.write(pixel_data_bin)
            
        print(f"Saved frame {i} as RGB332 .bin: {out_path}")
        # Store the filename relative to the manifest file itself
        generated_files.append(f"{base_name}-{i}.bin")
        frame_count += 1
        
    reader.close()
    if frame_count == 0:
        print(f"No frames processed from {input_path}")
    return generated_files # Return the list of generated filenames

def main():
    parser = argparse.ArgumentParser(description="Convert GIFs to raw RGB332 binary frames and generate a manifest.txt.")
    parser.add_argument('--source', type=str, required=True, help='Source directory containing GIFs')
    parser.add_argument('--output', type=str, required=True, help='Output directory for .bin frames')
    parser.add_argument('--size', nargs='+', type=int, default=[466], 
                        help='Target size W (for square WxW) or W H (default: 466 for 466x466)')
    parser.add_argument('--rotate', type=int, choices=[90, 180, -90], help='Rotate frames by 90, 180, or -90 degrees.')
    parser.add_argument('--max_frames', type=int, help='Maximum number of frames to process from each file.')
    args = parser.parse_args()

    # Create RGB332 palette image
    rgb332_palette_data = []
    for i in range(256):
        r3 = (i >> 5) & 0x07
        g3 = (i >> 2) & 0x07
        b2 = i & 0x03
        r8 = r3 << 5
        g8 = g3 << 5
        b8 = b2 << 6
        rgb332_palette_data.extend([r8, g8, b8])
    
    # Pad palette to 256*3 = 768 entries if it's shorter (PIL requirement)
    # This shouldn't be necessary as we iterate 0-255, creating 256*3 entries.
    # if len(rgb332_palette_data) < 256 * 3:
    #     rgb332_palette_data.extend([0,0,0] * (256 - len(rgb332_palette_data) // 3))

    global_rgb332_palette_img = Image.new('P', (1, 1))
    global_rgb332_palette_img.putpalette(rgb332_palette_data)

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
    all_frame_files = [] # List to store all frame filenames from all GIFs
    for fname in os.listdir(args.source):
        if fname.lower().endswith(('.gif', '.mp4')):
            in_path = os.path.join(args.source, fname)
            gif_frame_files = process_media_file(in_path, args.output, global_rgb332_palette_img, size=output_size, rotation=args.rotate, max_frames=args.max_frames)
            all_frame_files.extend(gif_frame_files)

    # After processing all GIFs, write the manifest file
    if all_frame_files:
        manifest_path = os.path.join(args.output, 'manifest.txt')
        with open(manifest_path, 'w') as mf:
            for frame_file_rel_path in all_frame_files:
                # Ensure path separator is '/' and correct newline
                mf.write(frame_file_rel_path.replace(os.path.sep, '/') + '\n')
        print(f"Generated manifest.txt at {manifest_path} with {len(all_frame_files)} entries.")
    else:
        print("No frames were processed, so no manifest.txt was generated.")

if __name__ == '__main__':
    main()
