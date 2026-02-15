// Sky Vertex Shader — Fullscreen triangle (no vertex buffer needed)
// Uses SV_VertexID to generate 3 vertices covering entire screen

#include "Common.hlsli"

struct SkyVSOutput {
    float4 Position : SV_POSITION;
    float3 ViewDir  : TEXCOORD0;
};

SkyVSOutput VSMain(uint vertexID : SV_VertexID) {
    SkyVSOutput output;

    // Generate fullscreen triangle from vertex ID (0, 1, 2)
    // This covers the entire screen with a single triangle
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    float4 clipPos = float4(uv * 2.0f - 1.0f, 1.0f, 1.0f);
    
    // Flip Y for DirectX coordinate system
    clipPos.y = -clipPos.y;
    
    output.Position = clipPos;

    // Reconstruct view direction from clip-space position
    // Multiply by inverse view-projection to get world-space ray direction
    float4 worldPos = mul(clipPos, gInvViewProjection);
    output.ViewDir = worldPos.xyz / worldPos.w - gCameraPosition;

    return output;
}
