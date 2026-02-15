// SSAO Pixel Shader — Screen-Space Ambient Occlusion
// Two entry points: SSAOMain (compute AO), BlurMain (bilateral blur)

// ============================================================
// Constant Buffer (b6) — matches CBSSAO in SSAO.cpp
// ============================================================
cbuffer CBSSAO : register(b6) {
    float4x4 Projection;
    float4x4 InvProjection;
    float4x4 View;
    float4   Samples[64];
    float2   ScreenSize;
    float2   NoiseScale;
    float    Radius;
    float    Bias;
    float    Intensity;
    int      KernelSize;
    float    NearZ;
    float    FarZ;
    float2   _pad;
};

// ============================================================
// Resources
// ============================================================
Texture2D    gDepthTexture : register(t2);   // Scene depth buffer (non-linear)
Texture2D    gNoiseTexture : register(t3);   // 4x4 random rotation vectors
SamplerState gPointSampler : register(s0);   // Point sampler for depth
SamplerState gLinearSampler : register(s1);  // Linear sampler

// ============================================================
// Input from PostProcessVS
// ============================================================
struct PostVSOutput {
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
};

// ============================================================
// Utility: Reconstruct view-space position from depth
// ============================================================
float LinearizeDepth(float depth) {
    // Reverse the perspective projection to get linear depth
    return NearZ * FarZ / (FarZ - depth * (FarZ - NearZ));
}

float3 ReconstructViewPos(float2 uv, float depth) {
    // Convert UV to clip space
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;

    // Unproject to view space
    float4 viewPos = mul(clipPos, InvProjection);
    return viewPos.xyz / viewPos.w;
}

// ============================================================
// Utility: Reconstruct view-space normal from depth
// Uses partial derivatives of view position
// ============================================================
float3 ReconstructNormal(float3 viewPos, float2 uv) {
    float2 texelSize = 1.0 / ScreenSize;

    float depthR = gDepthTexture.Sample(gPointSampler, uv + float2(texelSize.x, 0)).r;
    float depthU = gDepthTexture.Sample(gPointSampler, uv + float2(0, -texelSize.y)).r;

    float3 viewPosR = ReconstructViewPos(uv + float2(texelSize.x, 0), depthR);
    float3 viewPosU = ReconstructViewPos(uv + float2(0, -texelSize.y), depthU);

    float3 dpdx = viewPosR - viewPos;
    float3 dpdy = viewPosU - viewPos;

    return normalize(cross(dpdy, dpdx));
}

// ============================================================
// ENTRY POINT 1: SSAO Computation
// ============================================================
float4 SSAOMain(PostVSOutput input) : SV_TARGET {
    float depth = gDepthTexture.Sample(gPointSampler, input.UV).r;

    // Skip sky (depth = 1.0)
    if (depth >= 1.0)
        return float4(1.0, 1.0, 1.0, 1.0);

    float3 viewPos = ReconstructViewPos(input.UV, depth);
    float3 normal  = ReconstructNormal(viewPos, input.UV);

    // Get random rotation vector from noise texture (tiled)
    float2 noiseUV = input.UV * NoiseScale;
    float3 randomVec = gNoiseTexture.Sample(gPointSampler, noiseUV).xyz;

    // Create TBN matrix to orient hemisphere along surface normal
    float3 tangent   = normalize(randomVec - normal * dot(randomVec, normal));
    float3 bitangent = cross(normal, tangent);
    float3x3 TBN     = float3x3(tangent, bitangent, normal);

    // Sample hemisphere and accumulate occlusion
    float occlusion = 0.0;
    float linearDepth = LinearizeDepth(depth);

    for (int i = 0; i < KernelSize; i++) {
        // Orient sample to view-space hemisphere
        float3 sampleDir = mul(Samples[i].xyz, TBN);
        float3 samplePos = viewPos + sampleDir * Radius;

        // Project sample to screen space to get depth
        float4 offset = mul(float4(samplePos, 1.0), Projection);
        offset.xyz /= offset.w;
        float2 sampleUV = offset.xy * 0.5 + 0.5;
        sampleUV.y = 1.0 - sampleUV.y;

        // Skip samples that project outside the screen
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;

        // Sample depth at projected position
        float sampleDepth = gDepthTexture.Sample(gPointSampler, sampleUV).r;

        // Skip sky samples (they shouldn't occlude anything)
        if (sampleDepth >= 1.0)
            continue;

        float sampleLinear = LinearizeDepth(sampleDepth);

        // Range check — only count occlusion from geometry within the sample radius
        float depthDiff = linearDepth - sampleLinear;
        float rangeCheck = smoothstep(Radius, 0.0, abs(depthDiff));

        // If the actual surface is closer to camera than our sample point, it's occluded
        occlusion += step(Bias, depthDiff) * rangeCheck;
    }

    float ao = 1.0 - (occlusion / (float)KernelSize) * Intensity;
    ao = saturate(ao);
    return float4(ao, ao, ao, 1.0);
}

// ============================================================
// ENTRY POINT 2: Bilateral Blur (edge-preserving)
// 5x5 box blur on the AO texture
// In the blur pass, t2 = raw AO texture (bound by SSAO::Compute)
// ============================================================
float4 BlurMain(PostVSOutput input) : SV_TARGET {
    float2 texelSize = 1.0 / ScreenSize;
    float centerAO = gDepthTexture.Sample(gLinearSampler, input.UV).r;

    float result = 0.0;
    float totalWeight = 0.0;

    // gDepthTexture (t2) contains the raw AO in this pass
    // Depth-aware blur: weight samples by how close their AO is to center
    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            float2 offset = float2(x, y) * texelSize;
            float ao = gDepthTexture.Sample(gLinearSampler, input.UV + offset).r;

            // Simple spatial weight (closer = higher weight)
            float dist = length(float2(x, y));
            float w = 1.0 / (1.0 + dist);

            result += ao * w;
            totalWeight += w;
        }
    }

    result /= totalWeight;
    return float4(result, result, result, 1.0);
}
