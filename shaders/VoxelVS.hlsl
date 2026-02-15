// Voxel Vertex Shader — uses Common.hlsli CB layout
#include "Common.hlsli"

VoxelPSInput VSMain(VoxelVSInput input) {
    VoxelPSInput output;

    float4 worldPos = mul(float4(input.Position, 1.0f), gWorld);
    output.WorldPos = worldPos.xyz;
    output.Position = mul(worldPos, gViewProjection);

    // Transform normal with inverse-transpose for correct non-uniform scaling
    output.Normal = normalize(mul(float4(input.Normal, 0.0f), gWorldInvTranspose).xyz);
    output.Color  = input.Color;
    output.TexCoord = input.TexCoord;

    return output;
}
