// ============================================================
// SkinnedVS.hlsl — GPU skinned vertex shader
// Applies bone matrix palette to transform vertices before
// passing to the standard lighting pipeline.
// ============================================================

#include "Common.hlsli"

// Bone matrix palette at register b7
// Each matrix = inverseBindPose * animatedWorldPose (pre-transposed)
#define MAX_BONES 64
cbuffer CBBones : register(b7) {
    float4x4 gBones[MAX_BONES];
};

// Skinned vertex input
struct SkinnedVSInput {
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float2 TexCoord     : TEXCOORD0;
    uint4  BoneIndices  : BLENDINDICES;
    float4 BoneWeights  : BLENDWEIGHT;
};

VoxelPSInput VSMain(SkinnedVSInput input) {
    VoxelPSInput output;

    // Compute skinned position and normal
    float4 skinnedPos = float4(0, 0, 0, 0);
    float3 skinnedNormal = float3(0, 0, 0);

    // Apply up to 4 bone influences
    for (int i = 0; i < 4; i++) {
        float w = 0;
        if (i == 0) w = input.BoneWeights.x;
        else if (i == 1) w = input.BoneWeights.y;
        else if (i == 2) w = input.BoneWeights.z;
        else w = input.BoneWeights.w;

        if (w > 0.0f) {
            uint boneIdx = 0;
            if (i == 0) boneIdx = input.BoneIndices.x;
            else if (i == 1) boneIdx = input.BoneIndices.y;
            else if (i == 2) boneIdx = input.BoneIndices.z;
            else boneIdx = input.BoneIndices.w;

            boneIdx = min(boneIdx, MAX_BONES - 1);

            float4x4 bone = gBones[boneIdx];
            skinnedPos    += w * mul(float4(input.Position, 1.0f), bone);
            skinnedNormal += w * mul(float4(input.Normal, 0.0f), bone).xyz;
        }
    }

    // Transform to world space
    float4 worldPos = mul(skinnedPos, gWorld);
    output.WorldPos = worldPos.xyz;
    output.Position = mul(worldPos, gViewProjection);

    // Transform normal
    output.Normal = normalize(mul(float4(skinnedNormal, 0.0f), gWorldInvTranspose).xyz);

    // Skinned mesh uses white vertex color (texture provides color)
    output.Color = float4(1.0f, 1.0f, 1.0f, 1.0f);

    output.TexCoord = input.TexCoord;

    return output;
}
