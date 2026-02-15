#!/usr/bin/env python3
"""
Process OBJ models:
  1. Rotate all vertices/normals -90° CCW around Y axis (anti-clockwise from above)
  2. Generate box-mapped UV coordinates per face vertex
  3. Rewrite OBJ with v, vt, vn, and f v/vt/vn format

Usage: python process_models.py <directory|file.obj>
"""

import os
import sys
import math

def rotate_y_ccw90(x, y, z):
    """Rotate point -90 degrees (CCW from above) around Y axis.
    In left-handed coords (DirectX): x' = z, z' = -x, y' = y
    """
    return (z, y, -x)

def box_map_uv(px, py, pz, nx, ny, nz, bounds, tile=2.0):
    """Generate UV from box/triplanar mapping based on dominant normal axis.
    bounds = (minx,miny,minz, maxx,maxy,maxz)
    """
    ax, ay, az = abs(nx), abs(ny), abs(nz)
    minx, miny, minz, maxx, maxy, maxz = bounds
    sx = max(maxx - minx, 0.001)
    sy = max(maxy - miny, 0.001)
    sz = max(maxz - minz, 0.001)

    if ax >= ay and ax >= az:
        # X-facing: project onto YZ
        u = (pz - minz) / sz
        v = (py - miny) / sy
    elif ay >= ax and ay >= az:
        # Y-facing: project onto XZ
        u = (px - minx) / sx
        v = (pz - minz) / sz
    else:
        # Z-facing: project onto XY
        u = (px - minx) / sx
        v = (py - miny) / sy

    # Tile the texture
    u *= tile
    v *= tile
    return (u, v)

def process_obj(filepath):
    """Process a single OBJ file: rotate + add UVs."""
    print(f"Processing: {filepath}")

    with open(filepath, 'r') as f:
        lines = f.readlines()

    positions = []  # List of (x, y, z)
    normals = []    # List of (nx, ny, nz)
    faces = []      # List of list of (vi, vni) — 1-based indices
    comments = []

    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith('#'):
            comments.append(stripped)
            continue

        parts = stripped.split()
        prefix = parts[0]

        if prefix == 'v' and len(parts) >= 4:
            x, y, z = float(parts[1]), float(parts[2]), float(parts[3])
            positions.append((x, y, z))
        elif prefix == 'vn' and len(parts) >= 4:
            nx, ny, nz = float(parts[1]), float(parts[2]), float(parts[3])
            normals.append((nx, ny, nz))
        elif prefix == 'vt':
            pass  # Skip existing texcoords (we regenerate)
        elif prefix == 'f':
            face_verts = []
            for token in parts[1:]:
                # Parse v, v/vt, v/vt/vn, v//vn
                components = token.split('/')
                vi = int(components[0])
                vni = -1
                if len(components) >= 3 and components[2]:
                    vni = int(components[2])
                elif len(components) == 2 and components[1]:
                    pass  # v/vt only — no normal index
                face_verts.append((vi, vni))
            faces.append(face_verts)

    if not positions:
        print(f"  WARNING: No vertices found, skipping")
        return

    # Step 1: Rotate all positions and normals by -90° CCW around Y
    rotated_positions = []
    for x, y, z in positions:
        rx, ry, rz = rotate_y_ccw90(x, y, z)
        rotated_positions.append((rx, ry, rz))

    rotated_normals = []
    for nx, ny, nz in normals:
        rnx, rny, rnz = rotate_y_ccw90(nx, ny, nz)
        # Renormalize
        length = math.sqrt(rnx*rnx + rny*rny + rnz*rnz)
        if length > 0.0001:
            rnx /= length
            rny /= length
            rnz /= length
        rotated_normals.append((rnx, rny, rnz))

    # Compute bounding box of rotated positions (for UV mapping)
    xs = [p[0] for p in rotated_positions]
    ys = [p[1] for p in rotated_positions]
    zs = [p[2] for p in rotated_positions]
    bounds = (min(xs), min(ys), min(zs), max(xs), max(ys), max(zs))

    # Step 2: Generate UV coordinates per face-vertex using box mapping
    texcoords = []  # List of (u, v)
    new_faces = []  # List of list of (vi, vti, vni)

    for face_verts in faces:
        new_face = []
        for vi, vni in face_verts:
            px, py, pz = rotated_positions[vi - 1]  # OBJ is 1-based

            # Get normal for UV projection
            if vni > 0 and vni <= len(rotated_normals):
                nx, ny, nz = rotated_normals[vni - 1]
            else:
                nx, ny, nz = 0, 1, 0  # Default up

            u, v = box_map_uv(px, py, pz, nx, ny, nz, bounds, tile=2.0)
            texcoords.append((u, v))
            vti = len(texcoords)  # 1-based index
            new_face.append((vi, vti, vni))
        new_faces.append(new_face)

    # Step 3: Write updated OBJ
    with open(filepath, 'w') as f:
        # Header comments
        f.write("# Processed: rotated -90 CCW around Y + box-mapped UVs\n")
        f.write(f"# Vertices: {len(rotated_positions)}\n")
        f.write(f"# Normals: {len(rotated_normals)}\n")
        f.write(f"# TexCoords: {len(texcoords)}\n\n")

        # Vertices
        for x, y, z in rotated_positions:
            f.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
        f.write("\n")

        # Texture coordinates
        for u, v in texcoords:
            f.write(f"vt {u:.6f} {v:.6f}\n")
        f.write("\n")

        # Normals
        for nx, ny, nz in rotated_normals:
            f.write(f"vn {nx:.6f} {ny:.6f} {nz:.6f}\n")
        f.write("\n")

        # Faces
        for face in new_faces:
            tokens = []
            for vi, vti, vni in face:
                if vni > 0:
                    tokens.append(f"{vi}/{vti}/{vni}")
                else:
                    tokens.append(f"{vi}/{vti}")
            f.write("f " + " ".join(tokens) + "\n")

    print(f"  Done: {len(rotated_positions)} verts, {len(texcoords)} UVs, {len(new_faces)} faces")

def main():
    if len(sys.argv) < 2:
        print("Usage: python process_models.py <directory|file.obj>")
        sys.exit(1)

    target = sys.argv[1]

    if os.path.isfile(target) and target.lower().endswith('.obj'):
        process_obj(target)
    elif os.path.isdir(target):
        for root, dirs, files in os.walk(target):
            for fname in files:
                if fname.lower().endswith('.obj'):
                    process_obj(os.path.join(root, fname))
    else:
        print(f"Not a valid OBJ file or directory: {target}")
        sys.exit(1)

    print("\nAll models processed.")

if __name__ == "__main__":
    main()
