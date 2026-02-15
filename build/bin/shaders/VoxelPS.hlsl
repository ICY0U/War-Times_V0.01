// Voxel Pixel Shader — Blinn-Phong or Cel-shaded lighting with fog and tone mapping
#include "Common.hlsli"

float4 PSMain(VoxelPSInput input) : SV_TARGET {
    // Sample diffuse texture (white 1x1 when no texture bound = no effect)
    float3 texColor = gDiffuseTexture.Sample(gSamplerLinear, input.TexCoord).rgb;

    float3 baseColor = (gObjectColor.a > 0.0f) ? gObjectColor.rgb : input.Color.rgb;
    float3 albedo = baseColor * texColor;

    // Choose lighting model
    float3 litColor;
    if (gCelEnabled > 0.5f) {
        litColor = CalcCelShadowed(input.Normal, input.WorldPos, albedo, 64.0f);
    } else {
        litColor = CalcBlinnPhongShadowed(input.Normal, input.WorldPos, albedo, 32.0f);
    }

    // Distance fog
    litColor = ApplyFog(litColor, input.WorldPos);

    // Tone mapping for HDR-like feel
    litColor = ToneMapReinhard(litColor);

    return float4(litColor, input.Color.a);
}
