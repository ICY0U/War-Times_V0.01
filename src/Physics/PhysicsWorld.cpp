#include "PhysicsWorld.h"
#include "../Core/Entity.h"
#include "../Core/ResourceManager.h"
#include "../Graphics/DebugRenderer.h"
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <DirectXMath.h>

namespace WT {

// ============================================================
// Init / Shutdown
// ============================================================

void PhysicsWorld::Init() {
    m_bodies.clear();
    m_settings = PhysicsSettings{};
    showDebug = false;
}

void PhysicsWorld::Shutdown() {
    m_bodies.clear();
}

// ============================================================
// Body management
// ============================================================

int PhysicsWorld::AddBody(const PhysicsBody& body) {
    m_bodies.push_back(body);
    return static_cast<int>(m_bodies.size()) - 1;
}

void PhysicsWorld::RemoveBody(int index) {
    if (index >= 0 && index < static_cast<int>(m_bodies.size())) {
        m_bodies.erase(m_bodies.begin() + index);
        // Fix entity-index references
        for (auto& b : m_bodies) {
            if (b.entityIndex > index) b.entityIndex--;
        }
    }
}

void PhysicsWorld::ClearBodies() {
    m_bodies.clear();
}

// ============================================================
// Rebuild static colliders from scene entities
// ============================================================

void PhysicsWorld::RebuildStaticColliders(const Scene& scene) {
    // Remove old static bodies
    m_bodies.erase(
        std::remove_if(m_bodies.begin(), m_bodies.end(),
            [](const PhysicsBody& b) { return b.type == PhysicsBodyType::Static; }),
        m_bodies.end()
    );

    // Add static AABB for each visible entity
    const auto& entities = scene.GetEntities();
    for (int i = 0; i < static_cast<int>(entities.size()); i++) {
        const auto& e = entities[i];
        if (!e.visible || e.noCollision) continue;

        // Voxel entities: create a separate collider for each active cell
        if (e.voxelDestruction && e.meshType == MeshType::Cube) {
            int res = e.voxelRes;
            float cellSX = e.scale[0] / res;
            float cellSY = e.scale[1] / res;
            float cellSZ = e.scale[2] / res;

            bool hasRot = (e.rotation[0] != 0.0f || e.rotation[1] != 0.0f || e.rotation[2] != 0.0f);
            XMMATRIX R = XMMatrixIdentity();
            if (hasRot) {
                R = XMMatrixRotationRollPitchYaw(
                    XMConvertToRadians(e.rotation[0]),
                    XMConvertToRadians(e.rotation[1]),
                    XMConvertToRadians(e.rotation[2]));
            }

            for (int vz = 0; vz < res; vz++)
            for (int vy = 0; vy < res; vy++)
            for (int vx = 0; vx < res; vx++) {
                int idx = vx + vy * res + vz * res * res;
                if (!e.IsVoxelCellActive(idx)) continue;

                // Cell center in local space
                float offX = ((vx + 0.5f) / res - 0.5f) * e.scale[0];
                float offY = ((vy + 0.5f) / res - 0.5f) * e.scale[1];
                float offZ = ((vz + 0.5f) / res - 0.5f) * e.scale[2];

                PhysicsBody body;
                body.type = PhysicsBodyType::Static;
                body.entityIndex = i;
                body.voxelCellIndex = idx;
                body.enabled = true;

                if (!hasRot) {
                    body.box = AABB::FromCenterHalf(
                        { e.position[0] + offX, e.position[1] + offY, e.position[2] + offZ },
                        { cellSX * 0.5f, cellSY * 0.5f, cellSZ * 0.5f }
                    );
                } else {
                    // Rotate offset
                    XMVECTOR off = XMVectorSet(offX, offY, offZ, 0);
                    off = XMVector3TransformNormal(off, R);
                    XMFLOAT3 rOff;
                    XMStoreFloat3(&rOff, off);

                    body.hasRotation = true;
                    body.obbCenter = { e.position[0] + rOff.x, e.position[1] + rOff.y, e.position[2] + rOff.z };
                    body.obbHalfExt = { cellSX * 0.5f, cellSY * 0.5f, cellSZ * 0.5f };
                    XMStoreFloat3x3(&body.obbRotMat, R);

                    // Broad-phase AABB for the rotated cell
                    XMMATRIX cellSR = XMMatrixScaling(cellSX, cellSY, cellSZ) * R;
                    static const XMFLOAT3 corners[8] = {
                        {-0.5f,-0.5f,-0.5f}, {0.5f,-0.5f,-0.5f},
                        {-0.5f, 0.5f,-0.5f}, {0.5f, 0.5f,-0.5f},
                        {-0.5f,-0.5f, 0.5f}, {0.5f,-0.5f, 0.5f},
                        {-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f},
                    };
                    XMFLOAT3 mn = { FLT_MAX, FLT_MAX, FLT_MAX };
                    XMFLOAT3 mx = {-FLT_MAX,-FLT_MAX,-FLT_MAX };
                    for (int c = 0; c < 8; c++) {
                        XMVECTOR v = XMVector3Transform(XMLoadFloat3(&corners[c]), cellSR);
                        XMFLOAT3 pt; XMStoreFloat3(&pt, v);
                        mn.x = (std::min)(mn.x, pt.x); mn.y = (std::min)(mn.y, pt.y); mn.z = (std::min)(mn.z, pt.z);
                        mx.x = (std::max)(mx.x, pt.x); mx.y = (std::max)(mx.y, pt.y); mx.z = (std::max)(mx.z, pt.z);
                    }
                    body.box = AABB::FromCenterHalf(
                        { e.position[0] + rOff.x, e.position[1] + rOff.y, e.position[2] + rOff.z },
                        { (mx.x - mn.x) * 0.5f, (mx.y - mn.y) * 0.5f, (mx.z - mn.z) * 0.5f }
                    );
                }
                m_bodies.push_back(body);
            }
            continue;
        }

        PhysicsBody body;
        body.type = PhysicsBodyType::Static;
        body.entityIndex = i;
        body.enabled = true;

        // For custom meshes, use mesh bounds scaled by entity scale
        XMFLOAT3 halfExt = { e.scale[0] * 0.5f, e.scale[1] * 0.5f, e.scale[2] * 0.5f };
        XMFLOAT3 boundsCenter = { 0, 0, 0 };
        if (e.meshType == MeshType::Custom && !e.meshName.empty()) {
            Mesh* mesh = ResourceManager::Get().GetMesh(e.meshName);
            if (mesh && mesh->HasBounds()) {
                auto bc = mesh->GetBoundsCenter();
                auto bh = mesh->GetBoundsHalfExtent();
                boundsCenter = { bc.x * e.scale[0], bc.y * e.scale[1], bc.z * e.scale[2] };
                halfExt = { bh.x * e.scale[0], bh.y * e.scale[1], bh.z * e.scale[2] };
            }
        }

        // Compute AABB from rotated box: transform 8 corners of unit cube
        // through Scale * Rotation, then find axis-aligned bounds
        bool hasRotation = (e.rotation[0] != 0.0f || e.rotation[1] != 0.0f || e.rotation[2] != 0.0f);

        if (!hasRotation) {
            // Fast path: no rotation, AABB = center ± half-extent
            body.box = AABB::FromCenterHalf(
                { e.position[0] + boundsCenter.x, e.position[1] + boundsCenter.y, e.position[2] + boundsCenter.z },
                halfExt
            );
        } else {
            // Entity has rotation — store OBB for narrow-phase, inflated AABB for broad-phase
            using namespace DirectX;

            body.hasRotation = true;
            body.obbCenter = { e.position[0] + boundsCenter.x, e.position[1] + boundsCenter.y, e.position[2] + boundsCenter.z };
            body.obbHalfExt = halfExt;

            // Build rotation matrix
            XMMATRIX R = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(e.rotation[0]),
                XMConvertToRadians(e.rotation[1]),
                XMConvertToRadians(e.rotation[2]));
            XMStoreFloat3x3(&body.obbRotMat, R);

            // Compute broad-phase AABB from rotated bounds
            // Use actual half extents for corner computation
            XMFLOAT3 hx = halfExt;

            XMFLOAT3 localCorners[8] = {
                {-hx.x, -hx.y, -hx.z}, { hx.x, -hx.y, -hx.z},
                {-hx.x,  hx.y, -hx.z}, { hx.x,  hx.y, -hx.z},
                {-hx.x, -hx.y,  hx.z}, { hx.x, -hx.y,  hx.z},
                {-hx.x,  hx.y,  hx.z}, { hx.x,  hx.y,  hx.z},
            };

            XMFLOAT3 mn = { FLT_MAX,  FLT_MAX,  FLT_MAX };
            XMFLOAT3 mx = {-FLT_MAX, -FLT_MAX, -FLT_MAX };

            for (int c = 0; c < 8; c++) {
                XMVECTOR v = XMVector3TransformNormal(XMLoadFloat3(&localCorners[c]), R);
                XMFLOAT3 pt;
                XMStoreFloat3(&pt, v);
                mn.x = (std::min)(mn.x, pt.x); mn.y = (std::min)(mn.y, pt.y); mn.z = (std::min)(mn.z, pt.z);
                mx.x = (std::max)(mx.x, pt.x); mx.y = (std::max)(mx.y, pt.y); mx.z = (std::max)(mx.z, pt.z);
            }

            body.box = AABB::FromCenterHalf(
                { e.position[0] + boundsCenter.x, e.position[1] + boundsCenter.y,
                  e.position[2] + boundsCenter.z },
                { (mx.x - mn.x) * 0.5f, (mx.y - mn.y) * 0.5f, (mx.z - mn.z) * 0.5f }
            );
        }

        m_bodies.push_back(body);
    }
}

// ============================================================
// Simulation step
// ============================================================

void PhysicsWorld::Step(float dt, const PhysicsSettings& settings) {
    m_settings = settings;
    if (dt <= 0.0f) return;

    for (auto& body : m_bodies) {
        if (!body.enabled) continue;
        if (body.type == PhysicsBodyType::Static) continue;

        // --- Apply gravity ---
        if (body.type == PhysicsBodyType::Dynamic) {
            body.velocity.y -= settings.gravity * dt;
        }

        // --- Clamp velocity ---
        float speed = sqrtf(body.velocity.x * body.velocity.x +
                            body.velocity.y * body.velocity.y +
                            body.velocity.z * body.velocity.z);
        if (speed > settings.maxVelocity) {
            float scale = settings.maxVelocity / speed;
            body.velocity.x *= scale;
            body.velocity.y *= scale;
            body.velocity.z *= scale;
        }

        // --- Integrate position ---
        XMFLOAT3 displacement = {
            body.velocity.x * dt,
            body.velocity.y * dt,
            body.velocity.z * dt
        };

        // Move the AABB
        body.box.min.x += displacement.x;
        body.box.max.x += displacement.x;
        body.box.min.y += displacement.y;
        body.box.max.y += displacement.y;
        body.box.min.z += displacement.z;
        body.box.max.z += displacement.z;

        body.onGround = false;

        // --- Iterative collision resolution ---
        for (int iter = 0; iter < settings.maxIterations; iter++) {
            bool resolved = false;

            // Ground plane
            if (settings.groundEnabled) {
                CollisionHit ground = GroundTest(body.box);
                if (ground.hit) {
                    body.box.min.y += ground.depth + settings.skinWidth;
                    body.box.max.y += ground.depth + settings.skinWidth;
                    if (body.velocity.y < 0.0f) {
                        body.velocity.y = body.bounciness > 0.0f
                            ? -body.velocity.y * body.bounciness
                            : 0.0f;
                    }
                    body.onGround = true;
                    resolved = true;
                }
            }

            // Static colliders
            for (int i = 0; i < static_cast<int>(m_bodies.size()); i++) {
                const auto& other = m_bodies[i];
                if (&other == &body) continue;
                if (!other.enabled) continue;
                if (other.type != PhysicsBodyType::Static) continue;

                // Broad-phase: AABB overlap check
                if (!AABBOverlap(body.box, other.box)) continue;

                // Narrow-phase: OBB or AABB
                CollisionHit hit;
                if (other.hasRotation) {
                    hit = OBBvsAABB(other, body.box);
                } else {
                    hit = AABBResolve(body.box, other.box);
                }

                if (hit.hit) {
                    // Push out
                    body.box.min.x += hit.normal.x * (hit.depth + settings.skinWidth);
                    body.box.max.x += hit.normal.x * (hit.depth + settings.skinWidth);
                    body.box.min.y += hit.normal.y * (hit.depth + settings.skinWidth);
                    body.box.max.y += hit.normal.y * (hit.depth + settings.skinWidth);
                    body.box.min.z += hit.normal.z * (hit.depth + settings.skinWidth);
                    body.box.max.z += hit.normal.z * (hit.depth + settings.skinWidth);

                    // Cancel velocity along collision normal
                    float vDotN = body.velocity.x * hit.normal.x +
                                  body.velocity.y * hit.normal.y +
                                  body.velocity.z * hit.normal.z;
                    if (vDotN < 0.0f) {
                        body.velocity.x -= hit.normal.x * vDotN;
                        body.velocity.y -= hit.normal.y * vDotN;
                        body.velocity.z -= hit.normal.z * vDotN;
                    }

                    // Landing on top of an entity
                    if (hit.normal.y > 0.5f) {
                        body.onGround = true;
                    }

                    resolved = true;
                }
            }

            if (!resolved) break;
        }

        // --- Ground friction ---
        if (body.onGround && body.friction > 0.0f) {
            float factor = 1.0f - body.friction * dt * 10.0f;
            if (factor < 0.0f) factor = 0.0f;
            body.velocity.x *= factor;
            body.velocity.z *= factor;
        }
    }
}

// ============================================================
// Collision queries
// ============================================================

CollisionHit PhysicsWorld::TestAABB(const AABB& box, int ignoreBodyIndex) const {
    // Ground
    if (m_settings.groundEnabled) {
        CollisionHit ground = GroundTest(box);
        if (ground.hit) return ground;
    }

    // Bodies
    for (int i = 0; i < static_cast<int>(m_bodies.size()); i++) {
        if (i == ignoreBodyIndex) continue;
        const auto& b = m_bodies[i];
        if (!b.enabled) continue;

        // Broad-phase AABB check
        if (!AABBOverlap(box, b.box)) continue;

        // Narrow-phase: OBB or AABB
        CollisionHit hit;
        if (b.hasRotation) {
            hit = OBBvsAABB(b, box);
        } else {
            if (AABBOverlap(box, b.box)) {
                hit = AABBResolve(box, b.box);
            }
        }

        if (hit.hit) {
            hit.entityIndex = b.entityIndex;
            return hit;
        }
    }

    return {};
}

CollisionHit PhysicsWorld::SweepAABB(const AABB& box, const XMFLOAT3& displacement,
                                      int ignoreBodyIndex) const {
    // Simple step-sweep: subdivide displacement into small steps
    float dist = sqrtf(displacement.x * displacement.x +
                       displacement.y * displacement.y +
                       displacement.z * displacement.z);
    if (dist < 0.0001f) return {};

    const int steps = static_cast<int>(dist / 0.05f) + 1;
    float invSteps = 1.0f / static_cast<float>(steps);

    AABB swept = box;
    for (int s = 1; s <= steps; s++) {
        float t = static_cast<float>(s) * invSteps;
        swept.min = {
            box.min.x + displacement.x * t,
            box.min.y + displacement.y * t,
            box.min.z + displacement.z * t
        };
        swept.max = {
            box.max.x + displacement.x * t,
            box.max.y + displacement.y * t,
            box.max.z + displacement.z * t
        };

        CollisionHit hit = TestAABB(swept, ignoreBodyIndex);
        if (hit.hit) return hit;
    }

    return {};
}

bool PhysicsWorld::PointInside(const XMFLOAT3& point) const {
    for (const auto& body : m_bodies) {
        if (!body.enabled) continue;
        if (point.x >= body.box.min.x && point.x <= body.box.max.x &&
            point.y >= body.box.min.y && point.y <= body.box.max.y &&
            point.z >= body.box.min.z && point.z <= body.box.max.z) {
            return true;
        }
    }
    return false;
}

// ============================================================
// Raycast
// ============================================================

CollisionHit PhysicsWorld::Raycast(const XMFLOAT3& origin, const XMFLOAT3& direction,
                                    float maxDist) const {
    CollisionHit closest;
    float closestT = maxDist;

    // Compute inverse direction (handle zeros)
    XMFLOAT3 invDir = {
        (fabsf(direction.x) > 1e-8f) ? 1.0f / direction.x : 1e8f,
        (fabsf(direction.y) > 1e-8f) ? 1.0f / direction.y : 1e8f,
        (fabsf(direction.z) > 1e-8f) ? 1.0f / direction.z : 1e8f
    };

    // Ground plane
    if (m_settings.groundEnabled && fabsf(direction.y) > 1e-8f) {
        float t = (m_settings.groundY - origin.y) / direction.y;
        if (t > 0.0f && t < closestT) {
            closestT = t;
            closest.hit = true;
            closest.normal = { 0.0f, 1.0f, 0.0f };
            closest.depth = t;
            closest.entityIndex = -1;
        }
    }

    // Bodies
    for (int i = 0; i < static_cast<int>(m_bodies.size()); i++) {
        const auto& body = m_bodies[i];
        if (!body.enabled) continue;

        if (body.hasRotation) {
            // OBB raycast
            float tHit;
            XMFLOAT3 hitNorm;
            if (RayOBB(origin, direction, body, tHit, hitNorm)) {
                if (tHit > 0.0f && tHit < closestT) {
                    closestT = tHit;
                    closest.hit = true;
                    closest.depth = tHit;
                    closest.entityIndex = body.entityIndex;
                    closest.voxelCellIndex = body.voxelCellIndex;
                    closest.normal = hitNorm;
                }
            }
        } else {
            // Standard AABB raycast
            float tMin, tMax;
            if (RayAABB(origin, invDir, body.box, tMin, tMax)) {
                if (tMin > 0.0f && tMin < closestT) {
                    closestT = tMin;
                    closest.hit = true;
                    closest.depth = tMin;
                    closest.entityIndex = body.entityIndex;
                    closest.voxelCellIndex = body.voxelCellIndex;

                    // Determine hit face normal
                    XMFLOAT3 hitPt = {
                        origin.x + direction.x * tMin,
                        origin.y + direction.y * tMin,
                        origin.z + direction.z * tMin
                    };
                    XMFLOAT3 center = body.box.Center();
                    XMFLOAT3 half = body.box.HalfExtents();

                    // Find which face was hit
                    float dx = (hitPt.x - center.x) / half.x;
                    float dy = (hitPt.y - center.y) / half.y;
                    float dz = (hitPt.z - center.z) / half.z;

                    float ax = fabsf(dx), ay = fabsf(dy), az = fabsf(dz);
                    if (ax > ay && ax > az)
                        closest.normal = { dx > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f };
                    else if (ay > az)
                        closest.normal = { 0.0f, dy > 0.0f ? 1.0f : -1.0f, 0.0f };
                    else
                        closest.normal = { 0.0f, 0.0f, dz > 0.0f ? 1.0f : -1.0f };
                }
            }
        }
    }

    return closest;
}

// ============================================================
// Debug drawing
// ============================================================

void PhysicsWorld::DebugDraw(DebugRenderer& debug) const {
    if (!showDebug) return;

    for (const auto& body : m_bodies) {
        if (!body.enabled) continue;

        XMFLOAT4 color;
        switch (body.type) {
            case PhysicsBodyType::Static:    color = { 0.0f, 1.0f, 0.0f, 1.0f }; break;
            case PhysicsBodyType::Dynamic:   color = { 1.0f, 0.5f, 0.0f, 1.0f }; break;
            case PhysicsBodyType::Kinematic: color = { 0.0f, 0.5f, 1.0f, 1.0f }; break;
        }

        if (body.hasRotation) {
            // Draw the actual rotated box
            debug.DrawRotatedBox(body.obbCenter, body.obbHalfExt, body.obbRotMat, color);
        } else {
            XMFLOAT3 center = body.box.Center();
            XMFLOAT3 halfExt = body.box.HalfExtents();
            debug.DrawBox(center, halfExt, color);
        }
    }
}

// ============================================================
// Internal helpers
// ============================================================

bool PhysicsWorld::AABBOverlap(const AABB& a, const AABB& b) {
    return (a.min.x < b.max.x && a.max.x > b.min.x) &&
           (a.min.y < b.max.y && a.max.y > b.min.y) &&
           (a.min.z < b.max.z && a.max.z > b.min.z);
}

CollisionHit PhysicsWorld::AABBResolve(const AABB& moving, const AABB& stationary) {
    CollisionHit hit;

    // Compute overlap on each axis
    float overlapX1 = stationary.max.x - moving.min.x;  // push +X
    float overlapX2 = moving.max.x - stationary.min.x;  // push -X
    float overlapY1 = stationary.max.y - moving.min.y;  // push +Y
    float overlapY2 = moving.max.y - stationary.min.y;  // push -Y
    float overlapZ1 = stationary.max.z - moving.min.z;  // push +Z
    float overlapZ2 = moving.max.z - stationary.min.z;  // push -Z

    // If any overlap is negative, no collision
    if (overlapX1 <= 0.0f || overlapX2 <= 0.0f ||
        overlapY1 <= 0.0f || overlapY2 <= 0.0f ||
        overlapZ1 <= 0.0f || overlapZ2 <= 0.0f) {
        return hit;
    }

    // Find minimum penetration axis
    float minOverlap = FLT_MAX;
    XMFLOAT3 normal = { 0.0f, 0.0f, 0.0f };

    if (overlapX1 < minOverlap) { minOverlap = overlapX1; normal = {  1.0f, 0.0f, 0.0f }; }
    if (overlapX2 < minOverlap) { minOverlap = overlapX2; normal = { -1.0f, 0.0f, 0.0f }; }
    if (overlapY1 < minOverlap) { minOverlap = overlapY1; normal = { 0.0f,  1.0f, 0.0f }; }
    if (overlapY2 < minOverlap) { minOverlap = overlapY2; normal = { 0.0f, -1.0f, 0.0f }; }
    if (overlapZ1 < minOverlap) { minOverlap = overlapZ1; normal = { 0.0f, 0.0f,  1.0f }; }
    if (overlapZ2 < minOverlap) { minOverlap = overlapZ2; normal = { 0.0f, 0.0f, -1.0f }; }

    hit.hit = true;
    hit.normal = normal;
    hit.depth = minOverlap;
    return hit;
}

CollisionHit PhysicsWorld::GroundTest(const AABB& box) const {
    CollisionHit hit;
    if (box.min.y < m_settings.groundY) {
        hit.hit = true;
        hit.normal = { 0.0f, 1.0f, 0.0f };
        hit.depth = m_settings.groundY - box.min.y;
    }
    return hit;
}

bool PhysicsWorld::RayAABB(const XMFLOAT3& origin, const XMFLOAT3& invDir,
                            const AABB& box, float& tMin, float& tMax) {
    float t1 = (box.min.x - origin.x) * invDir.x;
    float t2 = (box.max.x - origin.x) * invDir.x;
    float t3 = (box.min.y - origin.y) * invDir.y;
    float t4 = (box.max.y - origin.y) * invDir.y;
    float t5 = (box.min.z - origin.z) * invDir.z;
    float t6 = (box.max.z - origin.z) * invDir.z;

    tMin = (std::max)((std::max)((std::min)(t1, t2), (std::min)(t3, t4)), (std::min)(t5, t6));
    tMax = (std::min)((std::min)((std::max)(t1, t2), (std::max)(t3, t4)), (std::max)(t5, t6));

    return tMax >= 0.0f && tMin <= tMax;
}

// ============================================================
// OBB vs AABB — SAT-based collision with MTV (Minimum Translation Vector)
// ============================================================

CollisionHit PhysicsWorld::OBBvsAABB(const PhysicsBody& obbBody, const AABB& aabb) {
    CollisionHit result;

    // OBB data
    XMFLOAT3 oC  = obbBody.obbCenter;
    XMFLOAT3 oH  = obbBody.obbHalfExt;
    const XMFLOAT3X3& oR = obbBody.obbRotMat;

    // OBB local axes (columns of rotation matrix)
    XMFLOAT3 oAxis[3] = {
        { oR._11, oR._12, oR._13 },  // local X in world space
        { oR._21, oR._22, oR._23 },  // local Y in world space
        { oR._31, oR._32, oR._33 },  // local Z in world space
    };

    // AABB data
    XMFLOAT3 aC = aabb.Center();
    XMFLOAT3 aH = aabb.HalfExtents();

    // Vector from AABB center to OBB center
    XMFLOAT3 d = { oC.x - aC.x, oC.y - aC.y, oC.z - aC.z };

    float minOverlap = FLT_MAX;
    XMFLOAT3 mtvAxis = { 0.0f, 0.0f, 0.0f };

    // Helper lambda: test a separating axis
    // projA = half-projection of AABB onto axis
    // projB = half-projection of OBB onto axis
    // dist  = projection of center-to-center vector onto axis
    auto testAxis = [&](float axisX, float axisY, float axisZ) -> bool {
        float len = sqrtf(axisX * axisX + axisY * axisY + axisZ * axisZ);
        if (len < 1e-6f) return true; // Degenerate axis, skip

        // Normalize
        float invLen = 1.0f / len;
        axisX *= invLen; axisY *= invLen; axisZ *= invLen;

        // Project AABB half-extents onto axis (AABB axes are world X,Y,Z)
        float projA = aH.x * fabsf(axisX) + aH.y * fabsf(axisY) + aH.z * fabsf(axisZ);

        // Project OBB half-extents onto axis
        float projB = oH.x * fabsf(oAxis[0].x * axisX + oAxis[0].y * axisY + oAxis[0].z * axisZ)
                    + oH.y * fabsf(oAxis[1].x * axisX + oAxis[1].y * axisY + oAxis[1].z * axisZ)
                    + oH.z * fabsf(oAxis[2].x * axisX + oAxis[2].y * axisY + oAxis[2].z * axisZ);

        // Distance between centers projected onto axis
        float dist = fabsf(d.x * axisX + d.y * axisY + d.z * axisZ);

        float overlap = (projA + projB) - dist;
        if (overlap <= 0.0f) return false;  // Separating axis found — no collision

        if (overlap < minOverlap) {
            minOverlap = overlap;
            // MTV direction: push AABB away from OBB
            float sign = (d.x * axisX + d.y * axisY + d.z * axisZ) > 0.0f ? -1.0f : 1.0f;
            mtvAxis = { axisX * sign, axisY * sign, axisZ * sign };
        }
        return true;
    };

    // Test 3 AABB face axes (world X, Y, Z)
    if (!testAxis(1, 0, 0)) return result;
    if (!testAxis(0, 1, 0)) return result;
    if (!testAxis(0, 0, 1)) return result;

    // Test 3 OBB face axes
    if (!testAxis(oAxis[0].x, oAxis[0].y, oAxis[0].z)) return result;
    if (!testAxis(oAxis[1].x, oAxis[1].y, oAxis[1].z)) return result;
    if (!testAxis(oAxis[2].x, oAxis[2].y, oAxis[2].z)) return result;

    // Test 9 cross-product axes (AABB axis × OBB axis)
    // AABB X × OBB axes
    if (!testAxis(0, -oAxis[0].z, oAxis[0].y)) return result;
    if (!testAxis(0, -oAxis[1].z, oAxis[1].y)) return result;
    if (!testAxis(0, -oAxis[2].z, oAxis[2].y)) return result;
    // AABB Y × OBB axes
    if (!testAxis(oAxis[0].z, 0, -oAxis[0].x)) return result;
    if (!testAxis(oAxis[1].z, 0, -oAxis[1].x)) return result;
    if (!testAxis(oAxis[2].z, 0, -oAxis[2].x)) return result;
    // AABB Z × OBB axes
    if (!testAxis(-oAxis[0].y, oAxis[0].x, 0)) return result;
    if (!testAxis(-oAxis[1].y, oAxis[1].x, 0)) return result;
    if (!testAxis(-oAxis[2].y, oAxis[2].x, 0)) return result;

    // All 15 axes overlap — collision confirmed
    result.hit = true;
    result.normal = mtvAxis;
    result.depth = minOverlap;
    return result;
}

// ============================================================
// Ray vs OBB — transform ray into OBB local space, do AABB test
// ============================================================

bool PhysicsWorld::RayOBB(const XMFLOAT3& origin, const XMFLOAT3& direction,
                           const PhysicsBody& obbBody, float& tHit, XMFLOAT3& hitNormal) {
    // Transform ray into OBB local space
    XMFLOAT3 oC = obbBody.obbCenter;
    XMFLOAT3 oH = obbBody.obbHalfExt;
    const XMFLOAT3X3& oR = obbBody.obbRotMat;

    // OBB axes
    XMFLOAT3 ax[3] = {
        { oR._11, oR._12, oR._13 },
        { oR._21, oR._22, oR._23 },
        { oR._31, oR._32, oR._33 },
    };

    // Vector from OBB center to ray origin
    XMFLOAT3 p = { origin.x - oC.x, origin.y - oC.y, origin.z - oC.z };

    // Project into local space
    XMFLOAT3 localOrigin = {
        p.x * ax[0].x + p.y * ax[0].y + p.z * ax[0].z,
        p.x * ax[1].x + p.y * ax[1].y + p.z * ax[1].z,
        p.x * ax[2].x + p.y * ax[2].y + p.z * ax[2].z,
    };
    XMFLOAT3 localDir = {
        direction.x * ax[0].x + direction.y * ax[0].y + direction.z * ax[0].z,
        direction.x * ax[1].x + direction.y * ax[1].y + direction.z * ax[1].z,
        direction.x * ax[2].x + direction.y * ax[2].y + direction.z * ax[2].z,
    };

    // Ray vs local AABB (-oH..+oH)
    AABB localBox = { {-oH.x, -oH.y, -oH.z}, {oH.x, oH.y, oH.z} };
    XMFLOAT3 localInvDir = {
        (fabsf(localDir.x) > 1e-8f) ? 1.0f / localDir.x : 1e8f,
        (fabsf(localDir.y) > 1e-8f) ? 1.0f / localDir.y : 1e8f,
        (fabsf(localDir.z) > 1e-8f) ? 1.0f / localDir.z : 1e8f,
    };

    float tMin, tMax;
    if (!RayAABB(localOrigin, localInvDir, localBox, tMin, tMax)) return false;
    if (tMin < 0.0f) tMin = tMax;
    if (tMin < 0.0f) return false;

    tHit = tMin;

    // Determine local hit face normal
    XMFLOAT3 localHitPt = {
        localOrigin.x + localDir.x * tMin,
        localOrigin.y + localDir.y * tMin,
        localOrigin.z + localDir.z * tMin,
    };

    float dx = localHitPt.x / oH.x;
    float dy = localHitPt.y / oH.y;
    float dz = localHitPt.z / oH.z;
    float adx = fabsf(dx), ady = fabsf(dy), adz = fabsf(dz);

    XMFLOAT3 localNorm = { 0, 0, 0 };
    if (adx > ady && adx > adz)
        localNorm = { dx > 0 ? 1.0f : -1.0f, 0, 0 };
    else if (ady > adz)
        localNorm = { 0, dy > 0 ? 1.0f : -1.0f, 0 };
    else
        localNorm = { 0, 0, dz > 0 ? 1.0f : -1.0f };

    // Transform normal back to world space
    hitNormal = {
        localNorm.x * ax[0].x + localNorm.y * ax[1].x + localNorm.z * ax[2].x,
        localNorm.x * ax[0].y + localNorm.y * ax[1].y + localNorm.z * ax[2].y,
        localNorm.x * ax[0].z + localNorm.y * ax[1].z + localNorm.z * ax[2].z,
    };

    return true;
}

} // namespace WT
