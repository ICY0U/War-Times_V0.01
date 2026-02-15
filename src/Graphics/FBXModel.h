#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <unordered_map>
#include "Util/MathHelpers.h"

// Forward-declare ufbx types so we don't include the massive header here
struct ufbx_scene;
struct ufbx_baked_anim;

namespace WT {

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// ============================================================
// FBXModel — loads FBX files via ufbx with skinning + animation
// ============================================================
class FBXModel {
public:
    struct BoneInfo {
        std::string name;
        int         parentIndex = -1;
        XMFLOAT4X4  inverseBindPose;   // geometry_to_bone matrix
        XMFLOAT4X4  bindWorldPose;     // bone world pose at bind time
    };

    // Load model FBX file (mesh + skeleton)
    bool LoadFromFile(ID3D11Device* device, const std::wstring& filepath);

    // Load animation from a separate FBX file and add it as a named clip
    bool LoadAnimation(const std::wstring& filepath, const std::string& clipName);

    void Release();

    // Animation playback
    void PlayAnimation(const std::string& name);
    void StopAnimation();
    void Update(float deltaTime);

    // Get final bone matrices for GPU (MAX_BONES count, transposed for HLSL)
    const std::vector<XMFLOAT4X4>& GetFinalBoneMatrices() const { return m_finalMatrices; }

    // Rendering
    void Draw(ID3D11DeviceContext* context) const;

    // Queries
    bool IsValid() const { return m_vertexBuffer && m_indexBuffer; }
    UINT GetIndexCount() const { return m_indexCount; }
    UINT GetVertexCount() const { return m_vertexCount; }
    int  GetBoneCount() const { return static_cast<int>(m_bones.size()); }
    int  FindBone(const std::string& name) const;
    const std::vector<BoneInfo>& GetBones() const { return m_bones; }
    bool HasAnimation(const std::string& name) const;
    bool IsAnimating() const { return !m_currentAnim.empty(); }
    float GetAnimTime() const { return m_animTime; }

    // Material colors assigned by material index
    void SetMaterialColor(int materialIndex, const XMFLOAT4& color);

private:
    // GPU buffers
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    UINT m_vertexCount  = 0;
    UINT m_indexCount   = 0;

    // Skeleton
    std::vector<BoneInfo> m_bones;
    std::unordered_map<std::string, int> m_boneNameToIndex;

    // Animation data stored as baked keyframes
    struct BakedAnimClip {
        std::string name;
        float duration = 0.0f;
        bool  looping  = true;

        // Per-bone baked keyframes
        struct BoneChannel {
            int boneIndex = -1;
            // Translation keyframes
            struct Vec3Key { float time; XMFLOAT3 value; };
            std::vector<Vec3Key> posKeys;
            // Rotation keyframes
            struct QuatKey { float time; XMFLOAT4 value; };
            std::vector<QuatKey> rotKeys;
            // Scale keyframes
            struct Vec3Key2 { float time; XMFLOAT3 value; };
            std::vector<Vec3Key2> scaleKeys;
        };
        std::vector<BoneChannel> channels;
    };

    std::unordered_map<std::string, BakedAnimClip> m_animations;
    std::string m_currentAnim;
    float       m_animTime = 0.0f;

    // Per-bone transforms
    std::vector<XMFLOAT4X4> m_localTransforms;
    std::vector<XMFLOAT4X4> m_worldPoses;
    std::vector<XMFLOAT4X4> m_finalMatrices;

    // Bind pose local transforms (for resetting)
    std::vector<XMFLOAT4X4> m_bindLocalTransforms;

    // Material colors (indexed by material index from FBX)
    std::unordered_map<int, XMFLOAT4> m_materialColors;

    // Internal helpers
    void ComputeFinalMatrices();
    void ResetToBindPose();
    XMFLOAT3 InterpolateVec3(const std::vector<BakedAnimClip::BoneChannel::Vec3Key>& keys, float time) const;
    XMFLOAT4 InterpolateQuat(const std::vector<BakedAnimClip::BoneChannel::QuatKey>& keys, float time) const;

    // The ufbx scene for the model (kept for bone lookups during animation loading)
    ufbx_scene* m_modelScene = nullptr;
};

} // namespace WT
