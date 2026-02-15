#!/usr/bin/env python3
"""
Generate procedural BMP textures for gun models.
Creates a 256x256 24-bit BMP for each gun with unique color/pattern.

Usage: python gen_textures.py <output_directory>
"""

import struct
import os
import sys
import math
import random

def write_bmp(filepath, width, height, pixels):
    """Write a 24-bit BMP file. pixels is a list of (r, g, b) tuples, row-major top-to-bottom."""
    row_size = width * 3
    padding = (4 - (row_size % 4)) % 4
    padded_row = row_size + padding
    pixel_data_size = padded_row * height
    file_size = 54 + pixel_data_size

    with open(filepath, 'wb') as f:
        # File header (14 bytes)
        f.write(b'BM')
        f.write(struct.pack('<I', file_size))
        f.write(struct.pack('<HH', 0, 0))
        f.write(struct.pack('<I', 54))

        # Info header (40 bytes)
        f.write(struct.pack('<I', 40))
        f.write(struct.pack('<i', width))
        f.write(struct.pack('<i', height))
        f.write(struct.pack('<HH', 1, 24))
        f.write(struct.pack('<I', 0))  # BI_RGB
        f.write(struct.pack('<I', pixel_data_size))
        f.write(struct.pack('<i', 2835))  # ~72 DPI
        f.write(struct.pack('<i', 2835))
        f.write(struct.pack('<I', 0))
        f.write(struct.pack('<I', 0))

        # Pixel data (bottom-to-top, BGR)
        for y in range(height - 1, -1, -1):
            for x in range(width):
                r, g, b = pixels[y * width + x]
                f.write(struct.pack('BBB', b, g, r))
            f.write(b'\x00' * padding)


def clamp(v, lo=0, hi=255):
    return max(lo, min(hi, int(v)))


def hash_noise(x, y, seed=0):
    """Simple hash-based noise, returns 0.0-1.0"""
    n = x * 374761393 + y * 668265263 + seed * 1274126177
    n = (n ^ (n >> 13)) * 1103515245
    n = n ^ (n >> 16)
    return (n & 0x7FFFFFFF) / 0x7FFFFFFF


def lerp(a, b, t):
    return a + (b - a) * t


def smooth_noise(x, y, seed=0, scale=32):
    """Smooth noise using bilinear interpolation of hash noise."""
    sx = x / scale
    sy = y / scale
    ix, iy = int(sx), int(sy)
    fx, fy = sx - ix, sy - iy
    # Smoothstep
    fx = fx * fx * (3 - 2 * fx)
    fy = fy * fy * (3 - 2 * fy)

    v00 = hash_noise(ix, iy, seed)
    v10 = hash_noise(ix + 1, iy, seed)
    v01 = hash_noise(ix, iy + 1, seed)
    v11 = hash_noise(ix + 1, iy + 1, seed)

    v0 = lerp(v00, v10, fx)
    v1 = lerp(v01, v11, fx)
    return lerp(v0, v1, fy)


def fbm_noise(x, y, seed=0, octaves=4, scale=64):
    """Fractal Brownian Motion noise."""
    value = 0.0
    amplitude = 1.0
    frequency = 1.0
    max_val = 0.0
    for i in range(octaves):
        value += smooth_noise(x * frequency, y * frequency, seed + i * 100, scale) * amplitude
        max_val += amplitude
        amplitude *= 0.5
        frequency *= 2.0
    return value / max_val


def generate_metal_texture(width, height, base_color, accent_color=None, seed=42):
    """Generate a metallic texture with brushed metal look and panel lines."""
    pixels = []
    random.seed(seed)

    # Pre-compute some scratch positions
    num_scratches = random.randint(8, 20)
    scratches = []
    for _ in range(num_scratches):
        sx = random.randint(0, width - 1)
        sy = random.randint(0, height - 1)
        angle = random.uniform(-0.3, 0.3)  # Mostly horizontal
        length = random.randint(10, 60)
        brightness = random.uniform(0.7, 1.3)
        scratches.append((sx, sy, angle, length, brightness))

    # Panel line positions (horizontal and vertical dividers)
    h_lines = sorted(random.sample(range(20, height - 20), random.randint(1, 3)))
    v_lines = sorted(random.sample(range(20, width - 20), random.randint(1, 3)))

    for y in range(height):
        for x in range(width):
            # Base color with subtle noise variation
            noise = fbm_noise(x, y, seed, octaves=3, scale=48)
            detail = fbm_noise(x, y, seed + 500, octaves=2, scale=8)

            # Brushed metal effect (horizontal streaks)
            streak = smooth_noise(x, y, seed + 200, scale=4)
            streak_h = smooth_noise(x * 4, y, seed + 300, scale=128)

            variation = (noise - 0.5) * 0.15 + (detail - 0.5) * 0.05
            metal_streak = (streak - 0.5) * 0.03 + (streak_h - 0.5) * 0.02

            r = clamp((base_color[0] + variation + metal_streak) * 255)
            g = clamp((base_color[1] + variation + metal_streak) * 255)
            b = clamp((base_color[2] + variation + metal_streak) * 255)

            # Panel lines (dark grooves)
            on_panel_line = False
            for hl in h_lines:
                if abs(y - hl) <= 1:
                    on_panel_line = True
            for vl in v_lines:
                if abs(x - vl) <= 1:
                    on_panel_line = True

            if on_panel_line:
                r = clamp(r * 0.4)
                g = clamp(g * 0.4)
                b = clamp(b * 0.4)

            # Scratches
            for sx, sy, angle, length, brightness in scratches:
                dx = x - sx
                dy = y - sy
                # Rotate to scratch space
                rdx = dx * math.cos(angle) + dy * math.sin(angle)
                rdy = -dx * math.sin(angle) + dy * math.cos(angle)
                if 0 <= rdx <= length and abs(rdy) < 1:
                    r = clamp(r * brightness)
                    g = clamp(g * brightness)
                    b = clamp(b * brightness)

            # Edge darkening (subtle vignette)
            edge_x = min(x, width - 1 - x) / (width * 0.5)
            edge_y = min(y, height - 1 - y) / (height * 0.5)
            edge = min(edge_x, edge_y)
            edge = min(edge * 3.0, 1.0)
            r = clamp(r * (0.85 + 0.15 * edge))
            g = clamp(g * (0.85 + 0.15 * edge))
            b = clamp(b * (0.85 + 0.15 * edge))

            # Accent color patches (if provided)
            if accent_color:
                patch_noise = fbm_noise(x, y, seed + 1000, octaves=2, scale=80)
                if patch_noise > 0.65:
                    t = (patch_noise - 0.65) / 0.35
                    t = min(t * 2, 1.0)
                    r = clamp(lerp(r, accent_color[0] * 255, t * 0.6))
                    g = clamp(lerp(g, accent_color[1] * 255, t * 0.6))
                    b = clamp(lerp(b, accent_color[2] * 255, t * 0.6))

            pixels.append((r, g, b))

    return pixels


def generate_wood_texture(width, height, base_color, grain_color, seed=42):
    """Generate a wood texture with grain patterns."""
    pixels = []

    for y in range(height):
        for x in range(width):
            # Wood grain (stretched noise)
            grain = fbm_noise(x * 0.3, y, seed, octaves=4, scale=32)
            ring = math.sin(grain * 20.0) * 0.5 + 0.5

            # Base mix
            t = ring * 0.4 + (grain - 0.5) * 0.3
            t = max(0, min(1, t + 0.5))

            r = clamp(lerp(base_color[0], grain_color[0], t) * 255)
            g = clamp(lerp(base_color[1], grain_color[1], t) * 255)
            b = clamp(lerp(base_color[2], grain_color[2], t) * 255)

            # Fine detail noise
            detail = fbm_noise(x, y, seed + 777, octaves=2, scale=8)
            r = clamp(r + (detail - 0.5) * 15)
            g = clamp(g + (detail - 0.5) * 12)
            b = clamp(b + (detail - 0.5) * 10)

            pixels.append((r, g, b))

    return pixels


def generate_two_tone_texture(width, height, color1, color2, seed=42):
    """Generate a two-tone camouflage/tactical texture."""
    pixels = []

    for y in range(height):
        for x in range(width):
            # Large-scale pattern
            pattern = fbm_noise(x, y, seed, octaves=3, scale=64)
            detail = fbm_noise(x, y, seed + 333, octaves=2, scale=12)

            # Hard(ish) transition between colors
            t = pattern + (detail - 0.5) * 0.15
            if t > 0.55:
                base = color2
            elif t > 0.45:
                blend = (t - 0.45) / 0.1
                base = (lerp(color1[0], color2[0], blend),
                        lerp(color1[1], color2[1], blend),
                        lerp(color1[2], color2[2], blend))
            else:
                base = color1

            # Noise variation
            noise_val = fbm_noise(x, y, seed + 600, octaves=2, scale=24)
            variation = (noise_val - 0.5) * 0.08

            r = clamp((base[0] + variation) * 255)
            g = clamp((base[1] + variation) * 255)
            b = clamp((base[2] + variation) * 255)

            # Subtle brushed metal overlay
            streak = smooth_noise(x * 3, y, seed + 800, scale=128)
            r = clamp(r + (streak - 0.5) * 8)
            g = clamp(g + (streak - 0.5) * 8)
            b = clamp(b + (streak - 0.5) * 8)

            pixels.append((r, g, b))

    return pixels


# ============================================================
# Gun texture definitions
# ============================================================

GUN_TEXTURES = {
    "AssaultRiffle_A": {
        "type": "metal",
        "base": (0.18, 0.22, 0.15),      # Military olive dark
        "accent": (0.12, 0.14, 0.10),     # Darker olive accents
        "seed": 100
    },
    "AssaultRiffle_G": {
        "type": "two_tone",
        "color1": (0.40, 0.36, 0.28),     # Desert tan
        "color2": (0.22, 0.20, 0.16),     # Dark brown
        "seed": 200
    },
    "Gun_A": {
        "type": "metal",
        "base": (0.12, 0.12, 0.14),       # Dark steel/black
        "accent": None,
        "seed": 300
    },
    "Gun_B": {
        "type": "metal",
        "base": (0.42, 0.42, 0.44),       # Silver/chrome
        "accent": (0.20, 0.20, 0.22),     # Dark grip areas
        "seed": 400
    },
    "HeavyWeapon_F": {
        "type": "metal",
        "base": (0.15, 0.18, 0.12),       # Dark military green
        "accent": (0.55, 0.30, 0.08),     # Orange warning details
        "seed": 500
    },
    "Riffle_A": {
        "type": "wood",
        "base": (0.45, 0.28, 0.12),       # Light wood
        "grain": (0.30, 0.18, 0.08),      # Dark wood grain
        "metal_mix": True,
        "seed": 600
    },
    "Shotgun_I": {
        "type": "wood",
        "base": (0.35, 0.22, 0.10),       # Dark wood
        "grain": (0.20, 0.12, 0.06),      # Very dark grain
        "metal_mix": True,
        "seed": 700
    },
    "Uzi_C": {
        "type": "metal",
        "base": (0.08, 0.08, 0.10),       # Near-black metal
        "accent": (0.15, 0.15, 0.16),     # Slightly lighter accents
        "seed": 800
    },
}


def generate_gun_texture(name, spec, size=256):
    """Generate a texture for a specific gun based on its spec."""
    w, h = size, size

    if spec["type"] == "metal":
        pixels = generate_metal_texture(w, h, spec["base"], spec.get("accent"), spec["seed"])
    elif spec["type"] == "wood":
        # For wood guns, bottom half is wood, top half is metal
        if spec.get("metal_mix"):
            wood_pixels = generate_wood_texture(w, h, spec["base"], spec["grain"], spec["seed"])
            metal_pixels = generate_metal_texture(w, h, (0.20, 0.20, 0.22), None, spec["seed"] + 50)
            pixels = []
            for y in range(h):
                for x in range(w):
                    idx = y * w + x
                    # Transition zone around middle
                    blend_y = y / h
                    noise = fbm_noise(x, y, spec["seed"] + 999, octaves=2, scale=32)
                    threshold = 0.5 + (noise - 0.5) * 0.15

                    if blend_y < threshold - 0.03:
                        pixels.append(wood_pixels[idx])
                    elif blend_y > threshold + 0.03:
                        pixels.append(metal_pixels[idx])
                    else:
                        t = (blend_y - (threshold - 0.03)) / 0.06
                        wr, wg, wb = wood_pixels[idx]
                        mr, mg, mb = metal_pixels[idx]
                        pixels.append((
                            clamp(lerp(wr, mr, t)),
                            clamp(lerp(wg, mg, t)),
                            clamp(lerp(wb, mb, t))
                        ))
        else:
            pixels = generate_wood_texture(w, h, spec["base"], spec["grain"], spec["seed"])
    elif spec["type"] == "two_tone":
        pixels = generate_two_tone_texture(w, h, spec["color1"], spec["color2"], spec["seed"])
    else:
        # Fallback: solid gray
        pixels = [(128, 128, 128)] * (w * h)

    return pixels


def main():
    if len(sys.argv) < 2:
        output_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "models", "Guns")
    else:
        output_dir = sys.argv[1]

    os.makedirs(output_dir, exist_ok=True)

    size = 256
    for name, spec in GUN_TEXTURES.items():
        print(f"Generating texture for {name}...")
        pixels = generate_gun_texture(name, spec, size)
        path = os.path.join(output_dir, f"{name}.bmp")
        write_bmp(path, size, size, pixels)
        print(f"  Saved: {path}")

    print(f"\nGenerated {len(GUN_TEXTURES)} textures.")


if __name__ == "__main__":
    main()
