// Shadow Depth Vertex Shader — only outputs position in light space
#include "Common.hlsli"

struct ShadowVSOutput {
    float4 Position : SV_POSITION;
};

ShadowVSOutput VSMain(VoxelVSInput input) {
    ShadowVSOutput output;
    float4 worldPos = mul(float4(input.Position, 1.0f), gWorld);
    output.Position = mul(worldPos, gLightViewProjection);
    return output;
}
