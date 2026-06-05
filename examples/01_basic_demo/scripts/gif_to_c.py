"""
Convert GIF animation to LVGL animated image C source.
- Resizes frames to a smaller size
- Converts to RGB565 byte arrays
- Generates lv_image_dsc_t array for use with lv_animimg
"""

import struct
import os
import sys
from PIL import Image

# Configuration
GIF_PATH = os.path.join(os.path.dirname(__file__), '..', 'tool', 'dance_1.gif')
OUTPUT_HEADER = os.path.join(os.path.dirname(__file__), '..', 'main', 'gif_frames.h')
OUTPUT_SOURCE = os.path.join(os.path.dirname(__file__), '..', 'main', 'gif_frames.c')

TARGET_W = 135   # Display width (will be resized)
TARGET_H = 135   # Display height
FRAME_STEP = 3   # Include every N frames (1 = all)
DISPLAY_MS = 60  # Milliseconds per frame

def rgb888_to_rgb565(r, g, b):
    """Convert 888 RGB to 565 RGB."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)

def image_to_rgb565_bytes(img):
    """Convert a PIL Image to RGB565 bytes (little-endian)."""
    if img.mode != 'RGB':
        img = img.convert('RGB')
    pixels = []
    for y in range(img.height):
        for x in range(img.width):
            r, g, b = img.getpixel((x, y))
            rgb565 = rgb888_to_rgb565(r, g, b)
            # Store as little-endian (LVGL expects this on ESP32)
            pixels.append(rgb565 & 0xFF)       # low byte
            pixels.append((rgb565 >> 8) & 0xFF) # high byte
    return bytes(pixels)

def main():
    if not os.path.exists(GIF_PATH):
        print(f"ERROR: GIF not found at {GIF_PATH}")
        sys.exit(1)

    gif = Image.open(GIF_PATH)
    n_frames = getattr(gif, 'n_frames', 1)

    print(f"GIF: {gif.size}, {n_frames} frames")

    # Extract resized frames
    frames = []
    durations = []
    for i in range(0, n_frames, FRAME_STEP):
        gif.seek(i)
        # Get frame duration and scale by FRAME_STEP
        # (since we skip frames, each kept frame represents FRAME_STEP originals)
        try:
            dur = gif.info.get('duration', DISPLAY_MS) * FRAME_STEP
        except:
            dur = DISPLAY_MS * FRAME_STEP
        if dur < 20:
            dur = DISPLAY_MS * FRAME_STEP
        durations.append(dur)

        # Convert to RGB and resize
        frame = gif.convert('RGB')
        frame = frame.resize((TARGET_W, TARGET_H), Image.LANCZOS)
        data = image_to_rgb565_bytes(frame)
        frames.append(data)
        print(f"  Frame {i}: {len(data)} bytes, duration={dur}ms")

    n_out = len(frames)
    frame_size = TARGET_W * TARGET_H * 2  # RGB565 = 2 bytes/pixel

    print(f"\nTotal frames: {n_out}")
    print(f"Frame size: {TARGET_W}x{TARGET_H} = {frame_size} bytes")
    print(f"Total data: {n_out * frame_size} bytes ({n_out * frame_size / 1024:.1f} KB)")

    # Generate source file
    with open(OUTPUT_SOURCE, 'w') as f:
        f.write('/* Auto-generated from dance_1.gif - DO NOT EDIT */\n')
        f.write('#include "gif_frames.h"\n\n')

        # Frame data arrays
        for i, data in enumerate(frames):
            f.write(f'static const uint8_t frame_{i}_data[{frame_size}] = {{\n')
            # Write as hex bytes, 16 per line
            for row in range(0, len(data), 16):
                chunk = data[row:row+16]
                hex_str = ', '.join(f'0x{b:02X}' for b in chunk)
                f.write(f'  {hex_str},\n')
            f.write('};\n\n')

        # Image descriptors
        f.write('/* LVGL image descriptors for each frame */\n')
        for i in range(n_out):
            f.write(f'static const lv_image_dsc_t img_dsc_{i} = {{\n')
            f.write(f'  .header = {{\n')
            f.write(f'    .magic = LV_IMAGE_HEADER_MAGIC,\n')
            f.write(f'    .cf = LV_COLOR_FORMAT_RGB565,\n')
            f.write(f'    .flags = 0,\n')
            f.write(f'    .w = {TARGET_W},\n')
            f.write(f'    .h = {TARGET_H},\n')
            f.write(f'    .stride = {TARGET_W * 2},\n')
            f.write(f'  }},\n')
            f.write(f'  .data = frame_{i}_data,\n')
            f.write(f'  .data_size = sizeof(frame_{i}_data),\n')
            f.write('};\n\n')

        # Array of image pointers and durations
        f.write(f'/* Frame count */\n')
        f.write(f'const int gif_frame_count = {n_out};\n\n')

        f.write(f'/* Array of image descriptor pointers */\n')
        f.write('const lv_image_dsc_t *gif_frames[] = {\n')
        for i in range(n_out):
            f.write(f'  &img_dsc_{i},\n')
        f.write('};\n\n')

        f.write(f'/* Frame durations in milliseconds */\n')
        f.write(f'const uint16_t gif_frame_durations[{n_out}] = {{\n')
        for i, d in enumerate(durations):
            f.write(f'  {d}{"," if i < n_out-1 else ""}\n')
        f.write('};\n\n')

    print(f"\nGenerated: {OUTPUT_SOURCE}")

    # Generate header file
    with open(OUTPUT_HEADER, 'w') as f:
        f.write('/* Auto-generated from dance_1.gif - DO NOT EDIT */\n')
        f.write('#pragma once\n')
        f.write('#include "lvgl.h"\n\n')
        f.write(f'#define GIF_FRAME_W   {TARGET_W}\n')
        f.write(f'#define GIF_FRAME_H   {TARGET_H}\n')
        f.write(f'#define GIF_FRAME_MS  {DISPLAY_MS}\n\n')
        f.write(f'extern const int gif_frame_count;\n')
        f.write(f'extern const lv_image_dsc_t *gif_frames[];\n')
        f.write(f'extern const uint16_t gif_frame_durations[];\n\n')

    print(f"Generated: {OUTPUT_HEADER}")
    print("Done!")

if __name__ == '__main__':
    main()
