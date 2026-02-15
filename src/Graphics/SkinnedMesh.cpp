#include "SkinnedMesh.h"
#include "Util/Log.h"
#include <fstream>
#include <algorithm>

namespace WT {

// ============================================================
// Loading
// ============================================================

bool SkinnedMesh::LoadFromFile(ID3D11Device* device, const std::wstring& filepath) {
    Release();

    // Open binary file
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("SkinnedMesh: Cannot open file");
        return false;
    }

    // Read header
    char magic[4];
    file.read(magic, 4);
    if (magic[0] != 'S' || magic[1] != 'M' || magic[2] != 'S' || magic[3] != 'H') {
        LOG_ERROR("SkinnedMesh: Invalid file magic");
        return false;
    }

    uint32_t version, numVerts, numIndices, numBones;
    file.read(reinterpret_cast<char*>(&version), 4);
    file.read(reinterpret_cast<char*>(&numVerts), 4);
    file.read(reinterpret_cast<char*>(&numIndices), 4);
    file.read(reinterpret_cast<char*>(&numBones), 4);

    if (version != 1) {
        LOG_ERROR("SkinnedMesh: Unsupported version");
        return false;
    }

    LOG_INFO("SkinnedMesh: Loading %u verts, %u indices, %u bones", numVerts, numIndices, numBones);

    // Read vertices
    std::vector<VertexSkinned> vertices(numVerts);
    file.read(reinterpret_cast<char*>(vertices.data()), numVerts * sizeof(VertexSkinned));

    // Read indices
    std::vector<UINT> indices(numIndices);
    file.read(reinterpret_cast<char*>(indices.data()), numIndices * sizeof(UINT));

    // Read bones
    m_bones.resize(numBones);
    for (uint32_t i = 0; i < numBones; i++) {
        uint8_t nameLen;
        file.read(reinterpret_cast<char*>(&nameLen), 1);
        m_bones[i].name.resize(nameLen);
        file.read(&m_bones[i].name[0], nameLen);
        file.read(reinterpret_cast<char*>(&m_bones[i].parentIndex), 4);

        // Inverse bind pose (16 floats, row-major)
        float mat[16];
        file.read(reinterpret_cast<char*>(mat), 64);
        // Store as column-major (DirectX XMFLOAT4X4 is row-major in memory
        // but HLSL expects column-major when we transpose for GPU)
        m_bones[i].inverseBindPose = {
            mat[0], mat[1], mat[2], mat[3],
            mat[4], mat[5], mat[6], mat[7],
            mat[8], mat[9], mat[10], mat[11],
            mat[12], mat[13], mat[14], mat[15]
        };

        // Bind pose (16 floats, row-major)
        file.read(reinterpret_cast<char*>(mat), 64);
        m_bones[i].bindPose = {
            mat[0], mat[1], mat[2], mat[3],
            mat[4], mat[5], mat[6], mat[7],
            mat[8], mat[9], mat[10], mat[11],
            mat[12], mat[13], mat[14], mat[15]
        };

        m_boneNameToIndex[m_bones[i].name] = static_cast<int>(i);
    }

    file.close();

    // Create vertex buffer
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage     = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = static_cast<UINT>(sizeof(VertexSkinned) * numVerts);
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vsd = {};
    vsd.pSysMem = vertices.data();

    HRESULT hr = device->CreateBuffer(&vbd, &vsd, m_vertexBuffer.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("SkinnedMesh: Failed to create vertex buffer");
        return false;
    }

    // Create index buffer
    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage     = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = static_cast<UINT>(sizeof(UINT) * numIndices);
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA isd = {};
    isd.pSysMem = indices.data();

    hr = device->CreateBuffer(&ibd, &isd, m_indexBuffer.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("SkinnedMesh: Failed to create index buffer");
        return false;
    }

    m_vertexCount = numVerts;
    m_indexCount  = numIndices;

    // Initialize transform arrays
    m_localTransforms.resize(numBones);
    m_worldPoses.resize(numBones);
    m_finalMatrices.resize(numBones);

    // Set to bind pose
    ResetToBindPose();

    LOG_INFO("SkinnedMesh: Loaded successfully (%u bones)", numBones);
    for (uint32_t i = 0; i < numBones; i++) {
        LOG_INFO("  Bone %u: %s (parent=%d)", i, m_bones[i].name.c_str(), m_bones[i].parentIndex);
    }

    return true;
}

void SkinnedMesh::Release() {
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();
    m_vertexCount = 0;
    m_indexCount  = 0;
    m_bones.clear();
    m_boneNameToIndex.clear();
    m_animations.clear();
    m_localTransforms.clear();
    m_worldPoses.clear();
    m_finalMatrices.clear();
}

// ============================================================
// Animation
// ============================================================

void SkinnedMesh::AddAnimation(const std::string& name, const AnimationClip& clip) {
    m_animations[name] = clip;
}

void SkinnedMesh::PlayAnimation(const std::string& name, float blendTime) {
    if (m_currentAnim == name) return;

    if (blendTime > 0.0f && !m_currentAnim.empty()) {
        m_prevAnim     = m_currentAnim;
        m_prevAnimTime = m_animTime;
        m_blendTime    = blendTime;
        m_blendTimer   = blendTime;
    }

    m_currentAnim = name;
    m_animTime    = 0.0f;
}

void SkinnedMesh::Update(float deltaTime) {
    if (m_bones.empty()) return;

    // Advance animation time
    m_animTime += deltaTime;

    // Advance blend timer
    if (m_blendTimer > 0.0f) {
        m_blendTimer -= deltaTime;
        m_prevAnimTime += deltaTime;
        if (m_blendTimer <= 0.0f) {
            m_blendTimer = 0.0f;
            m_prevAnim.clear();
        }
    }

    // If we have a clip-based animation, compute local transforms
    auto it = m_animations.find(m_currentAnim);
    if (it != m_animations.end()) {
        const auto& clip = it->second;
        float animTime = m_animTime;
        if (clip.looping && clip.duration > 0.0f) {
            animTime = fmodf(animTime, clip.duration);
        } else if (animTime > clip.duration) {
            animTime = clip.duration;
        }

        for (int i = 0; i < static_cast<int>(m_bones.size()); i++) {
            if (i < static_cast<int>(clip.boneKeyframes.size()) && !clip.boneKeyframes[i].empty()) {
                XMMATRIX local = InterpolateBone(clip.boneKeyframes[i], animTime);
                XMStoreFloat4x4(&m_localTransforms[i], local);
            }
        }
    }

    // Compute final matrices
    ComputeFinalMatrices();
}

void SkinnedMesh::ResetToBindPose() {
    for (int i = 0; i < static_cast<int>(m_bones.size()); i++) {
        // Local transform = bind pose (if root) or relative to parent
        if (m_bones[i].parentIndex < 0) {
            m_localTransforms[i] = m_bones[i].bindPose;
        } else {
            // Local = inverse(parentBindPose) * thisBindPose
            XMMATRIX parentBind = XMLoadFloat4x4(&m_bones[m_bones[i].parentIndex].bindPose);
            XMMATRIX thisBind   = XMLoadFloat4x4(&m_bones[i].bindPose);
            XMVECTOR det;
            XMMATRIX parentInv  = XMMatrixInverse(&det, parentBind);
            XMMATRIX local      = XMMatrixMultiply(thisBind, parentInv);
            // Actually: local = inv(parent_world) * this_world
            // For skinning: worldPose = local * parentWorld
            // So local = thisWorld * inv(parentWorld)
            local = XMMatrixMultiply(parentInv, thisBind);
            // Hmm, this depends on multiplication convention. Let me use:
            // worldPose[i] = localTransform[i] * worldPose[parent]
            // So localTransform = worldPose[i] * inverse(worldPose[parent])
            // In row-major (DirectXMath): v * M convention
            // worldPose = local * parentWorld
            // local = thisWorld * inv(parentWorld)
            XMStoreFloat4x4(&m_localTransforms[i], XMMatrixMultiply(thisBind, parentInv));
        }
    }
    ComputeFinalMatrices();
}

void SkinnedMesh::SetBoneLocalTransform(int boneIndex, const XMMATRIX& localTransform) {
    if (boneIndex >= 0 && boneIndex < static_cast<int>(m_localTransforms.size())) {
        XMStoreFloat4x4(&m_localTransforms[boneIndex], localTransform);
    }
}

void SkinnedMesh::ComputeFinalMatrices() {
    int numBones = static_cast<int>(m_bones.size());

    // Compute world poses by walking the hierarchy
    for (int i = 0; i < numBones; i++) {
        XMMATRIX local = XMLoadFloat4x4(&m_localTransforms[i]);

        if (m_bones[i].parentIndex >= 0 && m_bones[i].parentIndex < numBones) {
            XMMATRIX parentWorld = XMLoadFloat4x4(&m_worldPoses[m_bones[i].parentIndex]);
            // worldPose = local * parentWorld  (row-major, v*M convention)
            XMMATRIX world = XMMatrixMultiply(local, parentWorld);
            XMStoreFloat4x4(&m_worldPoses[i], world);
        } else {
            // Root bone: world = local
            XMStoreFloat4x4(&m_worldPoses[i], local);
        }
    }

    // Final matrix = inverseBindPose * worldPose
    // In the shader: skinnedPos = mul(float4(pos, 1), finalMatrix)
    // This transforms from bind-pose model space → bone space → current world pose
    for (int i = 0; i < numBones; i++) {
        XMMATRIX invBind = XMLoadFloat4x4(&m_bones[i].inverseBindPose);
        XMMATRIX worldPose = XMLoadFloat4x4(&m_worldPoses[i]);

        // finalMatrix = invBindPose * animatedWorldPose
        XMMATRIX final_mat = XMMatrixMultiply(invBind, worldPose);

        // Transpose for HLSL (HLSL reads column-major, we store row-major)
        XMStoreFloat4x4(&m_finalMatrices[i], XMMatrixTranspose(final_mat));
    }
}

int SkinnedMesh::FindBone(const std::string& name) const {
    auto it = m_boneNameToIndex.find(name);
    return (it != m_boneNameToIndex.end()) ? it->second : -1;
}

XMMATRIX SkinnedMesh::InterpolateBone(const std::vector<BoneKeyframe>& keyframes, float time) const {
    if (keyframes.empty()) {
        return XMMatrixIdentity();
    }

    if (keyframes.size() == 1 || time <= keyframes[0].time) {
        const auto& kf = keyframes[0];
        XMMATRIX S = XMMatrixScaling(kf.scale.x, kf.scale.y, kf.scale.z);
        XMVECTOR Q = XMLoadFloat4(&kf.rotation);
        XMMATRIX R = XMMatrixRotationQuaternion(Q);
        XMMATRIX T = XMMatrixTranslation(kf.translation.x, kf.translation.y, kf.translation.z);
        return S * R * T;
    }

    if (time >= keyframes.back().time) {
        const auto& kf = keyframes.back();
        XMMATRIX S = XMMatrixScaling(kf.scale.x, kf.scale.y, kf.scale.z);
        XMVECTOR Q = XMLoadFloat4(&kf.rotation);
        XMMATRIX R = XMMatrixRotationQuaternion(Q);
        XMMATRIX T = XMMatrixTranslation(kf.translation.x, kf.translation.y, kf.translation.z);
        return S * R * T;
    }

    // Find surrounding keyframes
    int idx = 0;
    for (int i = 0; i < static_cast<int>(keyframes.size()) - 1; i++) {
        if (time < keyframes[i + 1].time) {
            idx = i;
            break;
        }
    }

    const auto& kf0 = keyframes[idx];
    const auto& kf1 = keyframes[idx + 1];
    float t = (time - kf0.time) / (kf1.time - kf0.time);

    // Lerp translation + scale, slerp rotation
    XMVECTOR t0 = XMLoadFloat3(&kf0.translation);
    XMVECTOR t1 = XMLoadFloat3(&kf1.translation);
    XMVECTOR trans = XMVectorLerp(t0, t1, t);

    XMVECTOR s0 = XMLoadFloat3(&kf0.scale);
    XMVECTOR s1 = XMLoadFloat3(&kf1.scale);
    XMVECTOR scl = XMVectorLerp(s0, s1, t);

    XMVECTOR q0 = XMLoadFloat4(&kf0.rotation);
    XMVECTOR q1 = XMLoadFloat4(&kf1.rotation);
    XMVECTOR rot = XMQuaternionSlerp(q0, q1, t);

    XMFLOAT3 transF, sclF;
    XMStoreFloat3(&transF, trans);
    XMStoreFloat3(&sclF, scl);

    XMMATRIX S = XMMatrixScaling(sclF.x, sclF.y, sclF.z);
    XMMATRIX R = XMMatrixRotationQuaternion(rot);
    XMMATRIX T = XMMatrixTranslation(transF.x, transF.y, transF.z);
    return S * R * T;
}

// ============================================================
// Drawing
// ============================================================

void SkinnedMesh::Draw(ID3D11DeviceContext* context) const {
    if (!m_vertexBuffer || !m_indexBuffer) return;

    UINT stride = sizeof(VertexSkinned);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->DrawIndexed(m_indexCount, 0, 0);
}

} // namespace WT
