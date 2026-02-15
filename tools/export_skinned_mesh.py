#!/usr/bin/env python3
"""
FBX Binary → Skinned Mesh Exporter
Parses FBX 7400/7500 binary files and exports skinned mesh data
for the War Times engine GPU skinning pipeline.

Output: .skmesh binary file containing vertices, indices, bones, bind poses.

Usage: python export_skinned_mesh.py <input.fbx> [output.skmesh]
"""

import struct
import zlib
import sys
import os
import math

# ============================================================
# FBX Binary Parser (supports 7400+)
# ============================================================

class FBXNode:
    def __init__(self, name="", properties=None, children=None):
        self.name = name
        self.properties = properties or []
        self.children = children or []

    def find(self, name):
        for c in self.children:
            if c.name == name:
                return c
        return None

    def find_all(self, name):
        return [c for c in self.children if c.name == name]

    def __repr__(self):
        return f"FBXNode({self.name}, props={len(self.properties)}, children={len(self.children)})"


def read_fbx_binary(filepath):
    with open(filepath, 'rb') as f:
        data = f.read()

    magic = data[:21]
    if magic != b'Kaydara FBX Binary  \x00':
        raise ValueError("Not a valid FBX binary file")

    version = struct.unpack_from('<I', data, 23)[0]
    use_64bit = version >= 7500
    offset = 27

    root = FBXNode("__root__")
    root.children = _read_node_list(data, offset, len(data), use_64bit)
    return root, version


def _read_node_list(data, offset, end_offset, use_64bit):
    nodes = []
    null_size = 25 if use_64bit else 13

    while offset < end_offset:
        sentinel = data[offset:offset + null_size]
        if sentinel == b'\x00' * null_size:
            break
        node, offset = _read_node(data, offset, use_64bit)
        if node:
            nodes.append(node)
    return nodes


def _read_node(data, offset, use_64bit):
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

    properties = []
    for _ in range(num_props):
        prop, offset = _read_property(data, offset)
        properties.append(prop)

    children = []
    if offset < end_offset:
        children = _read_node_list(data, offset, end_offset, use_64bit)

    node = FBXNode(name, properties, children)
    return node, end_offset


def _read_property(data, offset):
    type_code = chr(data[offset])
    offset += 1

    if type_code == 'C':
        return bool(data[offset]), offset + 1
    elif type_code == 'Y':
        return struct.unpack_from('<h', data, offset)[0], offset + 2
    elif type_code == 'I':
        return struct.unpack_from('<i', data, offset)[0], offset + 4
    elif type_code == 'L':
        return struct.unpack_from('<q', data, offset)[0], offset + 8
    elif type_code == 'F':
        return struct.unpack_from('<f', data, offset)[0], offset + 4
    elif type_code == 'D':
        return struct.unpack_from('<d', data, offset)[0], offset + 8
    elif type_code == 'S':
        length = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        return data[offset:offset + length].decode('utf-8', errors='replace'), offset + length
    elif type_code == 'R':
        length = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        return data[offset:offset + length], offset + length
    elif type_code in ('f', 'd', 'i', 'l', 'b'):
        return _read_array_property(data, offset, type_code)
    else:
        raise ValueError(f"Unknown FBX property type: {type_code}")


def _read_array_property(data, offset, type_code):
    array_len, encoding, compressed_len = struct.unpack_from('<III', data, offset)
    offset += 12
    fmt_map = {'f': ('<f', 4), 'd': ('<d', 8), 'i': ('<i', 4), 'l': ('<q', 8), 'b': ('<B', 1)}
    fmt, elem_size = fmt_map[type_code]

    if encoding == 0:
        total_size = array_len * elem_size
        raw_data = data[offset:offset + total_size]
        offset += total_size
    elif encoding == 1:
        compressed_data = data[offset:offset + compressed_len]
        raw_data = zlib.decompress(compressed_data)
        offset += compressed_len
    else:
        raise ValueError(f"Unknown array encoding: {encoding}")

    values = list(struct.unpack(f'<{array_len}{fmt[1]}', raw_data[:array_len * elem_size]))
    return values, offset


# ============================================================
# FBX Data Extraction
# ============================================================

def clean_name(name):
    """Clean FBX name (remove \\x00\\x01 suffixes and namespacing)."""
    if isinstance(name, str):
        name = name.split('\x00')[0].split('::')[-1]
    return name


def extract_geometry(root, geo_id=None):
    """Extract mesh geometry from FBX."""
    objects = root.find("Objects")
    if not objects:
        return None

    for node in objects.find_all("Geometry"):
        if len(node.properties) < 3 or node.properties[2] != "Mesh":
            continue
        if geo_id is not None and node.properties[0] != geo_id:
            continue

        result = {}
        result['id'] = node.properties[0]
        result['name'] = clean_name(node.properties[1])

        # Vertices
        vert_node = node.find("Vertices")
        if not vert_node or not vert_node.properties:
            continue
        raw_verts = vert_node.properties[0]
        positions = []
        for i in range(0, len(raw_verts), 3):
            positions.append((raw_verts[i], raw_verts[i+1], raw_verts[i+2]))
        result['positions'] = positions

        # Polygon indices
        idx_node = node.find("PolygonVertexIndex")
        if not idx_node or not idx_node.properties:
            continue
        raw_indices = idx_node.properties[0]
        polygons = []
        current_poly = []
        for idx in raw_indices:
            if idx < 0:
                current_poly.append(-idx - 1)
                polygons.append(current_poly)
                current_poly = []
            else:
                current_poly.append(idx)
        if current_poly:
            polygons.append(current_poly)
        result['polygons'] = polygons

        # Normals
        layer_normal = node.find("LayerElementNormal")
        if layer_normal:
            n_node = layer_normal.find("Normals")
            if n_node and n_node.properties:
                raw = n_node.properties[0]
                normals = [(raw[i], raw[i+1], raw[i+2]) for i in range(0, len(raw), 3)]
                result['normals'] = normals

                mapping_node = layer_normal.find("MappingInformationType")
                result['normal_mapping'] = mapping_node.properties[0] if mapping_node else "ByPolygonVertex"

                ref_node = layer_normal.find("ReferenceInformationType")
                result['normal_ref'] = ref_node.properties[0] if ref_node else "Direct"

                if result['normal_ref'] == "IndexToDirect":
                    ni_node = layer_normal.find("NormalsIndex")
                    if ni_node:
                        result['normal_indices'] = ni_node.properties[0]

        # UVs
        layer_uv = node.find("LayerElementUV")
        if layer_uv:
            uv_node = layer_uv.find("UV")
            if uv_node and uv_node.properties:
                raw = uv_node.properties[0]
                uvs = [(raw[i], raw[i+1]) for i in range(0, len(raw), 2)]
                result['uvs'] = uvs

                uv_mapping = layer_uv.find("MappingInformationType")
                result['uv_mapping'] = uv_mapping.properties[0] if uv_mapping else "ByPolygonVertex"

                uv_ref = layer_uv.find("ReferenceInformationType")
                result['uv_ref'] = uv_ref.properties[0] if uv_ref else "Direct"

                if result['uv_ref'] == "IndexToDirect":
                    uvi_node = layer_uv.find("UVIndex")
                    if uvi_node:
                        result['uv_indices'] = uvi_node.properties[0]

        if geo_id is not None:
            return result

    return result if geo_id is None else None


def extract_all_geometries(root):
    """Extract all mesh geometries."""
    objects = root.find("Objects")
    if not objects:
        return []
    geos = []
    for node in objects.find_all("Geometry"):
        if len(node.properties) >= 3 and node.properties[2] == "Mesh":
            geo = extract_geometry(root, node.properties[0])
            if geo:
                geos.append(geo)
    return geos


def extract_models(root):
    """Extract all Model nodes with their IDs and names."""
    objects = root.find("Objects")
    if not objects:
        return {}
    models = {}
    for node in objects.find_all("Model"):
        if len(node.properties) >= 3:
            mid = node.properties[0]
            mname = clean_name(node.properties[1])
            mtype = node.properties[2]
            # Extract local transform from Properties70
            lcl_translation = [0.0, 0.0, 0.0]
            lcl_rotation = [0.0, 0.0, 0.0]
            lcl_scaling = [1.0, 1.0, 1.0]
            p70 = node.find("Properties70")
            if p70:
                for p in p70.find_all("P"):
                    if len(p.properties) >= 7:
                        pname = p.properties[0]
                        if pname == "Lcl Translation":
                            lcl_translation = [p.properties[4], p.properties[5], p.properties[6]]
                        elif pname == "Lcl Rotation":
                            lcl_rotation = [p.properties[4], p.properties[5], p.properties[6]]
                        elif pname == "Lcl Scaling":
                            lcl_scaling = [p.properties[4], p.properties[5], p.properties[6]]
            models[mid] = {
                'name': mname,
                'type': mtype,
                'translation': lcl_translation,
                'rotation': lcl_rotation,
                'scaling': lcl_scaling,
            }
    return models


def extract_deformers(root):
    """Extract skin deformers (clusters) with bone weights."""
    objects = root.find("Objects")
    if not objects:
        return {}, {}

    skin_deformers = {}  # id -> skin deformer info
    clusters = {}        # id -> cluster info (bone name, indices, weights, transforms)

    for node in objects.find_all("Deformer"):
        if len(node.properties) < 3:
            continue
        did = node.properties[0]
        dname = clean_name(node.properties[1])
        dtype = node.properties[2]

        if dtype == "Skin":
            skin_deformers[did] = {'name': dname, 'id': did}
        elif dtype == "Cluster":
            cluster = {'name': dname, 'id': did}

            idx_node = node.find("Indexes")
            if idx_node and idx_node.properties:
                cluster['indices'] = idx_node.properties[0]
            else:
                cluster['indices'] = []

            wgt_node = node.find("Weights")
            if wgt_node and wgt_node.properties:
                cluster['weights'] = wgt_node.properties[0]
            else:
                cluster['weights'] = []

            # Transform = inverse bind pose of the mesh in bone space
            tf_node = node.find("Transform")
            if tf_node and tf_node.properties:
                cluster['transform'] = tf_node.properties[0]

            # TransformLink = bind pose of the bone in world space
            tfl_node = node.find("TransformLink")
            if tfl_node and tfl_node.properties:
                cluster['transform_link'] = tfl_node.properties[0]

            clusters[did] = cluster

    return skin_deformers, clusters


def extract_connections(root):
    """Extract all connections (parent-child relationships)."""
    conn_node = root.find("Connections")
    if not conn_node:
        return []
    connections = []
    for c in conn_node.find_all("C"):
        if len(c.properties) >= 3:
            connections.append({
                'type': c.properties[0],
                'child': c.properties[1],
                'parent': c.properties[2],
            })
    return connections


def extract_bind_poses(root):
    """Extract bind pose matrices from Pose node."""
    objects = root.find("Objects")
    if not objects:
        return {}
    poses = {}
    for node in objects.find_all("Pose"):
        for pn in node.find_all("PoseNode"):
            n_node = pn.find("Node")
            m_node = pn.find("Matrix")
            if n_node and m_node and n_node.properties and m_node.properties:
                node_id = n_node.properties[0]
                matrix = m_node.properties[0]
                poses[node_id] = matrix
    return poses


# ============================================================
# Matrix Math
# ============================================================

def mat4_identity():
    return [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]

def mat4_from_fbx(fbx_mat):
    """FBX stores matrices row-major as 16 doubles. Convert to our format (row-major)."""
    if not fbx_mat or len(fbx_mat) < 16:
        return mat4_identity()
    return [float(x) for x in fbx_mat[:16]]

def mat4_multiply(a, b):
    """Multiply two 4x4 matrices (row-major)."""
    r = [0.0] * 16
    for i in range(4):
        for j in range(4):
            s = 0.0
            for k in range(4):
                s += a[i*4+k] * b[k*4+j]
            r[i*4+j] = s
    return r

def mat4_inverse(m):
    """Invert a 4x4 matrix (row-major)."""
    # Compute cofactors
    inv = [0.0] * 16

    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10]
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10]
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9]
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9]

    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10]
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10]
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9]
    inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9]

    inv[2]  =  m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6]
    inv[6]  = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6]
    inv[10] =  m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5]
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5]

    inv[3]  = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6]
    inv[7]  =  m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6]
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9]  + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7] + m[8]*m[3]*m[5]
    inv[15] =  m[0]*m[5]*m[10] - m[0]*m[6]*m[9]  - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6] - m[8]*m[2]*m[5]

    det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12]
    if abs(det) < 1e-12:
        return mat4_identity()

    det = 1.0 / det
    return [x * det for x in inv]

def mat4_transpose(m):
    return [m[0],m[4],m[8],m[12], m[1],m[5],m[9],m[13], m[2],m[6],m[10],m[14], m[3],m[7],m[11],m[15]]

def euler_to_matrix(rx, ry, rz):
    """Create rotation matrix from Euler angles (degrees, XYZ order as FBX uses)."""
    rx, ry, rz = math.radians(rx), math.radians(ry), math.radians(rz)
    cx, sx = math.cos(rx), math.sin(rx)
    cy, sy = math.cos(ry), math.sin(ry)
    cz, sz = math.cos(rz), math.sin(rz)

    # Rotation order: Rz * Ry * Rx (FBX default)
    return [
        cy*cz, cy*sz, -sy, 0,
        sx*sy*cz - cx*sz, sx*sy*sz + cx*cz, sx*cy, 0,
        cx*sy*cz + sx*sz, cx*sy*sz - sx*cz, cx*cy, 0,
        0, 0, 0, 1
    ]

def make_transform(translation, rotation, scaling):
    """Build a 4x4 transform from TRS."""
    rot = euler_to_matrix(rotation[0], rotation[1], rotation[2])
    sx, sy, sz = scaling
    return [
        rot[0]*sx, rot[1]*sx, rot[2]*sx, 0,
        rot[4]*sy, rot[5]*sy, rot[6]*sy, 0,
        rot[8]*sz, rot[9]*sz, rot[10]*sz, 0,
        translation[0], translation[1], translation[2], 1
    ]


# ============================================================
# Build Skinned Mesh Data
# ============================================================

def build_skinned_mesh(root):
    """Extract and build complete skinned mesh data from FBX."""

    # Get connections
    connections = extract_connections(root)
    models = extract_models(root)
    geometries = extract_all_geometries(root)
    skin_deformers, clusters = extract_deformers(root)
    bind_poses = extract_bind_poses(root)

    print(f"  Found {len(geometries)} geometries, {len(models)} models")
    print(f"  Found {len(skin_deformers)} skin deformers, {len(clusters)} clusters (bones)")
    print(f"  Found {len(bind_poses)} bind poses")

    # Build connection maps
    child_to_parent = {}
    parent_to_children = {}
    for c in connections:
        child_to_parent[c['child']] = c['parent']
        if c['parent'] not in parent_to_children:
            parent_to_children[c['parent']] = []
        parent_to_children[c['parent']].append(c['child'])

    # Find skin deformer with the most influences (the real arms, not sleeves)
    skin_id = None
    skin_geo_id = None
    best_influence_count = 0

    for sid in skin_deformers:
        # Count total influences for this skin deformer's clusters
        total_influences = 0
        for c in connections:
            if c['parent'] == sid and c['child'] in clusters:
                total_influences += len(clusters[c['child']].get('indices', []))

        # Find connected geometry
        geo_id = child_to_parent.get(sid)
        print(f"  Skin deformer {sid}: {total_influences} total influences → target {geo_id}")

        if total_influences > best_influence_count:
            best_influence_count = total_influences
            skin_id = sid
            skin_geo_id = geo_id

    if skin_id is None:
        print("  ERROR: No skin deformer found!")
        return None

    print(f"  Selected skin deformer {skin_id} → geometry {skin_geo_id} ({best_influence_count} influences)")

    # Find the geometry connected to the model the skin is on
    target_geo = None
    for geo in geometries:
        if geo['id'] == skin_geo_id:
            target_geo = geo
            break

    # If skin connects to model, find geometry connected to that model
    if target_geo is None:
        # skin_geo_id might be the model, find its geometry
        geos_for_model = [c['child'] for c in connections
                          if c['parent'] == skin_geo_id and c['child'] in [g['id'] for g in geometries]]
        if geos_for_model:
            for geo in geometries:
                if geo['id'] == geos_for_model[0]:
                    target_geo = geo
                    break

    if target_geo is None:
        # Try finding the geometry from the parent connections of the skin
        # Skin → parent model, geometry → same parent model
        print("  Trying to find geometry through model connections...")
        for geo in geometries:
            geo_parent = child_to_parent.get(geo['id'])
            skin_parent = child_to_parent.get(skin_id)
            if geo_parent and skin_parent and geo_parent == skin_parent:
                target_geo = geo
                print(f"  Found matching geometry: {geo['name']} (both connected to model {geo_parent})")
                break

    if target_geo is None:
        print("  ERROR: Could not find skinned geometry! Using first geometry.")
        target_geo = geometries[0] if geometries else None

    if target_geo is None:
        return None

    num_control_points = len(target_geo['positions'])
    print(f"  Target geometry: {target_geo['name']} ({num_control_points} vertices)")

    # Find clusters connected to this skin deformer
    skin_clusters = []
    for c in connections:
        if c['parent'] == skin_id and c['child'] in clusters:
            skin_clusters.append(clusters[c['child']])

    print(f"  Skin has {len(skin_clusters)} bone clusters")

    # Find which bone model each cluster connects to
    for cluster in skin_clusters:
        cid = cluster['id']
        # Cluster → Model (bone) connection
        for c in connections:
            if c['parent'] == cluster['id'] and c['child'] in models:
                bone_model = models[c['child']]
                cluster['bone_model_id'] = c['child']
                cluster['bone_name'] = bone_model['name']
                break
        if 'bone_name' not in cluster:
            cluster['bone_name'] = cluster['name']

    # Sort clusters by name for consistent bone ordering
    skin_clusters.sort(key=lambda c: c.get('bone_name', ''))

    # Build bone list
    bone_names = []
    bone_ids = {}  # model_id -> bone_index
    for i, cluster in enumerate(skin_clusters):
        bname = cluster.get('bone_name', cluster['name'])
        bone_names.append(bname)
        if 'bone_model_id' in cluster:
            bone_ids[cluster['bone_model_id']] = i
        print(f"    Bone {i}: {bname} ({len(cluster.get('indices', []))} influences)")

    # Build bone hierarchy from connections
    bone_parents = [-1] * len(skin_clusters)
    for i, cluster in enumerate(skin_clusters):
        if 'bone_model_id' not in cluster:
            continue
        bmid = cluster['bone_model_id']
        # Walk up the connection tree to find parent bone
        # In FBX, bone models are connected: child_bone → parent_bone
        if bmid in child_to_parent:
            parent_model_id = child_to_parent[bmid]
            # Check all parents (there can be multiple connection entries)
            for c in connections:
                if c['child'] == bmid and c['type'] == 'OO':
                    pmid = c['parent']
                    if pmid in bone_ids:
                        bone_parents[i] = bone_ids[pmid]
                        break

    # Build inverse bind pose matrices
    # FBX TransformLink = bone's world-space bind pose (may include unit scale, e.g. 100x for cm)
    # We use the cluster's Transform directly as the inverse bind pose.
    # In Blender FBX exports, Transform = mesh_global_at_bind * inv(bone_global_at_bind)
    # which IS the offset matrix that maps vertices from mesh space to bone space.
    # At bind time: finalMatrix = Transform * TransformLink = mesh_global (consistent for all bones)
    #
    # For our engine, we want bind-time result = identity (vertices stay in mesh-local coords).
    # So we use invBindPose = inv(TransformLink), but normalized to unit scale.
    # The TransformLink rotation columns may have magnitude ~100 (Blender cm scale).
    # We extract scale, normalize, and convert translations to meters (/ 100).
    bone_inv_bind_poses = []
    bone_bind_poses = []
    
    # Detect scale from first TransformLink (rotation column magnitude)
    fbx_scale = 1.0
    for cluster in skin_clusters:
        if 'transform_link' in cluster:
            tfl = mat4_from_fbx(cluster['transform_link'])
            # Column 0 magnitude = sqrt(m00^2 + m10^2 + m20^2)
            col0_mag = math.sqrt(tfl[0]**2 + tfl[4]**2 + tfl[8]**2)
            if col0_mag > 1.5:  # Clearly scaled
                fbx_scale = col0_mag
                print(f"  Detected FBX scale factor: {fbx_scale:.2f} (dividing to normalize)")
            break
    
    for cluster in skin_clusters:
        if 'transform_link' in cluster:
            tfl = mat4_from_fbx(cluster['transform_link'])
            # Normalize the bind pose: divide rotation by scale, divide translation by scale
            bind_pose = [
                tfl[0]/fbx_scale,  tfl[1]/fbx_scale,  tfl[2]/fbx_scale,  tfl[3],
                tfl[4]/fbx_scale,  tfl[5]/fbx_scale,  tfl[6]/fbx_scale,  tfl[7],
                tfl[8]/fbx_scale,  tfl[9]/fbx_scale,  tfl[10]/fbx_scale, tfl[11],
                tfl[12]/fbx_scale, tfl[13]/fbx_scale, tfl[14]/fbx_scale, tfl[15],
            ]
            inv_bind_pose = mat4_inverse(bind_pose)
        elif 'bone_model_id' in cluster and cluster['bone_model_id'] in bind_poses:
            bind_pose = mat4_from_fbx(bind_poses[cluster['bone_model_id']])
            inv_bind_pose = mat4_inverse(bind_pose)
        else:
            bind_pose = mat4_identity()
            inv_bind_pose = mat4_identity()

        bone_bind_poses.append(bind_pose)
        bone_inv_bind_poses.append(inv_bind_pose)

    # Build per-vertex bone weights (max 4 per vertex)
    vertex_bones = [[] for _ in range(num_control_points)]
    for bone_idx, cluster in enumerate(skin_clusters):
        indices = cluster.get('indices', [])
        weights = cluster.get('weights', [])
        for vi, w in zip(indices, weights):
            if vi < num_control_points:
                vertex_bones[vi].append((bone_idx, float(w)))

    # Limit to 4 bones per vertex, normalize weights
    for vi in range(num_control_points):
        bones = vertex_bones[vi]
        bones.sort(key=lambda x: -x[1])  # Sort by weight descending
        bones = bones[:4]  # Keep top 4
        total_w = sum(w for _, w in bones)
        if total_w > 0:
            bones = [(bi, w/total_w) for bi, w in bones]
        # Pad to exactly 4
        while len(bones) < 4:
            bones.append((0, 0.0))
        vertex_bones[vi] = bones

    # === Triangulate and build final vertex/index buffers ===
    positions = target_geo['positions']
    normals = target_geo.get('normals', [])
    normal_mapping = target_geo.get('normal_mapping', 'ByPolygonVertex')
    normal_ref = target_geo.get('normal_ref', 'Direct')
    normal_indices = target_geo.get('normal_indices', [])
    uvs = target_geo.get('uvs', [])
    uv_mapping = target_geo.get('uv_mapping', 'ByPolygonVertex')
    uv_ref = target_geo.get('uv_ref', 'Direct')
    uv_indices = target_geo.get('uv_indices', [])

    final_vertices = []  # (pos, normal, uv, bone_indices, bone_weights)
    final_indices = []
    vertex_map = {}  # (vi, ni, ui) -> final vertex index
    polygon_vertex_counter = 0

    for poly in target_geo['polygons']:
        # Triangulate polygon (fan triangulation)
        tri_indices = []
        for i in range(len(poly)):
            vi = poly[i]

            # Get normal
            if normals:
                if normal_mapping == "ByPolygonVertex":
                    if normal_ref == "IndexToDirect" and normal_indices:
                        ni = normal_indices[polygon_vertex_counter]
                    else:
                        ni = polygon_vertex_counter
                elif normal_mapping == "ByVertex" or normal_mapping == "ByVertice":
                    ni = vi
                else:
                    ni = polygon_vertex_counter
                normal = normals[ni] if ni < len(normals) else (0, 1, 0)
            else:
                normal = (0, 1, 0)
                ni = 0

            # Get UV
            if uvs:
                if uv_mapping == "ByPolygonVertex":
                    if uv_ref == "IndexToDirect" and uv_indices:
                        ui = uv_indices[polygon_vertex_counter]
                    else:
                        ui = polygon_vertex_counter
                elif uv_mapping == "ByVertex" or uv_mapping == "ByVertice":
                    ui = vi
                else:
                    ui = polygon_vertex_counter
                uv = uvs[ui] if ui < len(uvs) else (0, 0)
            else:
                uv = (0, 0)
                ui = 0

            # Bone data comes from the control point index
            bone_data = vertex_bones[vi]
            b_indices = tuple(bd[0] for bd in bone_data)
            b_weights = tuple(bd[1] for bd in bone_data)

            # Unique vertex key
            key = (vi, ni, ui)
            if key not in vertex_map:
                vertex_map[key] = len(final_vertices)
                # Scale positions by 1/fbx_scale to convert from cm to normalized units
                scaled_pos = (positions[vi][0] / fbx_scale,
                              positions[vi][1] / fbx_scale,
                              positions[vi][2] / fbx_scale)
                final_vertices.append((
                    scaled_pos,
                    normal,
                    uv,
                    b_indices,
                    b_weights
                ))

            tri_indices.append(vertex_map[key])
            polygon_vertex_counter += 1

        # Fan triangulation
        for i in range(1, len(tri_indices) - 1):
            final_indices.append(tri_indices[0])
            final_indices.append(tri_indices[i])
            final_indices.append(tri_indices[i + 1])

    print(f"  Final mesh: {len(final_vertices)} vertices, {len(final_indices)} indices ({len(final_indices)//3} triangles)")
    print(f"  Bones: {len(skin_clusters)}")

    # === Topological sort: parents must come before children ===
    # Build the bone data first with original indices
    num_bones = len(skin_clusters)
    old_bones = [{
        'name': bone_names[i],
        'parent': bone_parents[i],
        'inv_bind_pose': bone_inv_bind_poses[i],
        'bind_pose': bone_bind_poses[i],
    } for i in range(num_bones)]

    # Topological sort using BFS (Kahn's algorithm)
    # Roots first, then their children, etc.
    children = [[] for _ in range(num_bones)]
    roots = []
    for i in range(num_bones):
        p = bone_parents[i]
        if p < 0 or p >= num_bones:
            roots.append(i)
        else:
            children[p].append(i)

    sorted_order = []  # old index -> position in sorted_order
    queue = list(roots)
    while queue:
        idx = queue.pop(0)
        sorted_order.append(idx)
        for child in children[idx]:
            queue.append(child)

    assert len(sorted_order) == num_bones, f"Topological sort failed: {len(sorted_order)} != {num_bones}"

    # Build old-to-new index mapping
    old_to_new = [0] * num_bones
    for new_idx, old_idx in enumerate(sorted_order):
        old_to_new[old_idx] = new_idx

    # Remap bones
    sorted_bones = []
    for new_idx, old_idx in enumerate(sorted_order):
        bone = old_bones[old_idx].copy()
        old_parent = bone['parent']
        bone['parent'] = old_to_new[old_parent] if (0 <= old_parent < num_bones) else -1
        sorted_bones.append(bone)

    # Remap vertex bone indices
    for vi in range(len(final_vertices)):
        pos, normal, uv, b_indices, b_weights = final_vertices[vi]
        remapped = tuple(old_to_new[bi] if bi < num_bones else 0 for bi in b_indices)
        final_vertices[vi] = (pos, normal, uv, remapped, b_weights)

    # Print sorted hierarchy
    print("  Bone order (topologically sorted):")
    for i, bone in enumerate(sorted_bones):
        print(f"    Bone {i}: {bone['name']} (parent={bone['parent']})")

    return {
        'vertices': final_vertices,
        'indices': final_indices,
        'bones': sorted_bones,
    }


# ============================================================
# Also merge the second (sleeve) mesh if present
# ============================================================

def build_merged_mesh(root):
    """Build skinned mesh merging all geometries. Non-skinned meshes get
    assigned to nearest bone based on their model parent."""

    # First get the skinned mesh
    result = build_skinned_mesh(root)
    if result is None:
        return None

    # Get all geometries and find non-skinned ones
    geometries = extract_all_geometries(root)
    connections = extract_connections(root)
    models = extract_models(root)
    skin_deformers, clusters = extract_deformers(root)

    # Find skin geometry ID
    skinned_geo_ids = set()
    for c in connections:
        if c['child'] in skin_deformers:
            skinned_geo_ids.add(c['parent'])

    # Find non-skinned geometries
    non_skinned = [g for g in geometries if g['id'] not in skinned_geo_ids]

    if not non_skinned:
        return result

    print(f"\n  Merging {len(non_skinned)} additional mesh(es)...")

    # For non-skinned meshes, assign all vertices to the forearm bones (for sleeves)
    # Find forearm bone indices
    forearm_l_idx = -1
    forearm_r_idx = -1
    for i, bone in enumerate(result['bones']):
        if 'forearm.L' in bone['name']:
            forearm_l_idx = i
        elif 'forearm.R' in bone['name']:
            forearm_r_idx = i

    if forearm_l_idx < 0:
        forearm_l_idx = 0
    if forearm_r_idx < 0:
        forearm_r_idx = 0

    print(f"  Assigning sleeves to forearm bones: L={forearm_l_idx}, R={forearm_r_idx}")

    existing_verts = len(result['vertices'])
    existing_indices = len(result['indices'])

    for geo in non_skinned:
        positions = geo['positions']
        normals_data = geo.get('normals', [])
        normal_mapping = geo.get('normal_mapping', 'ByPolygonVertex')
        normal_ref = geo.get('normal_ref', 'Direct')
        normal_indices = geo.get('normal_indices', [])
        uvs = geo.get('uvs', [])
        uv_mapping = geo.get('uv_mapping', 'ByPolygonVertex')
        uv_ref = geo.get('uv_ref', 'Direct')
        uv_indices = geo.get('uv_indices', [])

        new_verts = []
        new_indices = []
        vertex_map = {}
        pvc = 0

        for poly in geo['polygons']:
            tri_idx = []
            for i in range(len(poly)):
                vi = poly[i]
                pos = positions[vi]

                # Normal
                if normals_data:
                    if normal_mapping == "ByPolygonVertex":
                        ni = normal_indices[pvc] if (normal_ref == "IndexToDirect" and normal_indices) else pvc
                    else:
                        ni = vi
                    normal = normals_data[ni] if ni < len(normals_data) else (0,1,0)
                else:
                    normal = (0,1,0)
                    ni = 0

                # UV
                if uvs:
                    if uv_mapping == "ByPolygonVertex":
                        ui = uv_indices[pvc] if (uv_ref == "IndexToDirect" and uv_indices) else pvc
                    else:
                        ui = vi
                    uv = uvs[ui] if ui < len(uvs) else (0,0)
                else:
                    uv = (0,0)
                    ui = 0

                # Assign to left or right forearm based on X position
                if pos[0] >= 0:
                    bone_idx = forearm_l_idx
                else:
                    bone_idx = forearm_r_idx

                key = (vi, ni, ui)
                if key not in vertex_map:
                    vertex_map[key] = len(new_verts) + existing_verts
                    new_verts.append((
                        pos, normal, uv,
                        (bone_idx, 0, 0, 0),
                        (1.0, 0.0, 0.0, 0.0)
                    ))
                tri_idx.append(vertex_map[key])
                pvc += 1

            for i in range(1, len(tri_idx) - 1):
                new_indices.append(tri_idx[0])
                new_indices.append(tri_idx[i])
                new_indices.append(tri_idx[i+1])

        result['vertices'].extend(new_verts)
        result['indices'].extend(new_indices)
        existing_verts += len(new_verts)
        print(f"  Added {geo['name']}: {len(new_verts)} verts, {len(new_indices)//3} tris")

    print(f"  Merged total: {len(result['vertices'])} vertices, {len(result['indices'])//3} triangles")
    return result


# ============================================================
# Binary Output (.skmesh)
# ============================================================

def write_skmesh(data, output_path):
    """Write skinned mesh to binary .skmesh file.

    Format:
    [4]  magic "SMSH"
    [4]  uint32 version (1)
    [4]  uint32 numVertices
    [4]  uint32 numIndices
    [4]  uint32 numBones
    [numVerts * 52] vertex data:
        float3 position  (12)
        float3 normal    (12)
        float2 texcoord  (8)
        uint8[4] boneIndices (4)
        float4 boneWeights (16)
    [numIndices * 4] uint32 indices
    [numBones * variable] bone data:
        uint8 nameLen
        char[nameLen] name
        int32 parentIndex
        float[16] inverseBindPose (row-major)
        float[16] bindPose (row-major)
    """
    with open(output_path, 'wb') as f:
        vertices = data['vertices']
        indices = data['indices']
        bones = data['bones']

        # Header
        f.write(b'SMSH')
        f.write(struct.pack('<I', 1))  # version
        f.write(struct.pack('<I', len(vertices)))
        f.write(struct.pack('<I', len(indices)))
        f.write(struct.pack('<I', len(bones)))

        # Vertices
        for pos, normal, uv, bi, bw in vertices:
            f.write(struct.pack('<fff', pos[0], pos[1], pos[2]))
            f.write(struct.pack('<fff', normal[0], normal[1], normal[2]))
            f.write(struct.pack('<ff', uv[0], uv[1]))
            f.write(struct.pack('<BBBB', min(bi[0],255), min(bi[1],255), min(bi[2],255), min(bi[3],255)))
            f.write(struct.pack('<ffff', bw[0], bw[1], bw[2], bw[3]))

        # Indices
        for idx in indices:
            f.write(struct.pack('<I', idx))

        # Bones
        for bone in bones:
            name_bytes = bone['name'].encode('utf-8')
            f.write(struct.pack('<B', len(name_bytes)))
            f.write(name_bytes)
            f.write(struct.pack('<i', bone['parent']))
            # Inverse bind pose (16 floats, row-major)
            for v in bone['inv_bind_pose']:
                f.write(struct.pack('<f', v))
            # Bind pose (16 floats, row-major)
            for v in bone['bind_pose']:
                f.write(struct.pack('<f', v))

    file_size = os.path.getsize(output_path)
    print(f"  Written: {output_path} ({file_size} bytes)")


# ============================================================
# Main
# ============================================================

def main():
    if len(sys.argv) < 2:
        print("Usage: python export_skinned_mesh.py <input.fbx> [output.skmesh]")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(input_path)[0] + ".skmesh"

    print(f"Exporting skinned mesh from: {input_path}")
    print(f"Output: {output_path}")

    root, version = read_fbx_binary(input_path)
    print(f"  FBX version: {version}")

    # Build merged mesh (arms + sleeves)
    data = build_merged_mesh(root)
    if data is None:
        print("ERROR: Failed to extract mesh data!")
        sys.exit(1)

    write_skmesh(data, output_path)
    print("Done!")


if __name__ == "__main__":
    main()
