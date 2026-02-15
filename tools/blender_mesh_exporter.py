# ============================================================
# War Times .mesh Exporter for Blender 3.x / 4.x
# ============================================================
# Export selected meshes as .mesh binary files ready for D3D11.
#
# Install:  Edit > Preferences > Add-ons > Install > select this .py
#           Enable "Import-Export: War Times Mesh Exporter"
# Usage:    File > Export > War Times Mesh (.mesh)
#           Or select objects and export; only mesh objects are exported.
#
# Coordinate conversion (Blender → D3D11):
#   Blender: right-handed, Z-up, CCW front faces
#   D3D11:   left-handed,  Y-up, CW  front faces
#
#   Position:  (x, y, z) → (x, z, -y)
#   Normal:    (x, y, z) → (x, z, -y)
#   UV:        (u, v) → (u, 1-v)        (V flipped)
#   Winding:   reverse triangle indices  (CCW → CW)
# ============================================================

bl_info = {
    "name": "War Times Mesh Exporter",
    "author": "War Times Dev",
    "version": (1, 0, 0),
    "blender": (3, 0, 0),
    "location": "File > Export > War Times Mesh (.mesh)",
    "description": "Export meshes as .mesh binary for the War Times engine (D3D11)",
    "category": "Import-Export",
}

import bpy
import struct
import os
import bmesh
from bpy_extras.io_utils import ExportHelper
from bpy.props import BoolProperty, FloatProperty, StringProperty
from mathutils import Matrix, Vector


def triangulate_mesh(mesh_data):
    """Triangulate a mesh using bmesh (non-destructive)."""
    bm = bmesh.new()
    bm.from_mesh(mesh_data)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.to_mesh(mesh_data)
    bm.free()


def export_mesh_object(context, obj, filepath, apply_modifiers, default_color):
    """Export a single mesh object to a .mesh binary file."""

    # Get evaluated mesh (with modifiers applied if requested)
    if apply_modifiers:
        depsgraph = context.evaluated_depsgraph_get()
        eval_obj = obj.evaluated_get(depsgraph)
        mesh = eval_obj.to_mesh()
    else:
        mesh = obj.to_mesh()

    if mesh is None:
        return False, "Could not get mesh data"

    # Triangulate
    triangulate_mesh(mesh)

    # Ensure we have loop normals
    mesh.calc_loop_triangles()
    if hasattr(mesh, 'calc_normals_split'):
        mesh.calc_normals_split()

    # Get vertex color layer (if any)
    color_layer = None
    if mesh.color_attributes:
        color_layer = mesh.color_attributes.active_color
    elif mesh.vertex_colors:
        color_layer = mesh.vertex_colors.active

    # Get UV layer (if any)
    uv_layer = None
    if mesh.uv_layers:
        uv_layer = mesh.uv_layers.active

    # Get object transform matrix and its normal matrix (inverse transpose of 3x3)
    obj_matrix = obj.matrix_world
    # Normal matrix = inverse transpose of the upper-left 3x3
    norm_matrix = obj_matrix.to_3x3().inverted_safe().transposed()
    # Check if the transform flips winding (negative determinant = odd number of negative scales)
    flip_winding = obj_matrix.determinant() < 0

    # Build vertex data by iterating loop triangles
    # Each unique (position, normal, color, uv) combination becomes a vertex
    vertex_map = {}   # (pos_tuple, norm_tuple, color_tuple, uv_tuple) → index
    vertices = []     # list of (pos, norm, color, uv) tuples
    indices = []      # triangle indices

    for tri in mesh.loop_triangles:
        tri_indices = []
        for loop_index in tri.loops:
            loop = mesh.loops[loop_index]
            vert = mesh.vertices[loop.vertex_index]

            # Apply object transform to position, THEN convert coordinates
            # Blender (x,y,z) → world space → D3D11 (x, z, -y)
            world_pos = obj_matrix @ vert.co
            px, py, pz = world_pos.x, world_pos.z, -world_pos.y

            # Apply object normal matrix, normalize, then convert coordinates
            if tri.use_smooth:
                raw_norm = loop.normal  # per-loop split normal
            else:
                raw_norm = tri.normal   # flat face normal
            world_norm = (norm_matrix @ Vector(raw_norm)).normalized()
            nx, ny, nz = world_norm.x, world_norm.z, -world_norm.y

            # Vertex color
            if color_layer is not None:
                if hasattr(color_layer, 'data') and len(color_layer.data) > loop_index:
                    c = color_layer.data[loop_index].color
                    cr, cg, cb, ca = c[0], c[1], c[2], c[3] if len(c) > 3 else 1.0
                else:
                    cr, cg, cb, ca = default_color
            else:
                cr, cg, cb, ca = default_color

            # UV: flip V axis
            if uv_layer is not None:
                uv = uv_layer.data[loop_index].uv
                u, v = uv.x, 1.0 - uv.y
            else:
                u, v = 0.0, 0.0

            # Create unique vertex key (quantized for floating point matching)
            def q(f): return round(f, 6)
            key = (q(px), q(py), q(pz),
                   q(nx), q(ny), q(nz),
                   q(cr), q(cg), q(cb), q(ca),
                   q(u), q(v))

            if key in vertex_map:
                tri_indices.append(vertex_map[key])
            else:
                idx = len(vertices)
                vertex_map[key] = idx
                vertices.append((px, py, pz, nx, ny, nz, cr, cg, cb, ca, u, v))
                tri_indices.append(idx)

        # Reverse winding order: CCW (Blender) → CW (D3D11)
        # If object transform has negative determinant (mirrored), don't reverse (double-negative)
        if flip_winding:
            indices.extend([tri_indices[0], tri_indices[1], tri_indices[2]])
        else:
            indices.extend([tri_indices[0], tri_indices[2], tri_indices[1]])

    # Clean up temporary mesh
    if apply_modifiers:
        eval_obj.to_mesh_clear()
    else:
        obj.to_mesh_clear()

    if len(vertices) == 0 or len(indices) == 0:
        return False, "Mesh has no geometry"

    # ---- Write binary file ----
    vertex_count = len(vertices)
    index_count = len(indices)

    os.makedirs(os.path.dirname(filepath), exist_ok=True)

    with open(filepath, 'wb') as f:
        # Header: magic, version, vertex count, index count
        f.write(struct.pack('<I', 0x4853454D))   # "MESH"
        f.write(struct.pack('<I', 1))             # version
        f.write(struct.pack('<I', vertex_count))
        f.write(struct.pack('<I', index_count))

        # Vertices: pos(3f) + normal(3f) + color(4f) + uv(2f) = 12 floats = 48 bytes
        for v in vertices:
            f.write(struct.pack('<12f', *v))

        # Indices: uint32
        for i in indices:
            f.write(struct.pack('<I', i))

    return True, f"Exported {vertex_count} verts, {index_count // 3} tris"


class WARTIMES_OT_export_mesh(bpy.types.Operator, ExportHelper):
    """Export selected objects as War Times .mesh binary files"""
    bl_idname = "export_mesh.wartimes"
    bl_label = "Export War Times Mesh"
    bl_options = {'PRESET'}

    filename_ext = ".mesh"

    filter_glob: StringProperty(
        default="*.mesh",
        options={'HIDDEN'},
    )

    apply_modifiers: BoolProperty(
        name="Apply Modifiers",
        description="Apply all modifiers before exporting",
        default=True,
    )

    export_selected: BoolProperty(
        name="Selected Only",
        description="Export only selected mesh objects (otherwise all visible meshes)",
        default=True,
    )

    batch_export: BoolProperty(
        name="Batch Export",
        description="Export each object as a separate .mesh file (filename = object name)",
        default=False,
    )

    default_color_r: FloatProperty(name="Default R", default=0.8, min=0.0, max=1.0)
    default_color_g: FloatProperty(name="Default G", default=0.8, min=0.0, max=1.0)
    default_color_b: FloatProperty(name="Default B", default=0.8, min=0.0, max=1.0)
    default_color_a: FloatProperty(name="Default A", default=1.0, min=0.0, max=1.0)

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "apply_modifiers")
        layout.prop(self, "export_selected")
        layout.prop(self, "batch_export")
        layout.separator()
        layout.label(text="Default Vertex Color (when no color attribute):")
        row = layout.row()
        row.prop(self, "default_color_r", text="R")
        row.prop(self, "default_color_g", text="G")
        row.prop(self, "default_color_b", text="B")
        row.prop(self, "default_color_a", text="A")

    def execute(self, context):
        default_color = (self.default_color_r, self.default_color_g,
                         self.default_color_b, self.default_color_a)

        # Gather mesh objects
        if self.export_selected:
            objects = [o for o in context.selected_objects if o.type == 'MESH']
        else:
            objects = [o for o in context.view_layer.objects if o.type == 'MESH' and o.visible_get()]

        if not objects:
            self.report({'WARNING'}, "No mesh objects to export")
            return {'CANCELLED'}

        if self.batch_export:
            # Export each object as separate file
            base_dir = os.path.dirname(self.filepath)
            exported = 0
            for obj in objects:
                # Sanitize name for filename
                safe_name = obj.name.replace(' ', '_')
                filepath = os.path.join(base_dir, safe_name + ".mesh")
                ok, msg = export_mesh_object(context, obj, filepath,
                                             self.apply_modifiers, default_color)
                if ok:
                    exported += 1
                    self.report({'INFO'}, f"{safe_name}: {msg}")
                else:
                    self.report({'WARNING'}, f"{safe_name}: {msg}")

            self.report({'INFO'}, f"Batch exported {exported}/{len(objects)} meshes")
        else:
            # Export first selected (or all joined as one — just export the active/first)
            if len(objects) == 1:
                obj = objects[0]
            elif context.active_object and context.active_object.type == 'MESH':
                obj = context.active_object
            else:
                obj = objects[0]

            ok, msg = export_mesh_object(context, obj, self.filepath,
                                         self.apply_modifiers, default_color)
            if ok:
                self.report({'INFO'}, msg)
            else:
                self.report({'ERROR'}, msg)
                return {'CANCELLED'}

        return {'FINISHED'}


def menu_func_export(self, context):
    self.layout.operator(WARTIMES_OT_export_mesh.bl_idname,
                         text="War Times Mesh (.mesh)")


def register():
    bpy.utils.register_class(WARTIMES_OT_export_mesh)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    bpy.utils.unregister_class(WARTIMES_OT_export_mesh)


if __name__ == "__main__":
    register()
