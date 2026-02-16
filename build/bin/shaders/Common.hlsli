// ============================================================
// Common.hlsli — Shared definitions for War Times Engine
// Constant buffers, vertex structures, and utility functions
// ============================================================

#ifndef COMMON_HLSLI
#define COMMON_HLSLI

// ============================================================
// Constant Buffers
// ============================================================

// Per-Frame data — set once per frame (b0, VS + PS)
cbuffer CBPerFrame : register(b0) {
    float4x4 gView;
    float4x4 gProjection;
    float4x4 gViewProjection;
    float4x4 gInvViewProjection;
    float3   gCameraPosition;
    float    gTime;
    float2   gScreenSize;
    float    gNearZ;
    float    gFarZ;
};

// Per-Object data — set per draw call (b1, VS + PS)
cbuffer CBPerObject : register(b1) {
    float4x4 gWorld;
    float4x4 gWorldInvTranspose;
    float4   gObjectColor;       // If alpha > 0, overrides vertex color
    float4   gHitDecals[4];      // xyz = world position, w = intensity (0..1)
    float    gHitDecalCount;     // Number of active hit decals (0-4)
    float3   _objPad1;           // alignment padding
};

// Lighting data — set once per frame (b2, PS)
cbuffer CBLighting : register(b2) {
    float3 gSunDirection;
    float  gSunIntensity;
    float3 gSunColor;
    float  gCelBands;          // Number of shading bands (2-5), 0 = smooth
    float3 gAmbientColor;
    float  gAmbientIntensity;
    float3 gFogColor;
    float  gFogDensity;
    float  gFogStart;
    float  gFogEnd;
    float  gCelEnabled;        // 0 = Blinn-Phong, >0.5 = cel-shaded
    float  gCelRimIntensity;   // Rim/fresnel highlight strength
};

// Sky data — set once per frame (b3, VS + PS)
cbuffer CBSky : register(b3) {
    float3 gSkyZenithColor;
    float  gSkyBrightness;
    float3 gSkyHorizonColor;
    float  gSkyHorizonFalloff;
    float3 gSkyGroundColor;
    float  gSunDiscSize;        // cosine angle threshold (e.g. 0.9998)
    float  gSunGlowIntensity;
    float  gSunGlowFalloff;
    float  gCloudCoverage;      // 0 = clear, 1 = overcast
    float  gCloudSpeed;         // animation speed multiplier
    float  gCloudDensity;       // edge sharpness
    float  gCloudHeight;        // vertical position in sky dome
    float3 gCloudColor;         // base cloud color
    float  gCloudSunInfluence;  // how much sun lights cloud edges
};

// Shadow data — set once per frame (b4, VS + PS)
cbuffer CBShadow : register(b4) {
    float4x4 gLightViewProjection;
    float    gShadowBias;
    float    gShadowNormalBias;
    float    gShadowIntensity;  // 0 = no shadow, 1 = full shadow
    float    gShadowMapSize;    // resolution for texel size calc
};

// ============================================================
// Textures & Samplers
// ============================================================

// Shadow map (t0 in PS)
Texture2D    gShadowMap : register(t0);
SamplerComparisonState gShadowSampler : register(s3);

// Diffuse texture (t1 in PS) — bound per model, white 1x1 if untextured
Texture2D    gDiffuseTexture : register(t1);
// Samplers (created by Renderer: s0=point, s1=linear, s2=aniso)
SamplerState gSamplerLinear : register(s1);

// ============================================================
// Vertex Structures
// ============================================================

struct VoxelVSInput {
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD0;
};

struct VoxelPSInput {
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 Normal    : TEXCOORD1;
    float4 Color     : COLOR;
    float2 TexCoord  : TEXCOORD2;
};

struct DebugVSInput {
    float3 Position : POSITION;
    float4 Color    : COLOR;
};

struct DebugPSInput {
    float4 Position : SV_POSITION;
    float4 Color    : COLOR;
};

// ============================================================
// Utility Functions
// ============================================================

// Linear distance fog
float CalcFogFactor(float3 worldPos) {
    float dist = length(worldPos - gCameraPosition);
    return saturate((dist - gFogStart) / max(gFogEnd - gFogStart, 0.001f));
}

// Apply fog to a color
float3 ApplyFog(float3 color, float3 worldPos) {
    float fogFactor = CalcFogFactor(worldPos);
    return lerp(color, gFogColor, fogFactor);
}

// Blinn-Phong lighting with specular
float3 CalcBlinnPhong(float3 normal, float3 worldPos, float3 albedo, float specPower) {
    float3 N = normalize(normal);
    float3 L = normalize(-gSunDirection);
    float3 V = normalize(gCameraPosition - worldPos);
    float3 H = normalize(L + V);

    // Diffuse
    float NdotL = max(dot(N, L), 0.0f);
    float3 diffuse = gSunColor * gSunIntensity * NdotL;

    // Specular (Blinn-Phong)
    float NdotH = max(dot(N, H), 0.0f);
    float spec = pow(NdotH, specPower) * NdotL;
    float3 specular = gSunColor * spec * 0.25f;

    // Ambient
    float3 ambient = gAmbientColor * gAmbientIntensity;

    // Combine
    return albedo * (ambient + diffuse) + specular;
}

// Simple hemisphere ambient (ground/sky blend)
float3 CalcHemisphereAmbient(float3 normal, float3 skyColor, float3 groundColor) {
    float up = normal.y * 0.5f + 0.5f;
    return lerp(groundColor, skyColor, up);
}

// Reinhard tone mapping
float3 ToneMapReinhard(float3 color) {
    return color / (color + 1.0f);
}

// ACES filmic tone mapping (approximation)
float3 ToneMapACES(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Linear to sRGB
float3 LinearToSRGB(float3 color) {
    return pow(max(color, 0.0f), 1.0f / 2.2f);
}

// sRGB to Linear
float3 SRGBToLinear(float3 color) {
    return pow(max(color, 0.0f), 2.2f);
}

// Fresnel-Schlick approximation
float FresnelSchlick(float cosTheta, float F0) {
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

// ============================================================
// Shadow Mapping
// ============================================================

// Transform world position to shadow map UV + depth
float3 WorldToShadowUV(float3 worldPos) {
    float4 lightClip = mul(float4(worldPos, 1.0f), gLightViewProjection);
    lightClip.xyz /= lightClip.w;
    // Convert from [-1,1] to [0,1] UV space
    float2 uv = lightClip.xy * 0.5f + 0.5f;
    uv.y = 1.0f - uv.y;  // Flip Y for texture space
    return float3(uv, lightClip.z);
}

// PCF shadow sampling (5-tap)
float CalcShadow(float3 worldPos, float3 normal) {
    // Apply normal bias to reduce shadow acne on surfaces facing the light
    float3 biasedPos = worldPos + normal * gShadowNormalBias;
    float3 shadowUV = WorldToShadowUV(biasedPos);

    // Out of shadow map bounds = no shadow
    if (shadowUV.x < 0.0f || shadowUV.x > 1.0f ||
        shadowUV.y < 0.0f || shadowUV.y > 1.0f ||
        shadowUV.z < 0.0f || shadowUV.z > 1.0f)
        return 1.0f;

    float depth = shadowUV.z - gShadowBias;
    float texelSize = 1.0f / gShadowMapSize;

    // 5-tap PCF (center + 4 neighbors)
    float shadow = 0.0f;
    shadow += gShadowMap.SampleCmpLevelZero(gShadowSampler, shadowUV.xy, depth);
    shadow += gShadowMap.SampleCmpLevelZero(gShadowSampler, shadowUV.xy + float2( texelSize, 0), depth);
    shadow += gShadowMap.SampleCmpLevelZero(gShadowSampler, shadowUV.xy + float2(-texelSize, 0), depth);
    shadow += gShadowMap.SampleCmpLevelZero(gShadowSampler, shadowUV.xy + float2(0,  texelSize), depth);
    shadow += gShadowMap.SampleCmpLevelZero(gShadowSampler, shadowUV.xy + float2(0, -texelSize), depth);
    shadow /= 5.0f;

    // Blend based on shadow intensity setting
    return lerp(1.0f, shadow, gShadowIntensity);
}

// Blinn-Phong lighting WITH shadows
float3 CalcBlinnPhongShadowed(float3 normal, float3 worldPos, float3 albedo, float specPower) {
    float3 N = normalize(normal);
    float3 L = normalize(-gSunDirection);
    float3 V = normalize(gCameraPosition - worldPos);
    float3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0f);

    // Shadow factor
    float shadow = CalcShadow(worldPos, N);

    // Diffuse (affected by shadow)
    float3 diffuse = gSunColor * gSunIntensity * NdotL * shadow;

    // Specular (affected by shadow)
    float NdotH = max(dot(N, H), 0.0f);
    float spec = pow(NdotH, specPower) * NdotL * shadow;
    float3 specular = gSunColor * spec * 0.25f;

    // Ambient (NOT affected by shadow)
    float3 ambient = gAmbientColor * gAmbientIntensity;

    return albedo * (ambient + diffuse) + specular;
}

// Cel-shaded lighting WITH shadows — quantized diffuse bands + hard specular + rim light
float3 CalcCelShadowed(float3 normal, float3 worldPos, float3 albedo, float specPower) {
    float3 N = normalize(normal);
    float3 L = normalize(-gSunDirection);
    float3 V = normalize(gCameraPosition - worldPos);
    float3 H = normalize(L + V);

    float NdotL = dot(N, L);

    // Shadow factor
    float shadow = CalcShadow(worldPos, N);

    // Wrap lighting: shift NdotL so back-faces still receive some light
    // This prevents surfaces facing away from the sun from going pure black
    float wrapNdotL = (NdotL + 0.3f) / 1.3f;
    float lit = max(wrapNdotL, 0.0f) * shadow;

    // Quantize diffuse into bands, but guarantee a minimum band
    // so shadowed areas still have some definition
    float bands = max(gCelBands, 2.0f);
    float stepped = floor(lit * bands + 0.5f) / bands;
    // Ensure at least a small amount of directional light
    stepped = max(stepped, 0.15f / bands);
    float3 diffuse = gSunColor * gSunIntensity * stepped;

    // Hard specular highlight (cel-style: sharp cutoff)
    float NdotH = max(dot(N, H), 0.0f);
    float specMask = step(0.95f, pow(NdotH, specPower)) * step(0.5f, lit);
    float3 specular = gSunColor * specMask * 0.4f;

    // Rim/fresnel highlight (edge light for dramatic silhouette)
    float rim = 1.0f - max(dot(N, V), 0.0f);
    rim = pow(rim, 3.0f) * gCelRimIntensity;
    float rimMask = step(0.1f, NdotL + 0.3f);  // Only on lit side
    float3 rimColor = gSunColor * rim * rimMask * 0.5f;

    // Ambient — keep it clean, no quantization artifacts
    float3 ambient = gAmbientColor * gAmbientIntensity;
    // Slight hemisphere boost: surfaces facing up get a bit more sky light
    float hemi = N.y * 0.5f + 0.5f;
    ambient *= lerp(0.8f, 1.2f, hemi);

    return albedo * (ambient + diffuse) + specular + rimColor;
}

#endif // COMMON_HLSLI
