import struct, sys, os
sys.path.insert(0, 'tools')
from export_skinned_mesh import *

root, version = read_fbx_binary('models/Character/FPSARmsRIG.fbx')
skin_deformers, clusters = extract_deformers(root)

# Print the Transform and TransformLink from the first few clusters
print('Cluster transforms:')
count = 0
for cid, c in clusters.items():
    if count >= 3:
        break
    count += 1
    name = c.get('name', '?')
    print(f'\nCluster: {name} (id={cid})')
    if 'transform' in c:
        tf = mat4_from_fbx(c['transform'])
        print('  Transform (mesh-to-bone):')
        for row in range(4):
            print(f'    [{tf[row*4]:.4f}, {tf[row*4+1]:.4f}, {tf[row*4+2]:.4f}, {tf[row*4+3]:.4f}]')
    if 'transform_link' in c:
        tfl = mat4_from_fbx(c['transform_link'])
        print('  TransformLink (bone world):')
        for row in range(4):
            print(f'    [{tfl[row*4]:.4f}, {tfl[row*4+1]:.4f}, {tfl[row*4+2]:.4f}, {tfl[row*4+3]:.4f}]')
    
    # Check what Transform * inverse(TransformLink) looks like
    if 'transform' in c and 'transform_link' in c:
        tf = mat4_from_fbx(c['transform'])
        tfl = mat4_from_fbx(c['transform_link'])
        inv_tfl = mat4_inverse(tfl)
        combined = mat4_multiply(tf, inv_tfl)
        print('  Transform * inv(TransformLink):')
        for row in range(4):
            print(f'    [{combined[row*4]:.6f}, {combined[row*4+1]:.6f}, {combined[row*4+2]:.6f}, {combined[row*4+3]:.6f}]')
