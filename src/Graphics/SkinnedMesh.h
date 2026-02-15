#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <unordered_map>
#include "Util/MathHelpers.h"

namespace WT {

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// ============================================================
// Bone data — one per joint in the skeleton
// ============================================================
struct Bone {
    std::string name;
    int         parentIndex = -1;    // -1 = root
    XMFLOAT4X4  inverseBindPose;     // Transforms from model space to bone space
    XMFLOAT4X4  bindPose;            // Bone's world-space bind pose
};

// ============================================================
// Animation keyframe for a single bone
// ============================================================
struct BoneKeyframe {
    float      time;           // Time in seconds
    XMFLOAT3   translation;
    XMFLOAT4   rotation;      // Quaternion (x, y, z, w)
    XMFLOAT3   scale;
};

// ============================================================
// Animation clip — keyframes for all bones
// ============================================================
struct AnimationClip {
    std::string name;
    float       duration = 0.0f;
    bool        looping  = true;
    // Per-bone keyframes: boneKeyframes[boneIndex] = sorted vector of keyframes
    std::vector<std::vector<BoneKeyframe>> boneKeyframes;
};

// ============================================================
// SkinnedMesh — loads .skmesh files (mesh + skeleton)
// Manages bone animation and outputs final bone matrices for GPU
// ============================================================
class SkinnedMesh {
public:
    bool LoadFromFile(ID3D11Device* device, const std::wstring& filepath);
    void Release();

    // Animation
    void AddAnimation(const std::string& name, const AnimationClip& clip);
    void PlayAnimation(const std::string& name, float blendTime = 0.2f);
    void Update(float deltaTime);

    // Get final bone matrices (inverse bind pose × animated world pose)
    // These go directly into CBBones for the GPU
    const std::vector<XMFLOAT4X4>& GetFinalBoneMatrices() const { return m_finalMatrices; }

    // Render
    void Draw(ID3D11DeviceContext* context) const;

    // Queries
    bool IsValid() const { return m_vertexBuffer && m_indexBuffer; }
    UINT GetIndexCount() const { return m_indexCount; }
    UINT GetVertexCount() const { return m_vertexCount; }
    int  GetBoneCount() const { return static_cast<int>(m_bones.size()); }
    int  FindBone(const std::string& name) const;
    const std::vector<Bone>& GetBones() const { return m_bones; }
    const std::vector<XMFLOAT4X4>& GetLocalTransforms() const { return m_localTransforms; }
    const std::vector<XMFLOAT4X4>& GetWorldPoses() const { return m_worldPoses; }

    // Direct bone override (for procedural animation)
    void SetBoneLocalTransform(int boneIndex, const XMMATRIX& localTransform);
    void ComputeFinalMatrices();  // Recompute after setting local transforms

    // Set all bones to bind pose
    void ResetToBindPose();

private:
    // Interpolate keyframes
    XMMATRIX InterpolateBone(const std::vector<BoneKeyframe>& keyframes, float time) const;

    // GPU buffers
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    UINT m_vertexCount  = 0;
    UINT m_indexCount   = 0;

    // Skeleton
    std::vector<Bone> m_bones;
    std::unordered_map<std::string, int> m_boneNameToIndex;

    // Animation state
    std::unordered_map<std::string, AnimationClip> m_animations;
    std::string m_currentAnim;
    float       m_animTime    = 0.0f;
    float       m_blendTime   = 0.0f;
    float       m_blendTimer  = 0.0f;
    std::string m_prevAnim;
    float       m_prevAnimTime = 0.0f;

    // Per-bone local transforms (for procedural animation override)
    std::vector<XMFLOAT4X4> m_localTransforms;

    // Final output: boneMatrix[i] = inverseBindPose[i] * worldPose[i]
    std::vector<XMFLOAT4X4> m_finalMatrices;
    std::vector<XMFLOAT4X4> m_worldPoses;
};

} // namespace WT
