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
- Reduce color palette (default 16 colors, customizable)
- Drop frames with stride (default 1, customizable)
- Optimize/compress them
- Save them to the output directory
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

def process_gif(input_path, output_dir, size=(466, 466)):
    reader = imageio.get_reader(input_path)
    base_name = os.path.splitext(os.path.basename(input_path))[0]
    generated_files = [] # List to store generated filenames
    
    frame_count = 0
    for i, frame_data in enumerate(reader):
        img = Image.fromarray(frame_data)
        img = img.convert('RGBA')
        
        # Crop and resize using the provided size
        img_resized = crop_and_resize(img, size) 
        
        # Handle transparency: composite onto a black background
        background = Image.new('RGBA', img_resized.size, (0, 0, 0, 255))
        img_composited = Image.alpha_composite(background, img_resized)
        img_final_rgb = img_composited.convert('RGB')

        # Prepare to write binary RGB565 data
        out_path = os.path.join(output_dir, f"{base_name}-{i}.bin")
        
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
        # Store the filename relative to the manifest file itself
        generated_files.append(f"{base_name}-{i}.bin")
        frame_count += 1
        
    reader.close()
    if frame_count == 0:
        print(f"No frames processed from {input_path}")
    return generated_files # Return the list of generated filenames

def main():
    parser = argparse.ArgumentParser(description="Convert GIFs to raw RGB565 binary frames (byte-swapped for CO5300) and generate a manifest.txt.")
    parser.add_argument('--source', type=str, required=True, help='Source directory containing GIFs')
    parser.add_argument('--output', type=str, required=True, help='Output directory for .bin frames')
    parser.add_argument('--size', nargs='+', type=int, default=[466], 
                        help='Target size W (for square WxW) or W H (default: 466 for 466x466)')
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
    all_frame_files = [] # List to store all frame filenames from all GIFs
    for fname in os.listdir(args.source):
        if fname.lower().endswith('.gif'):
            in_path = os.path.join(args.source, fname)
            # process_gif now returns a list of filenames
            gif_frame_files = process_gif(in_path, args.output, size=output_size)
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
