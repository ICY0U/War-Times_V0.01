#!/usr/bin/env python3
"""
FBX Binary to OBJ Converter
Parses FBX 7500 binary format and outputs Wavefront .obj files.
Handles zlib-compressed array properties.

Usage: python fbx2obj.py <input.fbx> [output.obj]
       python fbx2obj.py <directory>   (converts all .fbx in directory)
"""

import struct
import zlib
import sys
import os
import math

# ============================================================
# FBX Binary Parser
# ============================================================

class FBXNode:
    def __init__(self, name="", properties=None, children=None):
        self.name = name
        self.properties = properties or []
        self.children = children or []

    def find(self, name):
        """Find first child node with given name."""
        for c in self.children:
            if c.name == name:
                return c
        return None

    def find_all(self, name):
        """Find all child nodes with given name."""
        return [c for c in self.children if c.name == name]

    def __repr__(self):
        return f"FBXNode({self.name}, props={len(self.properties)}, children={len(self.children)})"


def read_fbx_binary(filepath):
    """Parse an FBX binary file and return the root node."""
    with open(filepath, 'rb') as f:
        data = f.read()

    # Validate header: "Kaydara FBX Binary  \x00"
    magic = data[:21]
    if magic != b'Kaydara FBX Binary  \x00':
        raise ValueError("Not a valid FBX binary file")

    version = struct.unpack_from('<I', data, 23)[0]
    if version < 7100:
        raise ValueError(f"FBX version {version} not supported (need >= 7100)")

    # FBX 7500+ uses 64-bit offsets
    use_64bit = version >= 7500
    offset = 27  # After header

    root = FBXNode("__root__")
    root.children = _read_node_list(data, offset, len(data), use_64bit)
    return root, version


def _read_node_list(data, offset, end_offset, use_64bit):
    """Read a list of sibling nodes until null sentinel."""
    nodes = []
    null_size = 25 if use_64bit else 13

    while offset < end_offset:
        # Check for null sentinel
        sentinel = data[offset:offset + null_size]
        if sentinel == b'\x00' * null_size:
            break

        node, offset = _read_node(data, offset, use_64bit)
        if node:
            nodes.append(node)

    return nodes


def _read_node(data, offset, use_64bit):
    """Read a single FBX node. Returns (node, next_offset)."""
    if use_64bit:
        end_offset, num_props, prop_list_len = struct.unpack_from('<QQQ', data, offset)
        offset += 24
    else:
        end_offset, num_props, prop_list_len = struct.unpack_from('<III', data, offset)
        offset += 12

    if end_offset == 0:
        return None, offset

    name_len = data[offset]
    offset += 1
    name = data[offset:offset + name_len].decode('ascii', errors='replace')
    offset += name_len

    # Read properties
    prop_start = offset
    properties = []
    for _ in range(num_props):
        prop, offset = _read_property(data, offset)
        properties.append(prop)

    # Read children (if there's space between properties end and node end)
    children = []
    if offset < end_offset:
        children = _read_node_list(data, offset, end_offset, use_64bit)

    node = FBXNode(name, properties, children)
    return node, end_offset


def _read_property(data, offset):
    """Read a single FBX property. Returns (value, next_offset)."""
    type_code = chr(data[offset])
    offset += 1

    if type_code == 'C':  # bool
        val = struct.unpack_from('<B', data, offset)[0]
        return bool(val), offset + 1
    elif type_code == 'Y':  # int16
        val = struct.unpack_from('<h', data, offset)[0]
        return val, offset + 2
    elif type_code == 'I':  # int32
        val = struct.unpack_from('<i', data, offset)[0]
        return val, offset + 4
    elif type_code == 'L':  # int64
        val = struct.unpack_from('<q', data, offset)[0]
        return val, offset + 8
    elif type_code == 'F':  # float32
        val = struct.unpack_from('<f', data, offset)[0]
        return val, offset + 4
    elif type_code == 'D':  # float64
        val = struct.unpack_from('<d', data, offset)[0]
        return val, offset + 8
    elif type_code == 'S':  # string
        length = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        val = data[offset:offset + length].decode('utf-8', errors='replace')
        return val, offset + length
    elif type_code == 'R':  # raw binary
        length = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        val = data[offset:offset + length]
        return val, offset + length
    elif type_code in ('f', 'd', 'i', 'l', 'b'):  # arrays
        return _read_array_property(data, offset, type_code)
    else:
        raise ValueError(f"Unknown FBX property type: {type_code}")


def _read_array_property(data, offset, type_code):
    """Read an array property (float32/float64/int32/int64/bool array)."""
    array_len, encoding, compressed_len = struct.unpack_from('<III', data, offset)
    offset += 12

    # Determine element format
    fmt_map = {'f': ('<f', 4), 'd': ('<d', 8), 'i': ('<i', 4), 'l': ('<q', 8), 'b': ('<B', 1)}
    fmt, elem_size = fmt_map[type_code]

    if encoding == 0:
        # Uncompressed
        total_size = array_len * elem_size
        raw_data = data[offset:offset + total_size]
        offset += total_size
    elif encoding == 1:
        # Zlib compressed
        compressed_data = data[offset:offset + compressed_len]
        raw_data = zlib.decompress(compressed_data)
        offset += compressed_len
    else:
        raise ValueError(f"Unknown array encoding: {encoding}")

    # Unpack array
    values = list(struct.unpack(f'<{array_len}{fmt[1]}', raw_data[:array_len * elem_size]))
    return values, offset


# ============================================================
# Geometry Extraction
# ============================================================

def extract_geometries(root):
    """Extract all Geometry (Mesh) nodes from the FBX tree."""
    geometries = []

    objects = root.find("Objects")
    if not objects:
        return geometries

    for node in objects.find_all("Geometry"):
        # Check if this is a Mesh geometry
        if len(node.properties) >= 3 and node.properties[2] == "Mesh":
            geo = extract_single_geometry(node)
            if geo:
                name = node.properties[1] if len(node.properties) > 1 else "unnamed"
                # FBX names often have "\x00\x01" suffix
                if isinstance(name, str):
                    name = name.split('\x00')[0].split('::')[-1]
                geo['name'] = name
                geometries.append(geo)

    return geometries


def extract_single_geometry(geo_node):
    """Extract vertices, normals, and polygon indices from a Geometry node."""
    result = {}

    # Vertices (positions as doubles: x,y,z,x,y,z,...)
    vert_node = geo_node.find("Vertices")
    if not vert_node or not vert_node.properties:
        return None
    raw_verts = vert_node.properties[0]
    if not isinstance(raw_verts, list):
        return None

    positions = []
    for i in range(0, len(raw_verts), 3):
        positions.append((raw_verts[i], raw_verts[i+1], raw_verts[i+2]))
    result['positions'] = positions

    # PolygonVertexIndex (int32 array, negative marks end of polygon)
    idx_node = geo_node.find("PolygonVertexIndex")
    if not idx_node or not idx_node.properties:
        return None
    raw_indices = idx_node.properties[0]

    # Parse polygon groups
    polygons = []
    current_poly = []
    for idx in raw_indices:
        if idx < 0:
            # Negative: actual index is (-idx - 1), marks end of polygon
            current_poly.append(-idx - 1)
            polygons.append(current_poly)
            current_poly = []
        else:
            current_poly.append(idx)
    if current_poly:
        polygons.append(current_poly)
    result['polygons'] = polygons

    # Normals
    normals = None
    normal_mapping = "ByPolygonVertex"
    normal_ref = "Direct"

    layer_normal = geo_node.find("LayerElementNormal")
    if layer_normal:
        n_node = layer_normal.find("Normals")
        if n_node and n_node.properties:
            raw_normals = n_node.properties[0]
            if isinstance(raw_normals, list):
                normals = []
                for i in range(0, len(raw_normals), 3):
                    normals.append((raw_normals[i], raw_normals[i+1], raw_normals[i+2]))

        mapping_node = layer_normal.find("MappingInformationType")
        if mapping_node and mapping_node.properties:
            normal_mapping = mapping_node.properties[0]

        ref_node = layer_normal.find("ReferenceInformationType")
        if ref_node and ref_node.properties:
            normal_ref = ref_node.properties[0]

        # Handle IndexToDirect reference
        if normal_ref == "IndexToDirect":
            ni_node = layer_normal.find("NormalsIndex")
            if ni_node and ni_node.properties:
                result['normal_indices'] = ni_node.properties[0]

    result['normals'] = normals
    result['normal_mapping'] = normal_mapping
    result['normal_ref'] = normal_ref

    return result


# ============================================================
# OBJ Writer
# ============================================================

def geometry_to_obj(geo, scale=1.0, center=(0,0,0)):
    """Convert extracted geometry to OBJ format string."""
    lines = []
    lines.append(f"# Converted from FBX: {geo.get('name', 'unknown')}")
    lines.append(f"# Vertices: {len(geo['positions'])}")
    lines.append("")

    # Write vertices (centered, scaled, Z-up -> Y-up: output x, z, -y)
    cx, cy, cz = center
    for x, y, z in geo['positions']:
        lines.append(f"v {(x-cx)*scale:.6f} {(z-cz)*scale:.6f} {-(y-cy)*scale:.6f}")
    lines.append("")

    # Write normals (Z-up -> Y-up: output nx, nz, -ny)
    normals = geo.get('normals')
    normal_mapping = geo.get('normal_mapping', 'ByPolygonVertex')
    normal_ref = geo.get('normal_ref', 'Direct')
    normal_indices = geo.get('normal_indices')
    has_normals = normals is not None and len(normals) > 0

    if has_normals:
        for nx, ny, nz in normals:
            lines.append(f"vn {nx:.6f} {nz:.6f} {-ny:.6f}")
        lines.append("")

    # Write faces
    # FBX uses 0-based indices, OBJ uses 1-based
    if has_normals and normal_mapping == "ByPolygonVertex":
        # Each polygon vertex has its own normal
        normal_counter = 0
        for poly in geo['polygons']:
            face_parts = []
            for vi in poly:
                if normal_ref == "IndexToDirect" and normal_indices:
                    ni = normal_indices[normal_counter] + 1
                else:
                    ni = normal_counter + 1
                face_parts.append(f"{vi + 1}//{ni}")
                normal_counter += 1
            lines.append("f " + " ".join(face_parts))
    elif has_normals and normal_mapping == "ByVertexIndex":
        # Normals indexed by vertex index
        for poly in geo['polygons']:
            face_parts = []
            for vi in poly:
                ni = vi + 1  # Same index as vertex
                face_parts.append(f"{vi + 1}//{ni}")
            lines.append("f " + " ".join(face_parts))
    else:
        # No normals
        for poly in geo['polygons']:
            face_parts = [str(vi + 1) for vi in poly]
            lines.append("f " + " ".join(face_parts))

    return "\n".join(lines) + "\n"


def merge_geometries_to_obj(geometries, scale=1.0, center=(0,0,0)):
    """Merge multiple geometries into a single OBJ, with vertex offset tracking."""
    lines = []
    lines.append("# Converted from FBX (merged geometries)")
    lines.append(f"# Total geometries: {len(geometries)}")
    lines.append("")

    cx, cy, cz = center
    vertex_offset = 0
    normal_offset = 0

    for geo in geometries:
        name = geo.get('name', 'unnamed')
        lines.append(f"o {name}")
        lines.append("")

        # Write vertices (centered, scaled, Z-up -> Y-up: output x, z, -y)
        for x, y, z in geo['positions']:
            lines.append(f"v {(x-cx)*scale:.6f} {(z-cz)*scale:.6f} {-(y-cy)*scale:.6f}")
        lines.append("")

        # Write normals (Z-up -> Y-up: output nx, nz, -ny)
        normals = geo.get('normals')
        normal_mapping = geo.get('normal_mapping', 'ByPolygonVertex')
        normal_ref = geo.get('normal_ref', 'Direct')
        normal_indices = geo.get('normal_indices')
        has_normals = normals is not None and len(normals) > 0

        if has_normals:
            for nx, ny, nz in normals:
                lines.append(f"vn {nx:.6f} {nz:.6f} {-ny:.6f}")
            lines.append("")

        # Write faces with offset
        if has_normals and normal_mapping == "ByPolygonVertex":
            normal_counter = 0
            for poly in geo['polygons']:
                face_parts = []
                for vi in poly:
                    if normal_ref == "IndexToDirect" and normal_indices:
                        ni = normal_indices[normal_counter] + 1 + normal_offset
                    else:
                        ni = normal_counter + 1 + normal_offset
                    face_parts.append(f"{vi + 1 + vertex_offset}//{ni}")
                    normal_counter += 1
                lines.append("f " + " ".join(face_parts))
        elif has_normals and normal_mapping == "ByVertexIndex":
            for poly in geo['polygons']:
                face_parts = []
                for vi in poly:
                    ni = vi + 1 + normal_offset
                    face_parts.append(f"{vi + 1 + vertex_offset}//{ni}")
                lines.append("f " + " ".join(face_parts))
        else:
            for poly in geo['polygons']:
                face_parts = [str(vi + 1 + vertex_offset) for vi in poly]
                lines.append("f " + " ".join(face_parts))

        vertex_offset += len(geo['positions'])
        if has_normals:
            normal_offset += len(normals)

        lines.append("")

    return "\n".join(lines) + "\n"


# ============================================================
# Auto-scale: normalize model to fit in a unit-ish bounding box
# ============================================================

def compute_bounds(geometries):
    """Compute AABB across all geometries."""
    min_v = [float('inf')] * 3
    max_v = [float('-inf')] * 3
    for geo in geometries:
        for x, y, z in geo['positions']:
            min_v[0] = min(min_v[0], x)
            min_v[1] = min(min_v[1], y)
            min_v[2] = min(min_v[2], z)
            max_v[0] = max(max_v[0], x)
            max_v[1] = max(max_v[1], y)
            max_v[2] = max(max_v[2], z)
    return min_v, max_v


def auto_scale(geometries, target_size=1.0):
    """Compute scale factor to normalize largest dimension to target_size."""
    min_v, max_v = compute_bounds(geometries)
    extents = [max_v[i] - min_v[i] for i in range(3)]
    max_extent = max(extents)
    if max_extent < 1e-6:
        return 1.0
    return target_size / max_extent


# ============================================================
# Main
# ============================================================

def convert_fbx_to_obj(fbx_path, obj_path=None, normalize=True, target_size=1.0):
    """Convert a single FBX file to OBJ."""
    if obj_path is None:
        obj_path = os.path.splitext(fbx_path)[0] + ".obj"

    print(f"  Parsing: {os.path.basename(fbx_path)}")
    root, version = read_fbx_binary(fbx_path)

    geometries = extract_geometries(root)
    if not geometries:
        print(f"  WARNING: No mesh geometries found in {fbx_path}")
        return False

    total_verts = sum(len(g['positions']) for g in geometries)
    total_polys = sum(len(g['polygons']) for g in geometries)

    # Compute scale and center
    # Center X/Y (FBX), but place bottom at Z=0 (FBX Z → output Y after swap)
    # so models sit on the ground plane
    scale = 1.0
    min_v, max_v = compute_bounds(geometries)
    center = ((min_v[0]+max_v[0])/2, (min_v[1]+max_v[1])/2, min_v[2])
    if normalize:
        scale = auto_scale(geometries, target_size)

    # Generate OBJ
    if len(geometries) == 1:
        obj_content = geometry_to_obj(geometries[0], scale=scale, center=center)
    else:
        obj_content = merge_geometries_to_obj(geometries, scale=scale, center=center)

    with open(obj_path, 'w') as f:
        f.write(obj_content)

    print(f"  -> {os.path.basename(obj_path)} ({len(geometries)} meshes, {total_verts} verts, {total_polys} polys, scale={scale:.4f})")
    return True


def convert_directory(dir_path, target_size=1.0):
    """Convert all FBX files in a directory to OBJ."""
    fbx_files = [f for f in os.listdir(dir_path) if f.lower().endswith('.fbx')]
    if not fbx_files:
        print(f"No .fbx files found in {dir_path}")
        return

    print(f"Converting {len(fbx_files)} FBX files in {dir_path}")
    print(f"Target size: {target_size}")
    print()

    success = 0
    for fbx_file in sorted(fbx_files):
        fbx_path = os.path.join(dir_path, fbx_file)
        try:
            if convert_fbx_to_obj(fbx_path, target_size=target_size):
                success += 1
        except Exception as e:
            print(f"  ERROR converting {fbx_file}: {e}")

    print(f"\nDone: {success}/{len(fbx_files)} converted successfully")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python fbx2obj.py <input.fbx|directory> [output.obj] [--size=1.0]")
        sys.exit(1)

    input_path = sys.argv[1]

    # Parse optional size parameter
    target_size = 1.0
    for arg in sys.argv[2:]:
        if arg.startswith("--size="):
            target_size = float(arg.split("=")[1])

    if os.path.isdir(input_path):
        convert_directory(input_path, target_size=target_size)
    elif os.path.isfile(input_path):
        output = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else None
        convert_fbx_to_obj(input_path, output, target_size=target_size)
    else:
        print(f"File or directory not found: {input_path}")
        sys.exit(1)
