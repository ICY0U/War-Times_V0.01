// Debug Line Pixel Shader — simple color pass-through
#include "Common.hlsli"

float4 PSMain(DebugPSInput input) : SV_TARGET {
    return input.Color;
}
