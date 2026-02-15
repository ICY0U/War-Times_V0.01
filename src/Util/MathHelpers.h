#pragma once

#include <DirectXMath.h>
#include <cmath>
#include <cstdint>

namespace WT {

using namespace DirectX;

// Constants
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = PI * 2.0f;
constexpr float HALF_PI = PI * 0.5f;
constexpr float DEG_TO_RAD = PI / 180.0f;
constexpr float RAD_TO_DEG = 180.0f / PI;

inline float ToRadians(float degrees) { return degrees * DEG_TO_RAD; }
inline float ToDegrees(float radians) { return radians * RAD_TO_DEG; }

inline float Clamp(float val, float minVal, float maxVal) {
    if (val < minVal) return minVal;
    if (val > maxVal) return maxVal;
    return val;
}

inline int FloorToInt(float val) {
    int i = static_cast<int>(val);
    return (val < static_cast<float>(i)) ? i - 1 : i;
}

inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// Simple vertex structures
struct VertexPos {
    XMFLOAT3 Position;
};

struct VertexPosColor {
    XMFLOAT3 Position;
    XMFLOAT4 Color;
};

struct VertexPosNormalColor {
    XMFLOAT3 Position;
    XMFLOAT3 Normal;
    XMFLOAT4 Color;
    XMFLOAT2 TexCoord;   // UV for texture mapping (0,0 for untextured)
};

// Skinned vertex for GPU bone animation (FPS arms, etc.)
// Must match SkinnedVSInput in SkinnedVS.hlsl
struct VertexSkinned {
    XMFLOAT3 Position;      // 0   (12 bytes)
    XMFLOAT3 Normal;        // 12  (12 bytes)
    XMFLOAT2 TexCoord;      // 24  (8 bytes)
    uint8_t  BoneIndices[4]; // 32  (4 bytes)
    XMFLOAT4 BoneWeights;   // 36  (16 bytes)
};                           // Total: 52 bytes

// ============================================================
// Constant buffer structures — must match Common.hlsli exactly
// All must be 16-byte aligned
// ============================================================

// b0 — Per-Frame (camera, time) — bound VS + PS
struct CBPerFrame {
    XMFLOAT4X4 View;
    XMFLOAT4X4 Projection;
    XMFLOAT4X4 ViewProjection;
    XMFLOAT4X4 InvViewProjection;
    XMFLOAT3   CameraPosition;
    float      Time;
    XMFLOAT2   ScreenSize;
    float      NearZ;
    float      FarZ;
};

// b1 — Per-Object (world transform) — bound VS
struct CBPerObject {
    XMFLOAT4X4 World;
    XMFLOAT4X4 WorldInvTranspose;
    XMFLOAT4   ObjectColor;      // If alpha > 0, overrides vertex color
};

// b2 — Lighting (sun, ambient, fog, cel-shading) — bound PS
struct CBLighting {
    XMFLOAT3 SunDirection;
    float    SunIntensity;
    XMFLOAT3 SunColor;
    float    CelBands;         // Number of shading bands (2-5), 0 = smooth
    XMFLOAT3 AmbientColor;
    float    AmbientIntensity;
    XMFLOAT3 FogColor;
    float    FogDensity;
    float    FogStart;
    float    FogEnd;
    float    CelEnabled;       // 0 = Blinn-Phong, >0.5 = cel-shaded
    float    CelRimIntensity;  // Rim/fresnel highlight strength
};

// b3 — Sky — bound VS + PS
struct CBSky {
    XMFLOAT3 ZenithColor;
    float    Brightness;
    XMFLOAT3 HorizonColor;
    float    HorizonFalloff;
    XMFLOAT3 GroundColor;
    float    SunDiscSize;        // cosine angle (e.g. 0.9998)
    float    SunGlowIntensity;
    float    SunGlowFalloff;
    float    CloudCoverage;
    float    CloudSpeed;
    float    CloudDensity;
    float    CloudHeight;
    XMFLOAT2 _cloudPad;         // Align CloudColor to 16-byte boundary (matches HLSL packing)
    XMFLOAT3 CloudColor;
    float    CloudSunInfluence;
};

// b4 — Shadow — bound VS + PS
struct CBShadow {
    XMFLOAT4X4 LightViewProjection;
    float    ShadowBias;
    float    ShadowNormalBias;
    float    ShadowIntensity;
    float    ShadowMapSize;
};

// b7 — Bone matrices for GPU skinning — bound VS
static constexpr int MAX_BONES = 64;
struct CBBones {
    XMFLOAT4X4 BoneMatrices[MAX_BONES];
};

// ============================================================
// Matrix Helpers
// ============================================================

// Compute inverse-transpose of a matrix for correct normal transformation
// under non-uniform scaling.  Zero translation first since normals are directions.
inline XMMATRIX InverseTranspose(XMMATRIX M) {
    M.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    XMVECTOR det = XMMatrixDeterminant(M);
    return XMMatrixTranspose(XMMatrixInverse(&det, M));
}

} // namespace WT
