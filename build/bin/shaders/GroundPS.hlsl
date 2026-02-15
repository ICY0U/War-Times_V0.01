// Ground Plane Pixel Shader — Procedural checkerboard with lighting and fog
#include "Common.hlsli"

float4 PSMain(VoxelPSInput input) : SV_TARGET {
    float3 worldPos = input.WorldPos;

    // Procedural checkerboard pattern
    float scale = 1.0f;  // 1 unit per tile
    float2 uv = worldPos.xz * scale;
    
    // Anti-aliased checkerboard using fwidth
    float2 fw = fwidth(uv);
    float2 p = frac(uv) - 0.5f;
    float2 aa = smoothstep(-fw * 0.5f, fw * 0.5f, p) - smoothstep(0.5f - fw * 0.5f, 0.5f + fw * 0.5f, abs(p));
    float checker = (aa.x + aa.y) * 0.5f;
    
    // Simpler fallback: crisp checker
    float2 fl = floor(uv);
    float check2 = fmod(abs(fl.x) + abs(fl.y), 2.0f);
    
    // Blend between sharp and AA based on distance
    float dist = length(worldPos - gCameraPosition);
    float blend = saturate(dist / 30.0f);
    float pattern = lerp(check2, 0.5f, blend);  // Fade to average at distance
    
    // Base colors - subtle dark/light variation
    float3 colorA = input.Color.rgb * 0.85f;  // Darker tile
    float3 colorB = input.Color.rgb * 1.05f;  // Lighter tile
    float3 albedo = lerp(colorA, colorB, pattern);

    // Grid lines at tile edges (subtle)
    float2 gridUV = frac(uv);
    float2 gridFW = fwidth(uv);
    float lineWidth = 0.02f;
    float2 grid = smoothstep(lineWidth + gridFW, lineWidth, gridUV) + 
                  smoothstep(1.0f - lineWidth - gridFW, 1.0f - lineWidth, gridUV);
    float gridLine = max(grid.x, grid.y);
    gridLine *= saturate(1.0f - dist / 50.0f);  // Fade grid lines at distance
    albedo = lerp(albedo, albedo * 0.6f, gridLine * 0.3f);

    // Ground lighting — diffuse + ambient only (no specular, ground isn't glossy)
    float3 N = normalize(input.Normal);
    float3 L = normalize(-gSunDirection);
    float NdotL = max(dot(N, L), 0.0f);
    float shadow = CalcShadow(worldPos, N);

    float3 litColor;
    if (gCelEnabled > 0.5f) {
        float bands = max(gCelBands, 2.0f);
        float lit = NdotL * shadow;
        float stepped = floor(lit * bands + 0.5f) / bands;
        float3 diffuse = gSunColor * gSunIntensity * stepped;
        float3 ambient = gAmbientColor * gAmbientIntensity;
        litColor = albedo * (ambient + diffuse);
    } else {
        float3 diffuse = gSunColor * gSunIntensity * NdotL * shadow;
        float3 ambient = gAmbientColor * gAmbientIntensity;
        litColor = albedo * (ambient + diffuse);
    }

    // Distance fog
    litColor = ApplyFog(litColor, worldPos);

    // Tone mapping
    litColor = ToneMapReinhard(litColor);

    return float4(litColor, input.Color.a);
}
