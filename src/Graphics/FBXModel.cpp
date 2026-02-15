#include "FBXModel.h"
#include "Util/Log.h"
#include "ThirdParty/ufbx.h"
#include <algorithm>
#include <cstring>

namespace WT {

// ============================================================
// Helper: convert wstring filepath to narrow string for ufbx
// ============================================================
static std::string WideToNarrow(const std::wstring& wide) {
    std::string narrow;
    narrow.reserve(wide.size());
    for (wchar_t c : wide) {
        narrow.push_back(static_cast<char>(c)); // Simple ASCII conversion
    }
    return narrow;
}

// ============================================================
// Helper: convert ufbx_matrix to XMFLOAT4X4
// ufbx uses column-vector convention (M * v), DirectXMath uses row-vector (v * M)
// so we must transpose: row i of result = column i of ufbx
// ufbx_matrix: cols[0..2] = basis vectors, cols[3] = translation
// ============================================================
static XMFLOAT4X4 UfbxMatrixToXM(const ufbx_matrix& m) {
    return XMFLOAT4X4(
        (float)m.m00, (float)m.m10, (float)m.m20, 0.0f,  // Row 0 = ufbx col 0 (X basis)
        (float)m.m01, (float)m.m11, (float)m.m21, 0.0f,  // Row 1 = ufbx col 1 (Y basis)
        (float)m.m02, (float)m.m12, (float)m.m22, 0.0f,  // Row 2 = ufbx col 2 (Z basis)
        (float)m.m03, (float)m.m13, (float)m.m23, 1.0f   // Row 3 = ufbx col 3 (translation)
    );
}

// ============================================================
// LoadFromFile — loads FBX mesh + skeleton
// ============================================================
bool FBXModel::LoadFromFile(ID3D11Device* device, const std::wstring& filepath) {
    Release();

    std::string path = WideToNarrow(filepath);

    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.generate_missing_normals = true;

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (!scene) {
        char errBuf[512];
        ufbx_format_error(errBuf, sizeof(errBuf), &error);
        LOG_ERROR("FBXModel: Failed to load '%s': %s", path.c_str(), errBuf);
        return false;
    }

    LOG_INFO("FBXModel: Loaded scene with %zu meshes, %zu nodes, %zu anim_stacks",
             scene->meshes.count, scene->nodes.count, scene->anim_stacks.count);

    if (scene->meshes.count == 0) {
        LOG_ERROR("FBXModel: No meshes found in file");
        ufbx_free_scene(scene);
        return false;
    }

    for (size_t mi = 0; mi < scene->meshes.count; mi++) {
        LOG_INFO("FBXModel: Mesh[%zu] '%s': %zu verts, %zu faces",
                 mi, scene->meshes.data[mi]->name.data,
                 scene->meshes.data[mi]->num_vertices,
                 scene->meshes.data[mi]->num_faces);
    }

    // ---- Extract skeleton from any skin deformer ----
    ufbx_skin_deformer* skin = nullptr;
    for (size_t mi = 0; mi < scene->meshes.count; mi++) {
        if (scene->meshes.data[mi]->skin_deformers.count > 0) {
            skin = scene->meshes.data[mi]->skin_deformers.data[0];
            break;
        }
    }

    if (skin) {
        LOG_INFO("FBXModel: Found skin deformer with %zu clusters (bones)", skin->clusters.count);

        m_bones.resize(skin->clusters.count);

        // Build bone info from clusters
        for (size_t i = 0; i < skin->clusters.count; i++) {
            ufbx_skin_cluster* cluster = skin->clusters.data[i];
            ufbx_node* boneNode = cluster->bone_node;

            m_bones[i].name = boneNode ? std::string(boneNode->name.data, boneNode->name.length) : "";
            m_bones[i].inverseBindPose = UfbxMatrixToXM(cluster->geometry_to_bone);

            if (boneNode) {
                m_bones[i].bindWorldPose = UfbxMatrixToXM(boneNode->node_to_world);
                m_boneNameToIndex[m_bones[i].name] = static_cast<int>(i);
            }
        }

        // Determine parent indices by walking the node hierarchy
        for (size_t i = 0; i < skin->clusters.count; i++) {
            ufbx_node* boneNode = skin->clusters.data[i]->bone_node;
            if (!boneNode || !boneNode->parent) {
                m_bones[i].parentIndex = -1;
                continue;
            }

            // Find parent in our bone list
            std::string parentName(boneNode->parent->name.data, boneNode->parent->name.length);
            auto it = m_boneNameToIndex.find(parentName);
            if (it != m_boneNameToIndex.end()) {
                m_bones[i].parentIndex = it->second;
            } else {
                m_bones[i].parentIndex = -1; // Parent not in skeleton
            }
        }

        // Log bone hierarchy
        for (size_t i = 0; i < m_bones.size(); i++) {
            LOG_INFO("  Bone %zu: '%s' parent=%d", i, m_bones[i].name.c_str(), m_bones[i].parentIndex);
        }
    } else {
        LOG_INFO("FBXModel: No skin deformer — static mesh");
    }

    // ---- Triangulate and build vertex/index buffers from ALL meshes ----
    std::vector<VertexSkinned> vertices;
    std::vector<UINT> indices;

    struct VertSkinData {
        uint8_t  boneIndices[4] = {0, 0, 0, 0};
        XMFLOAT4 boneWeights    = {0, 0, 0, 0};
    };

    for (size_t mi = 0; mi < scene->meshes.count; mi++) {
        ufbx_mesh* fbxMesh = scene->meshes.data[mi];

        // Find the skin deformer for this specific mesh
        ufbx_skin_deformer* meshSkin = nullptr;
        if (fbxMesh->skin_deformers.count > 0)
            meshSkin = fbxMesh->skin_deformers.data[0];

        // Get geometry-to-world transform for this mesh's node
        // (needed for unskinned meshes that might be in local space)
        XMMATRIX meshTransform = XMMatrixIdentity();
        if (fbxMesh->instances.count > 0) {
            ufbx_node* meshNode = fbxMesh->instances.data[0];
            XMFLOAT4X4 geoToWorld = UfbxMatrixToXM(meshNode->geometry_to_world);
            meshTransform = XMLoadFloat4x4(&geoToWorld);
        }
        bool hasSkin = (meshSkin != nullptr);

        // Temporary triangle index buffer for ufbx_triangulate_face
        std::vector<uint32_t> triIndices(fbxMesh->max_face_triangles * 3);

        // Build per-vertex skin weights for this mesh
        std::vector<VertSkinData> skinData;
        if (meshSkin) {
            skinData.resize(fbxMesh->num_vertices);
            for (size_t vi = 0; vi < meshSkin->vertices.count && vi < fbxMesh->num_vertices; vi++) {
                ufbx_skin_vertex sv = meshSkin->vertices.data[vi];
                float weights[4] = {0, 0, 0, 0};
                uint8_t bones[4] = {0, 0, 0, 0};
                int count = (int)sv.num_weights;
                if (count > 4) count = 4;
                float totalW = 0.0f;
                for (int w = 0; w < count; w++) {
                    ufbx_skin_weight sw = meshSkin->weights.data[sv.weight_begin + w];
                    bones[w]   = (uint8_t)sw.cluster_index;
                    weights[w] = (float)sw.weight;
                    totalW += weights[w];
                }
                if (totalW > 0.0f) {
                    for (int w = 0; w < 4; w++) weights[w] /= totalW;
                } else {
                    weights[0] = 1.0f;
                }
                skinData[vi].boneIndices[0] = bones[0];
                skinData[vi].boneIndices[1] = bones[1];
                skinData[vi].boneIndices[2] = bones[2];
                skinData[vi].boneIndices[3] = bones[3];
                skinData[vi].boneWeights = { weights[0], weights[1], weights[2], weights[3] };
            }
        }

        // Process each face
        for (size_t fi = 0; fi < fbxMesh->num_faces; fi++) {
            ufbx_face face = fbxMesh->faces.data[fi];
            uint32_t numTris = ufbx_triangulate_face(triIndices.data(), triIndices.size(), fbxMesh, face);

            for (uint32_t ti = 0; ti < numTris * 3; ti++) {
                uint32_t idx = triIndices[ti];

                VertexSkinned v = {};
                memset(&v, 0, sizeof(v));

                // Position
                ufbx_vec3 pos = ufbx_get_vertex_vec3(&fbxMesh->vertex_position, idx);
                if (!hasSkin) {
                    // Transform unskinned mesh vertices to world space
                    XMVECTOR p = XMVector3Transform(
                        XMVectorSet((float)pos.x, (float)pos.y, (float)pos.z, 1.0f), meshTransform);
                    XMStoreFloat3(&v.Position, p);
                } else {
                    v.Position = { (float)pos.x, (float)pos.y, (float)pos.z };
                }

                // Normal
                if (fbxMesh->vertex_normal.exists) {
                    ufbx_vec3 n = ufbx_get_vertex_vec3(&fbxMesh->vertex_normal, idx);
                    if (!hasSkin) {
                        XMVECTOR nv = XMVector3TransformNormal(
                            XMVectorSet((float)n.x, (float)n.y, (float)n.z, 0.0f), meshTransform);
                        nv = XMVector3Normalize(nv);
                        XMStoreFloat3(&v.Normal, nv);
                    } else {
                        v.Normal = { (float)n.x, (float)n.y, (float)n.z };
                    }
                } else {
                    v.Normal = { 0.0f, 1.0f, 0.0f };
                }

                // UV
                if (fbxMesh->vertex_uv.exists) {
                    ufbx_vec2 uv = ufbx_get_vertex_vec2(&fbxMesh->vertex_uv, idx);
                    v.TexCoord = { (float)uv.x, (float)uv.y };
                }

                // Skin weights
                if (meshSkin && !skinData.empty()) {
                    uint32_t vertIdx = fbxMesh->vertex_indices.data[idx];
                    if (vertIdx < skinData.size()) {
                        v.BoneIndices[0] = skinData[vertIdx].boneIndices[0];
                        v.BoneIndices[1] = skinData[vertIdx].boneIndices[1];
                        v.BoneIndices[2] = skinData[vertIdx].boneIndices[2];
                        v.BoneIndices[3] = skinData[vertIdx].boneIndices[3];
                        v.BoneWeights = skinData[vertIdx].boneWeights;
                    } else {
                        v.BoneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
                    }
                } else {
                    v.BoneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
                }

                indices.push_back(static_cast<UINT>(vertices.size()));
                vertices.push_back(v);
            }
        }

        LOG_INFO("FBXModel: Mesh[%zu] added %zu tris", mi, fbxMesh->num_triangles);
    } // end for each mesh

    LOG_INFO("FBXModel: Total: %zu vertices, %zu indices (%zu triangles)",
             vertices.size(), indices.size(), indices.size() / 3);

    if (vertices.empty()) {
        LOG_ERROR("FBXModel: No vertices generated");
        ufbx_free_scene(scene);
        return false;
    }

    // ---- Create GPU buffers ----
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage     = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = static_cast<UINT>(sizeof(VertexSkinned) * vertices.size());
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vsd = {};
    vsd.pSysMem = vertices.data();

    HRESULT hr = device->CreateBuffer(&vbd, &vsd, m_vertexBuffer.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("FBXModel: Failed to create vertex buffer");
        ufbx_free_scene(scene);
        return false;
    }

    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage     = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = static_cast<UINT>(sizeof(UINT) * indices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA isd = {};
    isd.pSysMem = indices.data();

    hr = device->CreateBuffer(&ibd, &isd, m_indexBuffer.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("FBXModel: Failed to create index buffer");
        ufbx_free_scene(scene);
        return false;
    }

    m_vertexCount = static_cast<UINT>(vertices.size());
    m_indexCount  = static_cast<UINT>(indices.size());

    // ---- Initialize bone transforms ----
    if (!m_bones.empty()) {
        int numBones = static_cast<int>(m_bones.size());
        m_localTransforms.resize(numBones);
        m_worldPoses.resize(numBones);
        m_finalMatrices.resize(numBones);
        m_bindLocalTransforms.resize(numBones);

        // Compute bind-pose local transforms
        for (int i = 0; i < numBones; i++) {
            XMMATRIX thisWorld = XMLoadFloat4x4(&m_bones[i].bindWorldPose);
            if (m_bones[i].parentIndex >= 0) {
                XMMATRIX parentWorld = XMLoadFloat4x4(&m_bones[m_bones[i].parentIndex].bindWorldPose);
                XMVECTOR det;
                XMMATRIX parentInv = XMMatrixInverse(&det, parentWorld);
                // local = thisWorld * inv(parentWorld) — row-major v*M convention
                XMMATRIX local = XMMatrixMultiply(thisWorld, parentInv);
                XMStoreFloat4x4(&m_bindLocalTransforms[i], local);
            } else {
                XMStoreFloat4x4(&m_bindLocalTransforms[i], thisWorld);
            }
            m_localTransforms[i] = m_bindLocalTransforms[i];
        }

        ResetToBindPose();
    } else {
        // No bones — add a single identity bone
        m_finalMatrices.resize(1);
        XMStoreFloat4x4(&m_finalMatrices[0], XMMatrixIdentity());
    }

    // ---- Load animations embedded in this FBX ----
    for (size_t si = 0; si < scene->anim_stacks.count; si++) {
        ufbx_anim_stack* stack = scene->anim_stacks.data[si];
        std::string animName(stack->name.data, stack->name.length);
        if (animName.empty()) animName = "default";

        LOG_INFO("FBXModel: Found embedded animation '%s' (%.2f - %.2f sec)",
                 animName.c_str(), stack->time_begin, stack->time_end);

        // Bake the animation
        ufbx_bake_opts bakeOpts = {};
        ufbx_error bakeError;
        ufbx_baked_anim* baked = ufbx_bake_anim(scene, stack->anim, &bakeOpts, &bakeError);
        if (!baked) {
            LOG_WARN("FBXModel: Failed to bake animation '%s'", animName.c_str());
            continue;
        }

        BakedAnimClip clip;
        clip.name = animName;
        clip.duration = (float)(baked->playback_duration > 0.0 ? baked->playback_duration : (stack->time_end - stack->time_begin));

        // Extract baked keyframes for each bone
        for (size_t ni = 0; ni < baked->nodes.count; ni++) {
            ufbx_baked_node& bn = baked->nodes.data[ni];

            // Find which bone this corresponds to
            if (bn.typed_id >= scene->nodes.count) continue;
            ufbx_node* node = scene->nodes.data[bn.typed_id];
            std::string nodeName(node->name.data, node->name.length);

            auto boneIt = m_boneNameToIndex.find(nodeName);
            if (boneIt == m_boneNameToIndex.end()) continue;

            BakedAnimClip::BoneChannel ch;
            ch.boneIndex = boneIt->second;

            // Translation keys
            for (size_t ki = 0; ki < bn.translation_keys.count; ki++) {
                auto& k = bn.translation_keys.data[ki];
                ch.posKeys.push_back({ (float)k.time, { (float)k.value.x, (float)k.value.y, (float)k.value.z } });
            }

            // Rotation keys
            for (size_t ki = 0; ki < bn.rotation_keys.count; ki++) {
                auto& k = bn.rotation_keys.data[ki];
                ch.rotKeys.push_back({ (float)k.time, { (float)k.value.x, (float)k.value.y, (float)k.value.z, (float)k.value.w } });
            }

            // Scale keys
            for (size_t ki = 0; ki < bn.scale_keys.count; ki++) {
                auto& k = bn.scale_keys.data[ki];
                ch.scaleKeys.push_back({ (float)k.time, { (float)k.value.x, (float)k.value.y, (float)k.value.z } });
            }

            clip.channels.push_back(std::move(ch));
        }

        LOG_INFO("FBXModel: Baked animation '%s': %.2f sec, %zu bone channels",
                 clip.name.c_str(), clip.duration, clip.channels.size());

        m_animations[animName] = std::move(clip);
        ufbx_free_baked_anim(baked);
    }

    // Keep scene for animation loading
    m_modelScene = scene;

    LOG_INFO("FBXModel: Ready (%u verts, %u indices, %zu bones, %zu animations)",
             m_vertexCount, m_indexCount, m_bones.size(), m_animations.size());
    return true;
}

// ============================================================
// LoadAnimation — load animation from a separate FBX file
// ============================================================
bool FBXModel::LoadAnimation(const std::wstring& filepath, const std::string& clipName) {
    std::string path = WideToNarrow(filepath);

    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;

    ufbx_error error;
    ufbx_scene* animScene = ufbx_load_file(path.c_str(), &opts, &error);
    if (!animScene) {
        char errBuf[512];
        ufbx_format_error(errBuf, sizeof(errBuf), &error);
        LOG_ERROR("FBXModel: Failed to load animation '%s': %s", path.c_str(), errBuf);
        return false;
    }

    LOG_INFO("FBXModel: Loading animation from '%s' (%zu anim_stacks, %zu nodes)",
             path.c_str(), animScene->anim_stacks.count, animScene->nodes.count);

    if (animScene->anim_stacks.count == 0) {
        LOG_ERROR("FBXModel: No animation stacks in '%s'", path.c_str());
        ufbx_free_scene(animScene);
        return false;
    }

    ufbx_anim_stack* stack = animScene->anim_stacks.data[0];

    // Bake the animation
    ufbx_bake_opts bakeOpts = {};
    ufbx_error bakeError;
    ufbx_baked_anim* baked = ufbx_bake_anim(animScene, stack->anim, &bakeOpts, &bakeError);
    if (!baked) {
        LOG_WARN("FBXModel: Failed to bake animation from '%s'", path.c_str());
        ufbx_free_scene(animScene);
        return false;
    }

    BakedAnimClip clip;
    clip.name = clipName;
    clip.duration = (float)(baked->playback_duration > 0.0 ? baked->playback_duration : (stack->time_end - stack->time_begin));

    LOG_INFO("FBXModel: Animation '%s' duration=%.2f sec, %zu baked nodes",
             clipName.c_str(), clip.duration, baked->nodes.count);

    // Map animation bones to our skeleton by name
    for (size_t ni = 0; ni < baked->nodes.count; ni++) {
        ufbx_baked_node& bn = baked->nodes.data[ni];

        // Get node name from the animation scene
        if (bn.typed_id >= animScene->nodes.count) continue;
        ufbx_node* node = animScene->nodes.data[bn.typed_id];
        std::string nodeName(node->name.data, node->name.length);

        // Find matching bone in our skeleton
        auto boneIt = m_boneNameToIndex.find(nodeName);
        if (boneIt == m_boneNameToIndex.end()) continue;

        BakedAnimClip::BoneChannel ch;
        ch.boneIndex = boneIt->second;

        for (size_t ki = 0; ki < bn.translation_keys.count; ki++) {
            auto& k = bn.translation_keys.data[ki];
            ch.posKeys.push_back({ (float)k.time, { (float)k.value.x, (float)k.value.y, (float)k.value.z } });
        }
        for (size_t ki = 0; ki < bn.rotation_keys.count; ki++) {
            auto& k = bn.rotation_keys.data[ki];
            ch.rotKeys.push_back({ (float)k.time, { (float)k.value.x, (float)k.value.y, (float)k.value.z, (float)k.value.w } });
        }
        for (size_t ki = 0; ki < bn.scale_keys.count; ki++) {
            auto& k = bn.scale_keys.data[ki];
            ch.scaleKeys.push_back({ (float)k.time, { (float)k.value.x, (float)k.value.y, (float)k.value.z } });
        }

        clip.channels.push_back(std::move(ch));
    }

    LOG_INFO("FBXModel: Animation '%s' mapped %zu bone channels", clipName.c_str(), clip.channels.size());

    m_animations[clipName] = std::move(clip);

    ufbx_free_baked_anim(baked);
    ufbx_free_scene(animScene);
    return true;
}

// ============================================================
// Animation playback
// ============================================================
void FBXModel::PlayAnimation(const std::string& name) {
    if (m_currentAnim == name) return;
    if (m_animations.find(name) == m_animations.end()) {
        LOG_WARN("FBXModel: Animation '%s' not found", name.c_str());
        return;
    }
    m_currentAnim = name;
    m_animTime = 0.0f;
}

void FBXModel::StopAnimation() {
    m_currentAnim.clear();
    m_animTime = 0.0f;
    ResetToBindPose();
}

void FBXModel::Update(float deltaTime) {
    if (m_bones.empty()) return;

    if (!m_currentAnim.empty()) {
        auto it = m_animations.find(m_currentAnim);
        if (it != m_animations.end()) {
            const auto& clip = it->second;
            m_animTime += deltaTime;
            if (clip.looping && clip.duration > 0.0f) {
                m_animTime = fmodf(m_animTime, clip.duration);
            } else if (m_animTime > clip.duration) {
                m_animTime = clip.duration;
            }

            // Start from bind pose
            for (size_t i = 0; i < m_localTransforms.size(); i++) {
                m_localTransforms[i] = m_bindLocalTransforms[i];
            }

            // Apply animation channels
            for (const auto& ch : clip.channels) {
                if (ch.boneIndex < 0 || ch.boneIndex >= static_cast<int>(m_bones.size())) continue;

                // For root bones (no parent), keep bind pose entirely.
                // Character position/rotation is controlled by Application's world matrix.
                // Only animate child bones (limbs, spine, etc.).
                if (m_bones[ch.boneIndex].parentIndex < 0) {
                    m_localTransforms[ch.boneIndex] = m_bindLocalTransforms[ch.boneIndex];
                    continue;
                }

                XMFLOAT3 pos = InterpolateVec3(ch.posKeys, m_animTime);
                XMFLOAT4 rot = InterpolateQuat(ch.rotKeys, m_animTime);
                XMFLOAT3 scl = { 1.0f, 1.0f, 1.0f };
                if (!ch.scaleKeys.empty()) {
                    scl = InterpolateVec3(reinterpret_cast<const std::vector<BakedAnimClip::BoneChannel::Vec3Key>&>(ch.scaleKeys), m_animTime);
                }

                XMMATRIX S = XMMatrixScaling(scl.x, scl.y, scl.z);
                XMMATRIX R = XMMatrixRotationQuaternion(XMLoadFloat4(&rot));
                XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);
                XMMATRIX local = S * R * T;
                XMStoreFloat4x4(&m_localTransforms[ch.boneIndex], local);
            }
        }
    }

    ComputeFinalMatrices();
}

// ============================================================
// Bone transform computation
// ============================================================
void FBXModel::ResetToBindPose() {
    for (size_t i = 0; i < m_localTransforms.size(); i++) {
        m_localTransforms[i] = m_bindLocalTransforms[i];
    }
    ComputeFinalMatrices();
}

void FBXModel::ComputeFinalMatrices() {
    int numBones = static_cast<int>(m_bones.size());

    // Compute world poses by walking hierarchy
    for (int i = 0; i < numBones; i++) {
        XMMATRIX local = XMLoadFloat4x4(&m_localTransforms[i]);

        if (m_bones[i].parentIndex >= 0 && m_bones[i].parentIndex < numBones) {
            XMMATRIX parentWorld = XMLoadFloat4x4(&m_worldPoses[m_bones[i].parentIndex]);
            // worldPose = local * parentWorld (row-vector v*M convention)
            XMMATRIX world = XMMatrixMultiply(local, parentWorld);
            XMStoreFloat4x4(&m_worldPoses[i], world);
        } else {
            XMStoreFloat4x4(&m_worldPoses[i], local);
        }
    }

    // final = invBindPose * worldPose, then transpose for HLSL
    for (int i = 0; i < numBones; i++) {
        XMMATRIX invBind = XMLoadFloat4x4(&m_bones[i].inverseBindPose);
        XMMATRIX worldPose = XMLoadFloat4x4(&m_worldPoses[i]);
        XMMATRIX finalMat = XMMatrixMultiply(invBind, worldPose);
        XMStoreFloat4x4(&m_finalMatrices[i], XMMatrixTranspose(finalMat));
    }
}

// ============================================================
// Interpolation helpers
// ============================================================
XMFLOAT3 FBXModel::InterpolateVec3(const std::vector<BakedAnimClip::BoneChannel::Vec3Key>& keys, float time) const {
    if (keys.empty()) return { 0, 0, 0 };
    if (keys.size() == 1 || time <= keys[0].time) return keys[0].value;
    if (time >= keys.back().time) return keys.back().value;

    // Find surrounding keys
    int idx = 0;
    for (int i = 0; i < static_cast<int>(keys.size()) - 1; i++) {
        if (time < keys[i + 1].time) { idx = i; break; }
    }

    float t = (time - keys[idx].time) / (keys[idx + 1].time - keys[idx].time);
    XMVECTOR v0 = XMLoadFloat3(&keys[idx].value);
    XMVECTOR v1 = XMLoadFloat3(&keys[idx + 1].value);
    XMFLOAT3 result;
    XMStoreFloat3(&result, XMVectorLerp(v0, v1, t));
    return result;
}

XMFLOAT4 FBXModel::InterpolateQuat(const std::vector<BakedAnimClip::BoneChannel::QuatKey>& keys, float time) const {
    if (keys.empty()) return { 0, 0, 0, 1 };
    if (keys.size() == 1 || time <= keys[0].time) return keys[0].value;
    if (time >= keys.back().time) return keys.back().value;

    int idx = 0;
    for (int i = 0; i < static_cast<int>(keys.size()) - 1; i++) {
        if (time < keys[i + 1].time) { idx = i; break; }
    }

    float t = (time - keys[idx].time) / (keys[idx + 1].time - keys[idx].time);
    XMVECTOR q0 = XMLoadFloat4(&keys[idx].value);
    XMVECTOR q1 = XMLoadFloat4(&keys[idx + 1].value);
    XMFLOAT4 result;
    XMStoreFloat4(&result, XMQuaternionSlerp(q0, q1, t));
    return result;
}

// ============================================================
// Drawing
// ============================================================
void FBXModel::Draw(ID3D11DeviceContext* context) const {
    if (!m_vertexBuffer || !m_indexBuffer) return;

    UINT stride = sizeof(VertexSkinned);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->DrawIndexed(m_indexCount, 0, 0);
}

// ============================================================
// Queries
// ============================================================
int FBXModel::FindBone(const std::string& name) const {
    auto it = m_boneNameToIndex.find(name);
    return it != m_boneNameToIndex.end() ? it->second : -1;
}

bool FBXModel::HasAnimation(const std::string& name) const {
    return m_animations.find(name) != m_animations.end();
}

void FBXModel::SetMaterialColor(int materialIndex, const XMFLOAT4& color) {
    m_materialColors[materialIndex] = color;
}

// ============================================================
// Cleanup
// ============================================================
void FBXModel::Release() {
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
    m_bindLocalTransforms.clear();
    m_materialColors.clear();
    m_currentAnim.clear();
    m_animTime = 0.0f;

    if (m_modelScene) {
        ufbx_free_scene(m_modelScene);
        m_modelScene = nullptr;
    }
}

} // namespace WT
