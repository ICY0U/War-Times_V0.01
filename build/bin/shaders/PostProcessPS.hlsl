// Post-Process Pixel Shaders — Bloom extract, blur, compositing
// Multiple entry points compiled separately

// ============================================================
// Constant Buffer — matches CBPostProcess in PostProcess.cpp (b5)
// ============================================================
cbuffer CBPostProcess : register(b5) {
    float BloomThreshold;
    float BloomIntensity;
    float VignetteIntensity;
    float VignetteSmoothness;
    float Brightness;
    float Contrast;
    float Saturation;
    float Gamma;
    float3 Tint;
    float BlurDirection;  // 0 = horizontal, 1 = vertical
    float TexelSizeX;
    float TexelSizeY;
    int   BloomEnabled;
    int   VignetteEnabled;
    int   SSAOEnabled;
    int   OutlineEnabled;
    float OutlineThickness;
    float OutlineDepthThreshold;
    float OutlineNormalThreshold;  // unused padding (for future normal-based edges)
    float PaperGrainIntensity;
    float HatchingIntensity;
    float HatchingScale;
    float3 OutlineColor;
    float _postPad1;
};

// ============================================================
// Resources
// ============================================================
Texture2D    gSceneTexture : register(t0);   // HDR scene or bloom source
Texture2D    gBloomTexture : register(t1);   // Bloom result (used in composite)
Texture2D    gDepthTexture : register(t2);   // Depth buffer (for outline detection)
Texture2D    gAOTexture    : register(t4);   // SSAO result
SamplerState gLinearSampler : register(s1);  // Re-use linear sampler from renderer
SamplerState gPointSampler  : register(s0);  // Point sampler for depth reads

// ============================================================
// Input structure from PostProcessVS
// ============================================================
struct PostVSOutput {
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
};

// ============================================================
// Utility: Luminance
// ============================================================
float Luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// ============================================================
// ENTRY POINT 1: Bloom Extract
// Extracts bright pixels above threshold
// ============================================================
float4 BloomExtract(PostVSOutput input) : SV_TARGET {
    float3 color = gSceneTexture.Sample(gLinearSampler, input.UV).rgb;
    float lum = Luminance(color);

    // Soft threshold (smooth transition)
    float knee = BloomThreshold * 0.5;
    float soft = lum - BloomThreshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);

    float contribution = max(soft, lum - BloomThreshold) / max(lum, 0.00001);
    return float4(color * contribution, 1.0);
}

// ============================================================
// ENTRY POINT 2: Bloom Gaussian Blur (9-tap separable)
// Direction controlled by BlurDirection (0=H, 1=V)
// ============================================================
static const float weights[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };

float4 BloomBlur(PostVSOutput input) : SV_TARGET {
    float2 texelSize = float2(TexelSizeX, TexelSizeY);
    float2 direction = (BlurDirection < 0.5) ? float2(1.0, 0.0) : float2(0.0, 1.0);
    float2 offset = direction * texelSize;

    float3 result = gSceneTexture.Sample(gLinearSampler, input.UV).rgb * weights[0];

    [unroll]
    for (int i = 1; i < 5; i++) {
        result += gSceneTexture.Sample(gLinearSampler, input.UV + offset * i).rgb * weights[i];
        result += gSceneTexture.Sample(gLinearSampler, input.UV - offset * i).rgb * weights[i];
    }

    return float4(result, 1.0);
}

// ============================================================
// Utility: Hash for procedural noise
// ============================================================
float PostHash(float2 p) {
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float PostNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = PostHash(i);
    float b = PostHash(i + float2(1, 0));
    float c = PostHash(i + float2(0, 1));
    float d = PostHash(i + float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// ============================================================
// Depth-based edge detection (Sobel filter)
// ============================================================
float SampleDepth(float2 uv) {
    return gDepthTexture.Sample(gPointSampler, uv).r;
}

float DetectEdges(float2 uv, float2 texelSize, float thickness) {
    float2 offset = texelSize * thickness;

    // 3x3 depth samples
    float d00 = SampleDepth(uv + float2(-offset.x, -offset.y));
    float d10 = SampleDepth(uv + float2(       0.0, -offset.y));
    float d20 = SampleDepth(uv + float2( offset.x, -offset.y));
    float d01 = SampleDepth(uv + float2(-offset.x,        0.0));
    float d21 = SampleDepth(uv + float2( offset.x,        0.0));
    float d02 = SampleDepth(uv + float2(-offset.x,  offset.y));
    float d12 = SampleDepth(uv + float2(       0.0,  offset.y));
    float d22 = SampleDepth(uv + float2( offset.x,  offset.y));

    // Sobel operator
    float sobelH = -d00 - 2.0 * d01 - d02 + d20 + 2.0 * d21 + d22;
    float sobelV = -d00 - 2.0 * d10 - d20 + d02 + 2.0 * d12 + d22;

    float edge = sqrt(sobelH * sobelH + sobelV * sobelV);

    // Scale by depth (edges far away should be thinner)
    float centerDepth = SampleDepth(uv);
    float depthScale = 1.0 - centerDepth * centerDepth;  // Reduce edge at far depth
    edge *= depthScale * 80.0;

    return saturate(edge);
}

// ============================================================
// Cross-hatching pattern
// ============================================================
float Hatching(float2 screenPos, float luminance, float scale) {
    // Multiple hatching line directions based on darkness
    float2 uv = screenPos / scale;

    // Primary diagonal hatching (light shadow)
    float hatch1 = abs(sin((uv.x + uv.y) * 3.14159 * 2.0));
    hatch1 = smoothstep(0.3, 0.35, hatch1);

    // Secondary cross-hatch (deeper shadow)
    float hatch2 = abs(sin((uv.x - uv.y) * 3.14159 * 2.0));
    hatch2 = smoothstep(0.3, 0.35, hatch2);

    // Third layer - horizontal (very dark)
    float hatch3 = abs(sin(uv.y * 3.14159 * 3.0));
    hatch3 = smoothstep(0.3, 0.35, hatch3);

    // Apply layers based on luminance level
    float result = 1.0;
    if (luminance < 0.55) result = min(result, hatch1);
    if (luminance < 0.35) result = min(result, hatch2);
    if (luminance < 0.15) result = min(result, hatch3);

    return result;
}

// ============================================================
// ENTRY POINT 3: Composite — combines scene + bloom + effects
// ============================================================
float4 Composite(PostVSOutput input) : SV_TARGET {
    float3 scene = gSceneTexture.Sample(gLinearSampler, input.UV).rgb;
    float3 bloom = gBloomTexture.Sample(gLinearSampler, input.UV).rgb;

    // Apply SSAO — darken by ambient occlusion factor
    float3 color = scene;
    if (SSAOEnabled) {
        float ao = gAOTexture.Sample(gLinearSampler, input.UV).r;
        color *= ao;
    }
    if (BloomEnabled)
        color += bloom * BloomIntensity;

    // ---- Ink Outlines ----
    if (OutlineEnabled) {
        float2 texelSize = float2(TexelSizeX * 0.5, TexelSizeY * 0.5); // full-res texel
        float edge = DetectEdges(input.UV, texelSize, OutlineThickness);
        color = lerp(color, OutlineColor, edge);
    }

    // ---- Cross-Hatching (hand-drawn shading) ----
    if (HatchingIntensity > 0.001) {
        float lum = Luminance(color);
        float2 screenPos = input.UV * float2(1.0 / TexelSizeX, 1.0 / TexelSizeY) * 0.5;
        float hatch = Hatching(screenPos, lum, HatchingScale);
        // Only apply hatching in darker areas
        float hatchMask = smoothstep(0.6, 0.3, lum);
        color = lerp(color, color * hatch, hatchMask * HatchingIntensity);
    }

    // ---- Color Grading ----
    // Brightness
    color += Brightness;

    // Contrast (around midpoint 0.5)
    color = (color - 0.5) * Contrast + 0.5;

    // Saturation
    float lum = Luminance(color);
    color = lerp(float3(lum, lum, lum), color, Saturation);

    // Tint
    color *= Tint;

    // Gamma correction
    color = pow(max(color, 0.0), 1.0 / Gamma);

    // ---- Vignette ----
    if (VignetteEnabled) {
        float2 uv = input.UV * 2.0 - 1.0;
        float dist = length(uv);
        float vignette = smoothstep(1.0, 1.0 - VignetteSmoothness, dist * VignetteIntensity);
        color *= vignette;
    }

    // ---- Paper Grain (hand-drawn feel) ----
    if (PaperGrainIntensity > 0.001) {
        float2 grainUV = input.UV * float2(1.0 / TexelSizeX, 1.0 / TexelSizeY) * 0.5;
        float grain = PostNoise(grainUV * 0.8) * 0.5 + PostNoise(grainUV * 2.4) * 0.3 + PostNoise(grainUV * 6.0) * 0.2;
        grain = grain * 2.0 - 1.0;  // Center around 0
        color += grain * PaperGrainIntensity;
    }

    // Clamp to valid range
    color = saturate(color);

    return float4(color, 1.0);
}
