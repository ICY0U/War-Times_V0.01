#pragma once
// ============================================================
// SceneCulling — Frustum culling + distance-based level streaming
// ============================================================

#include <DirectXMath.h>
#include <vector>
#include <cmath>
#include <algorithm>

namespace WT {

using namespace DirectX;

// ============================================================
// Frustum — 6 planes extracted from a ViewProjection matrix
// ============================================================
struct Frustum {
    XMFLOAT4 planes[6]; // Left, Right, Bottom, Top, Near, Far  (a,b,c,d => ax+by+cz+d=0)

    // Extract frustum planes from a row-major ViewProjection matrix.
    // Planes are normalized so distance tests give world-space units.
    void ExtractFromViewProj(XMMATRIX vp) {
        // We need the matrix in row-major form for Gribb/Hartmann extraction.
        // DirectXMath stores row-major by default in XMMATRIX.
        XMFLOAT4X4 m;
        XMStoreFloat4x4(&m, vp);

        // Left:   row3 + row0
        planes[0] = { m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41 };
        // Right:  row3 - row0
        planes[1] = { m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41 };
        // Bottom: row3 + row1
        planes[2] = { m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42 };
        // Top:    row3 - row1
        planes[3] = { m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42 };
        // Near:   row2
        planes[4] = { m._13, m._23, m._33, m._43 };
        // Far:    row3 - row2
        planes[5] = { m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43 };

        // Normalize each plane
        for (int i = 0; i < 6; i++) {
            float len = sqrtf(planes[i].x * planes[i].x +
                              planes[i].y * planes[i].y +
                              planes[i].z * planes[i].z);
            if (len > 0.0001f) {
                float inv = 1.0f / len;
                planes[i].x *= inv;
                planes[i].y *= inv;
                planes[i].z *= inv;
                planes[i].w *= inv;
            }
        }
    }

    // Test an AABB against the frustum. Returns true if the AABB is
    // at least partially inside (conservative — no false negatives).
    bool TestAABB(const XMFLOAT3& center, const XMFLOAT3& halfExtents) const {
        for (int i = 0; i < 6; i++) {
            // Compute the positive extent vertex (the corner most aligned with the plane normal)
            float d = planes[i].x * center.x + planes[i].y * center.y + planes[i].z * center.z + planes[i].w;
            float r = fabsf(planes[i].x) * halfExtents.x +
                      fabsf(planes[i].y) * halfExtents.y +
                      fabsf(planes[i].z) * halfExtents.z;
            if (d + r < 0.0f) return false; // Entirely outside this plane
        }
        return true;
    }

    // Test a sphere against the frustum
    bool TestSphere(const XMFLOAT3& center, float radius) const {
        for (int i = 0; i < 6; i++) {
            float d = planes[i].x * center.x + planes[i].y * center.y + planes[i].z * center.z + planes[i].w;
            if (d + radius < 0.0f) return false;
        }
        return true;
    }
};

// ============================================================
// AABB helper — compute from Entity position + scale (axis-aligned cube)
// For rotated entities, uses a conservative bounding sphere radius.
// ============================================================
struct EntityBounds {
    XMFLOAT3 center;
    XMFLOAT3 halfExtents;
    float    boundingSphereRadius;

    void ComputeFromEntity(const float pos[3], const float scl[3], const float rot[3]) {
        center = { pos[0], pos[1], pos[2] };

        bool hasRotation = (fabsf(rot[0]) > 0.01f || fabsf(rot[1]) > 0.01f || fabsf(rot[2]) > 0.01f);

        if (hasRotation) {
            // For rotated entities, use a bounding sphere (conservative)
            // Bounding sphere radius = half diagonal of the box
            float halfDiag = sqrtf(scl[0] * scl[0] + scl[1] * scl[1] + scl[2] * scl[2]) * 0.5f;
            halfExtents = { halfDiag, halfDiag, halfDiag };
            boundingSphereRadius = halfDiag;
        } else {
            halfExtents = { fabsf(scl[0]) * 0.5f, fabsf(scl[1]) * 0.5f, fabsf(scl[2]) * 0.5f };
            boundingSphereRadius = sqrtf(halfExtents.x * halfExtents.x +
                                         halfExtents.y * halfExtents.y +
                                         halfExtents.z * halfExtents.z);
        }
    }
};

// ============================================================
// SceneCuller — performs frustum culling on a list of entities
// Produces a visibility bitset each frame.
// ============================================================
class SceneCuller {
public:
    // Rebuild bounds cache. Call when entities are added/removed/transformed.
    template <typename GetEntityFn>
    void RebuildBounds(int entityCount, GetEntityFn getEntity) {
        m_bounds.resize(entityCount);
        for (int i = 0; i < entityCount; i++) {
            const auto& e = getEntity(i);
            m_bounds[i].ComputeFromEntity(e.position, e.scale, e.rotation);
        }
        m_boundsEntityCount = entityCount;
    }

    // Rebuild visibility list. Call once per frame before rendering.
    // `entityCount`:    number of entities in scene
    // `getEntity`:      lambda/callback to access entity data
    // `viewProj`:       camera view * projection matrix (NOT transposed)
    // `cameraPos`:      camera world position
    // `streamDist`:     distance beyond which entities are not rendered (level streaming)
    //                   Set to 0 or negative to disable distance culling.
    template <typename GetEntityFn>
    void Update(int entityCount, GetEntityFn getEntity,
                XMMATRIX viewProj, const XMFLOAT3& cameraPos,
                float streamDist) {
        // Rebuild bounds cache if entity count changed
        if (entityCount != m_boundsEntityCount) {
            RebuildBounds(entityCount, getEntity);
        }

        // Extract frustum
        m_frustum.ExtractFromViewProj(viewProj);
        m_streamDistance = streamDist;
        m_cameraPos = cameraPos;

        // Resize visibility array
        m_visible.resize(entityCount, false);
        m_stats = {};

        float streamDist2 = streamDist * streamDist;
        bool useDistanceCull = streamDist > 0.0f;

        for (int i = 0; i < entityCount; i++) {
            const auto& e = getEntity(i);

            if (!e.visible) {
                m_visible[i] = false;
                continue;
            }

            m_stats.totalEntities++;

            const EntityBounds& bounds = m_bounds[i];

            // Distance culling (level streaming)
            if (useDistanceCull) {
                float dx = bounds.center.x - cameraPos.x;
                float dy = bounds.center.y - cameraPos.y;
                float dz = bounds.center.z - cameraPos.z;
                float dist2 = dx * dx + dy * dy + dz * dz;
                float effectiveDist2 = streamDist2 + bounds.boundingSphereRadius * bounds.boundingSphereRadius;

                if (dist2 > effectiveDist2) {
                    m_visible[i] = false;
                    m_stats.distanceCulled++;
                    continue;
                }
            }

            // Frustum culling
            if (m_frustum.TestAABB(bounds.center, bounds.halfExtents)) {
                m_visible[i] = true;
                m_stats.rendered++;
            } else {
                m_visible[i] = false;
                m_stats.frustumCulled++;
            }
        }
    }

    // Check if entity at index should be rendered
    bool IsVisible(int index) const {
        if (index < 0 || index >= static_cast<int>(m_visible.size())) return false;
        return m_visible[index];
    }

    // Shadow pass frustum test for a specific entity (uses light frustum)
    // Call after UpdateShadowFrustum
    bool IsVisibleShadow(int index) const {
        if (index < 0 || index >= static_cast<int>(m_shadowVisible.size())) return false;
        return m_shadowVisible[index];
    }

    // Update shadow visibility using the light's view-projection
    // Reuses cached bounds from Update() — no recomputation.
    template <typename GetEntityFn>
    void UpdateShadowFrustum(int entityCount, GetEntityFn getEntity,
                             XMMATRIX lightVP, const XMFLOAT3& cameraPos,
                             float shadowDist) {
        m_shadowFrustum.ExtractFromViewProj(lightVP);
        m_shadowVisible.resize(entityCount, false);

        float shadowDist2 = shadowDist * shadowDist;

        for (int i = 0; i < entityCount; i++) {
            const auto& e = getEntity(i);
            if (!e.visible || !e.castShadow) {
                m_shadowVisible[i] = false;
                continue;
            }

            const EntityBounds& bounds = (i < m_boundsEntityCount) ? m_bounds[i]
                                         : m_fallbackBounds;

            // Distance cull for shadows (typically shorter range)
            float dx = bounds.center.x - cameraPos.x;
            float dz = bounds.center.z - cameraPos.z;
            float dist2 = dx * dx + dz * dz;
            if (dist2 > shadowDist2 + bounds.boundingSphereRadius * bounds.boundingSphereRadius) {
                m_shadowVisible[i] = false;
                continue;
            }

            m_shadowVisible[i] = m_shadowFrustum.TestAABB(bounds.center, bounds.halfExtents);
        }
    }

    // Force bounds rebuild (call after scene changes like PCG, load, entity edit)
    void InvalidateBounds() { m_boundsEntityCount = -1; }

    // Statistics
    struct CullStats {
        int totalEntities  = 0;
        int frustumCulled  = 0;
        int distanceCulled = 0;
        int rendered       = 0;
    };

    const CullStats& GetStats() const { return m_stats; }
    const Frustum& GetFrustum() const { return m_frustum; }

private:
    Frustum m_frustum;
    Frustum m_shadowFrustum;
    std::vector<bool> m_visible;
    std::vector<bool> m_shadowVisible;
    std::vector<EntityBounds> m_bounds;
    int m_boundsEntityCount = -1;
    EntityBounds m_fallbackBounds = {};
    float m_streamDistance = 0.0f;
    XMFLOAT3 m_cameraPos = {};
    CullStats m_stats;
};

} // namespace WT
