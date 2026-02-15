#include "PhysicsWorld.h"
#include "../Core/Entity.h"
#include "../Graphics/DebugRenderer.h"
#include <algorithm>
#include <cmath>
#include <cfloat>

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
        if (!e.visible) continue;

        PhysicsBody body;
        body.type = PhysicsBodyType::Static;
        body.entityIndex = i;
        body.enabled = true;

        // Entity position is CENTER, scale is full size
        // Cube mesh is (-0.5..0.5), so half-extents = scale * 0.5
        body.box = AABB::FromCenterHalf(
            { e.position[0], e.position[1], e.position[2] },
            { e.scale[0] * 0.5f, e.scale[1] * 0.5f, e.scale[2] * 0.5f }
        );

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

                if (AABBOverlap(body.box, other.box)) {
                    CollisionHit hit = AABBResolve(body.box, other.box);
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

        if (AABBOverlap(box, b.box)) {
            CollisionHit hit = AABBResolve(box, b.box);
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

        float tMin, tMax;
        if (RayAABB(origin, invDir, body.box, tMin, tMax)) {
            if (tMin > 0.0f && tMin < closestT) {
                closestT = tMin;
                closest.hit = true;
                closest.depth = tMin;
                closest.entityIndex = body.entityIndex;

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

    return closest;
}

// ============================================================
// Debug drawing
// ============================================================

void PhysicsWorld::DebugDraw(DebugRenderer& debug) const {
    if (!showDebug) return;

    for (const auto& body : m_bodies) {
        if (!body.enabled) continue;

        XMFLOAT3 center = body.box.Center();
        XMFLOAT3 halfExt = body.box.HalfExtents();

        XMFLOAT4 color;
        switch (body.type) {
            case PhysicsBodyType::Static:    color = { 0.0f, 1.0f, 0.0f, 1.0f }; break;
            case PhysicsBodyType::Dynamic:   color = { 1.0f, 0.5f, 0.0f, 1.0f }; break;
            case PhysicsBodyType::Kinematic: color = { 0.0f, 0.5f, 1.0f, 1.0f }; break;
        }

        debug.DrawBox(center, halfExt, color);
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

} // namespace WT
