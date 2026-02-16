// Voxel Pixel Shader — Blinn-Phong or Cel-shaded lighting with fog and tone mapping
#include "Common.hlsli"

float4 PSMain(VoxelPSInput input) : SV_TARGET {
    // Determine UV coordinates
    float2 uv = input.TexCoord;

    // When ObjectColor.a > 0, this is a level entity — use world-space UVs for proper tiling
    if (gObjectColor.a > 0.0f) {
        // Blended triplanar mapping: smoothly blend all three projections
        // to avoid hard seams and texture popping at angled surfaces
        float3 absN = abs(input.Normal);
        // Sharpen blend weights so dominant axis wins clearly, but transitions are smooth
        float3 blend = pow(absN, 4.0f);
        blend /= (blend.x + blend.y + blend.z + 0.0001f);  // normalize to sum=1

        float2 uvX = input.WorldPos.yz;   // project onto YZ for X-facing
        float2 uvY = input.WorldPos.xz;   // project onto XZ for Y-facing
        float2 uvZ = input.WorldPos.xy;   // project onto XY for Z-facing

        uv = uvX * blend.x + uvY * blend.y + uvZ * blend.z;
    }

    // Sample diffuse texture (white 1x1 when no texture bound = no effect)
    float3 texColor = gDiffuseTexture.Sample(gSamplerLinear, uv).rgb;

    float3 baseColor = (gObjectColor.a > 0.0f) ? gObjectColor.rgb : input.Color.rgb;
    float3 albedo = baseColor * texColor;

    // Hit decal darkening — bullet scars burn dark spots on surfaces
    int decalCount = (int)gHitDecalCount;
    for (int di = 0; di < decalCount; di++) {
        float3 decalPos = gHitDecals[di].xyz;
        float  intensity = gHitDecals[di].w;
        float  dist = length(input.WorldPos - decalPos);
        float  radius = 0.35f;  // decal radius in world units
        float  falloff = 1.0f - saturate(dist / radius);
        falloff = falloff * falloff;  // quadratic falloff for softer edges
        albedo *= lerp(1.0f, 0.2f, falloff * intensity);  // darken up to 80%
    }

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
