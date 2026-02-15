// Debug Line Vertex Shader — transforms world-space lines by ViewProjection
#include "Common.hlsli"

DebugPSInput VSMain(DebugVSInput input) {
    DebugPSInput output;
    output.Position = mul(float4(input.Position, 1.0f), gViewProjection);
    output.Color = input.Color;
    return output;
}
