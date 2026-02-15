"""Inspect FBX files using assimp_py to understand structure."""
import assimp_py
import os, sys

def inspect_file(path):
    print(f"\n{'='*60}")
    print(f"FILE: {os.path.basename(path)} ({os.path.getsize(path)} bytes)")
    print(f"{'='*60}")
    
    scene = assimp_py.ImportFile(path, 
        assimp_py.Process_Triangulate | 
        assimp_py.Process_GenNormals |
        assimp_py.Process_LimitBoneWeights)
    
    # Node hierarchy
    print("\n--- NODE HIERARCHY ---")
    def print_node(node, depth=0):
        name = node.name if node.name else "(unnamed)"
        print(f"{'  '*depth}{name}  children={len(node.children)}  meshes={len(node.meshes)}")
        for c in node.children:
            print_node(c, depth+1)
    print_node(scene.root_node)
    
    # Meshes
    print(f"\n--- MESHES ({len(scene.meshes)}) ---")
    for i, mesh in enumerate(scene.meshes):
        print(f"  Mesh[{i}]: name='{mesh.name}' verts={len(mesh.vertices)//3} faces={len(mesh.indices)//3}")
        print(f"    has_normals={mesh.normals is not None and len(mesh.normals)>0}")
        print(f"    has_texcoords={mesh.texcoords is not None and len(mesh.texcoords)>0}")
        print(f"    num_bones={len(mesh.bones) if mesh.bones else 0}")
        if mesh.bones:
            for j, bone in enumerate(mesh.bones):
                weights_count = len(bone.weights) if bone.weights else 0
                print(f"      Bone[{j}]: '{bone.name}' weights={weights_count}")
                # Print offset matrix
                if bone.offset_matrix:
                    m = bone.offset_matrix
                    print(f"        offset: [{m[0]:.3f},{m[1]:.3f},{m[2]:.3f},{m[3]:.3f}]")
                    print(f"                [{m[4]:.3f},{m[5]:.3f},{m[6]:.3f},{m[7]:.3f}]")
                    print(f"                [{m[8]:.3f},{m[9]:.3f},{m[10]:.3f},{m[11]:.3f}]")
                    print(f"                [{m[12]:.3f},{m[13]:.3f},{m[14]:.3f},{m[15]:.3f}]")
        
        # Vertex bounds
        verts = mesh.vertices
        xs = verts[0::3]
        ys = verts[1::3]
        zs = verts[2::3]
        print(f"    bounds: X[{min(xs):.2f}, {max(xs):.2f}] Y[{min(ys):.2f}, {max(ys):.2f}] Z[{min(zs):.2f}, {max(zs):.2f}]")
    
    # Materials
    print(f"\n--- MATERIALS ({len(scene.materials)}) ---")
    for i, mat in enumerate(scene.materials):
        print(f"  Material[{i}]: {mat}")
    
    # Animations
    print(f"\n--- ANIMATIONS ({len(scene.animations)}) ---")
    for i, anim in enumerate(scene.animations):
        print(f"  Animation[{i}]: name='{anim.name}'")
        print(f"    duration={anim.duration} ticks_per_sec={anim.ticks_per_second}")
        print(f"    num_channels={len(anim.channels)}")
        for j, ch in enumerate(anim.channels):
            print(f"      Channel[{j}]: bone='{ch.name}' pos_keys={len(ch.position_keys)//4} rot_keys={len(ch.rotation_keys)//5} scale_keys={len(ch.scaling_keys)//4}")

base = r"D:\D_Documents\game\2026\War Times_V0.01\models\Character"
for fname in ["Low_P_Bot_0201.fbx", "Walking.fbx"]:
    fpath = os.path.join(base, fname)
    if os.path.exists(fpath):
        inspect_file(fpath)
    else:
        print(f"NOT FOUND: {fpath}")
