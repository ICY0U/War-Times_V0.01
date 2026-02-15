"""Quick inspect of FBX with trimesh."""
import trimesh
import os

base = r"D:\D_Documents\game\2026\War Times_V0.01\models\Character"
fpath = os.path.join(base, "Low_P_Bot_0201.fbx")

print("Loading model FBX...")
s = trimesh.load(fpath)
print(f"Type: {type(s).__name__}")

if hasattr(s, 'geometry'):
    print(f"Scene with {len(s.geometry)} geometries:")
    for name, geom in s.geometry.items():
        print(f"  {name}: verts={len(geom.vertices)} faces={len(geom.faces)}")
        print(f"    bounds: {geom.bounds}")
elif hasattr(s, 'vertices'):
    print(f"Single mesh: verts={len(s.vertices)} faces={len(s.faces)}")
    print(f"  bounds: {s.bounds}")

if hasattr(s, 'graph'):
    print(f"\nScene graph nodes: {list(s.graph.nodes)[:20]}")
    print(f"Graph transforms count: {len(s.graph.transforms.edge_data) if hasattr(s.graph, 'transforms') else 'N/A'}")
