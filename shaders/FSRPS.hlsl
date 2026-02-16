// FSRPS.hlsl — FSR 1.0 Edge-Adaptive Spatial Upsampling (EASU) & Robust Contrast-Adaptive Sharpening (RCAS)
// Two entry points: EASUMain (upscale) and RCASMain (sharpen)

cbuffer CBFSRParams : register(b6) {
    float InputSizeX;
    float InputSizeY;
    float OutputSizeX;
    float OutputSizeY;
    float RcasSharpness;
    float PassMode;        // 0 = EASU, 1 = RCAS
    float2 Pad;
};

Texture2D    InputTexture : register(t0);
SamplerState LinearSampler : register(s5);

struct PSInput {
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
};

// ---------- Utility ----------
float Luma(float3 c) {
    return dot(c, float3(0.299f, 0.587f, 0.114f));
}

// ============================================================
// EASU — Edge-Adaptive Spatial Upsampling
// Implements a 3x3 Lanczos-style filter with edge-awareness
// to produce a high-quality upscale from render res to output res.
// ============================================================
float Lanczos2(float x) {
    const float PI = 3.14159265359f;
    if (abs(x) < 1e-5f) return 1.0f;
    if (abs(x) >= 2.0f) return 0.0f;
    float px = PI * x;
    return (sin(px) * sin(px * 0.5f)) / (px * px * 0.5f);
}

float4 EASUMain(PSInput input) : SV_TARGET {
    float2 outputSize = float2(OutputSizeX, OutputSizeY);
    float2 inputSize  = float2(InputSizeX, InputSizeY);
    float2 inputTexelSize = 1.0f / inputSize;
    
    // Map output pixel to input space
    float2 srcPos = input.UV * inputSize;
    float2 srcCenter = floor(srcPos - 0.5f) + 0.5f;
    float2 fracPos = srcPos - srcCenter;
    
    // Sample a 6x6 neighborhood in input space (using bilinear taps at 4x4 grid)
    // For performance, we use a simplified 4x4 Lanczos kernel
    float3 colorSum = float3(0, 0, 0);
    float weightSum = 0.0f;
    
    // Gather 4x4 texel neighborhood
    float3 samples[16];
    float lumas[16];
    
    [unroll]
    for (int y = -1; y <= 2; y++) {
        [unroll]
        for (int x = -1; x <= 2; x++) {
            float2 sampleUV = (srcCenter + float2(x, y)) * inputTexelSize;
            sampleUV = clamp(sampleUV, 0.0f, 1.0f);
            int idx = (y + 1) * 4 + (x + 1);
            samples[idx] = InputTexture.SampleLevel(LinearSampler, sampleUV, 0).rgb;
            lumas[idx] = Luma(samples[idx]);
        }
    }
    
    // Compute gradient information from the center 2x2 and neighbors
    // This detects edges and adjusts filter shape accordingly
    
    // Horizontal and vertical gradients from center samples
    // Center 2x2: indices 5,6,9,10
    float lumaCenter = (lumas[5] + lumas[6] + lumas[9] + lumas[10]) * 0.25f;
    
    // Edge detection using Sobel-like operators on 3x3 around center
    float edgeH = abs(lumas[4] - lumas[6])  + abs(lumas[5] - lumas[6]) * 2.0f +
                  abs(lumas[8] - lumas[10]) + abs(lumas[9] - lumas[10]) * 2.0f;
    float edgeV = abs(lumas[1] - lumas[9])  + abs(lumas[5] - lumas[9]) * 2.0f +
                  abs(lumas[2] - lumas[10]) + abs(lumas[6] - lumas[10]) * 2.0f;
    
    // Edge strength and direction
    float edgeStrength = sqrt(edgeH * edgeH + edgeV * edgeV);
    edgeStrength = saturate(edgeStrength * 4.0f); // Amplify for effect
    
    // Compute anisotropic filter weights
    // Along edges: use narrow kernel; across edges: use wider kernel
    float2 edgeDir = float2(edgeV, edgeH);
    float edgeLen = length(edgeDir);
    if (edgeLen > 0.001f) edgeDir /= edgeLen;
    
    // Blend between isotropic Lanczos and edge-directed sampling
    [unroll]
    for (int sy = -1; sy <= 2; sy++) {
        [unroll]
        for (int sx = -1; sx <= 2; sx++) {
            float2 offset = float2(sx, sy) - fracPos;
            int idx = (sy + 1) * 4 + (sx + 1);
            
            // Base Lanczos weight
            float w = Lanczos2(offset.x) * Lanczos2(offset.y);
            
            // Edge-adaptive: reduce weight of samples across edges
            if (edgeStrength > 0.01f) {
                float crossEdge = abs(dot(offset, edgeDir));
                float alongEdge = abs(dot(offset, float2(-edgeDir.y, edgeDir.x)));
                
                // Narrow the kernel perpendicular to edge, widen along edge
                float aniso = lerp(1.0f, exp(-crossEdge * 2.0f), edgeStrength * 0.5f);
                w *= aniso;
            }
            
            // Luma-based ringing suppression: reduce weight if sample luma differs a lot
            float lumaDiff = abs(lumas[idx] - lumaCenter);
            float ringingSuppress = 1.0f / (1.0f + lumaDiff * 4.0f);
            w *= lerp(1.0f, ringingSuppress, edgeStrength);
            
            w = max(w, 0.0f);
            colorSum += samples[idx] * w;
            weightSum += w;
        }
    }
    
    float3 result = colorSum / max(weightSum, 0.0001f);
    return float4(saturate(result), 1.0f);
}

// ============================================================
// RCAS — Robust Contrast-Adaptive Sharpening
// AMD CAS-style adaptive sharpening that avoids ringing artifacts.
// ============================================================
float4 RCASMain(PSInput input) : SV_TARGET {
    float2 texelSize = 1.0f / float2(InputSizeX, InputSizeY);
    float2 uv = input.UV;
    
    // Sample cross pattern (center + 4 neighbors)
    float3 center = InputTexture.SampleLevel(LinearSampler, uv, 0).rgb;
    float3 north  = InputTexture.SampleLevel(LinearSampler, uv + float2(0, -texelSize.y), 0).rgb;
    float3 south  = InputTexture.SampleLevel(LinearSampler, uv + float2(0,  texelSize.y), 0).rgb;
    float3 west   = InputTexture.SampleLevel(LinearSampler, uv + float2(-texelSize.x, 0), 0).rgb;
    float3 east   = InputTexture.SampleLevel(LinearSampler, uv + float2( texelSize.x, 0), 0).rgb;
    
    // Compute lumas
    float lumaC = Luma(center);
    float lumaN = Luma(north);
    float lumaS = Luma(south);
    float lumaW = Luma(west);
    float lumaE = Luma(east);
    
    // Find min/max luma in neighborhood
    float lumaMin = min(lumaC, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float lumaMax = max(lumaC, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    
    // Compute adaptive sharpening weight
    // Higher contrast → less sharpening (prevent ringing)
    float contrast = lumaMax - lumaMin;
    
    // CAS-style weight: w = sqrt(min / max) limited by sharpness
    float w = 0.0f;
    if (lumaMax > 0.0f) {
        w = sqrt(saturate(lumaMin / lumaMax));
        w = w * -RcasSharpness; // Negative because we subtract neighbors
    }
    
    // Clamp sharpening weight to prevent artifacts
    w = max(w, -0.25f);
    
    // Apply: center + w * (center - average_neighbors)
    // Rewritten as: (1 - 4*w) * center + w * (N + S + W + E) / (1 - 4*w + 4*w) = normalized
    float3 sharpened = (center + (north + south + west + east) * w) / (1.0f + 4.0f * w);
    
    // Clamp to prevent out-of-range ringing
    float3 minColor = min(center, min(min(north, south), min(west, east)));
    float3 maxColor = max(center, max(max(north, south), max(west, east)));
    sharpened = clamp(sharpened, minColor, maxColor);
    
    return float4(saturate(sharpened), 1.0f);
}
