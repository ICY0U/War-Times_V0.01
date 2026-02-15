// Sky Pixel Shader — Cel-shaded / hand-drawn sky with stylized clouds
#include "Common.hlsli"

struct SkyVSOutput {
    float4 Position : SV_POSITION;
    float3 ViewDir  : TEXCOORD0;
};

// ============================================================
// Procedural noise for clouds (value noise + FBM)
// ============================================================
float Hash(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float ValueNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);

    float a = Hash(i);
    float b = Hash(i + float2(1.0, 0.0));
    float c = Hash(i + float2(0.0, 1.0));
    float d = Hash(i + float2(1.0, 1.0));

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float FBM(float2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    [unroll]
    for (int i = 0; i < 5; i++) {
        if (i >= octaves) break;
        value += amplitude * ValueNoise(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

// Quantize a value into N steps (cel-shading helper)
float Quantize(float val, float steps) {
    return floor(val * steps + 0.5) / steps;
}

// Quantize a color into bands
float3 QuantizeColor(float3 col, float steps) {
    return float3(
        Quantize(col.r, steps),
        Quantize(col.g, steps),
        Quantize(col.b, steps)
    );
}

float4 PSMain(SkyVSOutput input) : SV_TARGET {
    float3 dir = normalize(input.ViewDir);
    float y = dir.y;

    bool celMode = gCelEnabled > 0.5;
    float bands = max(gCelBands, 2.0);

    // ============================================================
    // Sky gradient — keep smooth even in cel mode (no banding)
    // ============================================================
    float3 color;
    if (y > 0.0f) {
        float t = pow(saturate(y), gSkyHorizonFalloff);
        color = lerp(gSkyHorizonColor, gSkyZenithColor, t);
    } else {
        float t = pow(saturate(-y), 0.8f);
        color = lerp(gSkyHorizonColor, gSkyGroundColor, t);
    }

    // ============================================================
    // Sun disc
    // ============================================================
    float3 sunDir = normalize(-gSunDirection);
    float sunDot = dot(dir, sunDir);

    if (celMode) {
        // Flat cartoon sun — convert cosine threshold to a wider usable angle
        // gSunDiscSize is cosine of the disc angle (e.g. 0.9995 = tiny)
        // Use a slightly more generous threshold so the sun is actually visible
        float discThreshold = gSunDiscSize;
        float sunDisc = step(discThreshold, sunDot);
        float3 sunFlatColor = gSunColor * gSkyBrightness * 2.0;
        color = lerp(color, sunFlatColor, sunDisc);

        // Warm glow zone around sun (subtle, not a ring)
        float glowZone = smoothstep(discThreshold - 0.06, discThreshold, sunDot);
        float glowOnly = glowZone * (1.0 - sunDisc);
        color += gSunColor * glowOnly * 0.15 * gSunGlowIntensity;
    } else {
        // Original smooth sun
        float sunDisc = smoothstep(gSunDiscSize - 0.002, gSunDiscSize, sunDot);
        color = lerp(color, gSunColor * gSkyBrightness * 2.0, sunDisc);

        float sunGlow = pow(saturate(sunDot), gSunGlowFalloff);
        color += gSunColor * sunGlow * gSunGlowIntensity;
    }

    // ============================================================
    // Procedural Clouds
    // ============================================================
    if (y > 0.0f && gCloudCoverage > 0.001f) {
        float2 cloudUV = dir.xz / (y + gCloudHeight);
        cloudUV *= 2.5;

        float2 wind = float2(gTime * gCloudSpeed, gTime * gCloudSpeed * 0.3);
        cloudUV += wind;

        float noise = FBM(cloudUV, 5);

        float threshold = 1.0 - gCloudCoverage;

        float cloudMask;
        float3 finalCloudColor;

        if (celMode) {
            // ---- Cel-shaded clouds: hard edges, flat shading, 2-tone ----
            // Hard cutoff instead of smooth transition
            cloudMask = step(threshold, noise);

            // Slight edge softening to prevent pixel aliasing
            float edgeSoft = smoothstep(threshold - 0.02, threshold + 0.02, noise);
            cloudMask = lerp(cloudMask, edgeSoft, 0.3);

            // Simple 2-tone cloud shading: lit top, shadowed bottom
            float cloudShadeNoise = FBM(cloudUV * 2.0 + 0.5, 3);
            float shadeFactor = step(0.45, cloudShadeNoise);  // Hard shadow line
            float3 cloudLight = gCloudColor * gSkyBrightness;
            float3 cloudShadow = gCloudColor * gSkyBrightness * 0.6;  // Darker underside

            // Sun-lit highlight band
            float sunInfluence = saturate(sunDot * 0.5 + 0.5);
            float sunHighlight = step(0.7, sunInfluence) * gCloudSunInfluence;
            float3 highlightColor = lerp(gCloudColor, gSunColor, 0.3) * gSkyBrightness * 1.2;

            // Compose: shadow base → lit → highlight
            finalCloudColor = lerp(cloudShadow, cloudLight, shadeFactor);
            finalCloudColor = lerp(finalCloudColor, highlightColor, sunHighlight * shadeFactor);

            // Optional: quantize cloud color to match the cel palette
            finalCloudColor = QuantizeColor(finalCloudColor, bands + 2.0);
        } else {
            // ---- Original smooth clouds ----
            cloudMask = smoothstep(threshold, threshold + 0.3 / gCloudDensity, noise);

            float detail = FBM(cloudUV * 3.0 + wind * 0.5, 4);
            cloudMask *= smoothstep(0.2, 0.6, detail + gCloudCoverage * 0.5);

            float sunInfluence = saturate(sunDot * 0.5 + 0.5);
            sunInfluence = pow(sunInfluence, 2.0) * gCloudSunInfluence;
            float3 litCloudColor = lerp(gCloudColor, gSunColor * 1.5, sunInfluence);

            float shade = lerp(0.7, 1.0, noise);
            finalCloudColor = litCloudColor * shade * gSkyBrightness;
        }

        // Fade clouds near horizon
        float horizonFade = smoothstep(0.0, 0.15, y);
        color = lerp(color, finalCloudColor, cloudMask * horizonFade);
    }

    // ============================================================
    // Horizon haze
    // ============================================================
    if (celMode) {
        // Hard horizon line instead of soft gradient
        float horizonBand = smoothstep(0.0, 0.03, abs(y));
        float horizonLine = 1.0 - horizonBand;
        color = lerp(color, gSkyHorizonColor * 1.1, horizonLine * 0.5);
    } else {
        float horizonGlow = 1.0 - abs(y);
        horizonGlow = pow(horizonGlow, 4.0) * 0.15;
        color += gSkyHorizonColor * horizonGlow;
    }

    // Overall brightness
    color *= gSkyBrightness;

    // Tone map
    color = ToneMapReinhard(color);

    return float4(color, 1.0f);
}
