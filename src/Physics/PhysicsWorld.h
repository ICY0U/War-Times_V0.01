#pragma once

#include <DirectXMath.h>
#include <vector>
#include <cstdint>

namespace WT {

using namespace DirectX;

// Forward declarations
class Scene;
class DebugRenderer;

// ============================================================
// Collision Primitives
// ============================================================

// ---- Axis-Aligned Bounding Box ----
struct AABB {
    XMFLOAT3 min = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 max = { 0.0f, 0.0f, 0.0f };

    XMFLOAT3 Center() const {
        return { (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f };
    }

    XMFLOAT3 HalfExtents() const {
        return { (max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f };
    }

    XMFLOAT3 Size() const {
        return { max.x - min.x, max.y - min.y, max.z - min.z };
    }

    // Create AABB from center + half-extents
    static AABB FromCenterHalf(const XMFLOAT3& center, const XMFLOAT3& halfExt) {
        return {
            { center.x - halfExt.x, center.y - halfExt.y, center.z - halfExt.z },
            { center.x + halfExt.x, center.y + halfExt.y, center.z + halfExt.z }
        };
    }

    // Create AABB from position (bottom-center) + width/height/depth
    static AABB FromBottom(const XMFLOAT3& bottomCenter, float width, float height, float depth) {
        float hw = width * 0.5f;
        float hd = depth * 0.5f;
        return {
            { bottomCenter.x - hw, bottomCenter.y, bottomCenter.z - hd },
            { bottomCenter.x + hw, bottomCenter.y + height, bottomCenter.z + hd }
        };
    }
};

// ---- Collision result ----
struct CollisionHit {
    bool     hit = false;
    XMFLOAT3 normal   = { 0.0f, 0.0f, 0.0f };  // Push-out direction
    float    depth    = 0.0f;                     // Penetration depth
    int      entityIndex = -1;                    // Which entity was hit (-1 = world)
    int      voxelCellIndex = -1;                 // Which voxel cell was hit (-1 = not voxel)
};

// ---- Physics body type ----
enum class PhysicsBodyType : uint8_t {
    Static = 0,     // Doesn't move (entities, walls)
    Dynamic,        // Affected by physics (player, projectiles)
    Kinematic       // Moves but not affected by physics (AI agents)
};

// ---- Physics body ----
struct PhysicsBody {
    AABB box;                    // Broad-phase AABB (inflated for rotated bodies)
    XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };
    PhysicsBodyType type = PhysicsBodyType::Static;
    float mass       = 1.0f;
    float friction   = 0.3f;     // Ground friction (deceleration)
    float bounciness = 0.0f;     // Restitution (0 = no bounce)
    bool  onGround   = false;
    bool  enabled    = true;
    int   entityIndex = -1;      // Associated entity index (-1 = none)
    int   voxelCellIndex = -1;   // Voxel cell index (-1 = not a voxel cell)

    // ---- OBB data (for rotated static bodies) ----
    bool     hasRotation = false;    // If true, use OBB for narrow-phase
    XMFLOAT3 obbCenter   = { 0, 0, 0 };  // World-space center
    XMFLOAT3 obbHalfExt  = { 0, 0, 0 };  // Local-space half-extents (pre-rotation)
    XMFLOAT3X3 obbRotMat = {};            // 3x3 rotation matrix (local → world)
};

// ============================================================
// Physics World — manages collision detection & response
// ============================================================

struct PhysicsSettings {
    float gravity       = 18.0f;
    float groundY       = 0.0f;      // Flat ground plane Y
    bool  groundEnabled = true;      // Collide with infinite ground plane
    float maxVelocity   = 50.0f;     // Velocity clamp
    float skinWidth     = 0.01f;     // Small separation to prevent tunneling
    int   maxIterations = 4;         // Collision resolution iterations
};

class PhysicsWorld {
public:
    void Init();
    void Shutdown();

    // ---- Body management ----
    int  AddBody(const PhysicsBody& body);
    void RemoveBody(int index);
    void ClearBodies();
    int  GetBodyCount() const { return static_cast<int>(m_bodies.size()); }
    PhysicsBody& GetBody(int index) { return m_bodies[index]; }
    const PhysicsBody& GetBody(int index) const { return m_bodies[index]; }

    // ---- Static colliders from scene entities ----
    void RebuildStaticColliders(const Scene& scene);

    // ---- Simulation ----
    void Step(float dt, const PhysicsSettings& settings);

    // ---- Queries ----
    // Test if an AABB overlaps any collider. Returns first hit.
    CollisionHit TestAABB(const AABB& box, int ignoreBodyIndex = -1) const;

    // Sweep an AABB along a direction, returns collision info
    CollisionHit SweepAABB(const AABB& box, const XMFLOAT3& displacement,
                           int ignoreBodyIndex = -1) const;

    // Point inside any collider?
    bool PointInside(const XMFLOAT3& point) const;

    // ---- Raycast ----
    CollisionHit Raycast(const XMFLOAT3& origin, const XMFLOAT3& direction,
                         float maxDist = 1000.0f) const;

    // ---- Debug ----
    void DebugDraw(DebugRenderer& debug) const;
    bool showDebug = false;

    // ---- Settings access ----
    PhysicsSettings& GetSettings() { return m_settings; }
    const PhysicsSettings& GetSettings() const { return m_settings; }

private:
    // AABB vs AABB overlap test
    static bool AABBOverlap(const AABB& a, const AABB& b);

    // Compute minimum translation vector to resolve overlap
    static CollisionHit AABBResolve(const AABB& moving, const AABB& stationary);

    // OBB vs AABB collision test (SAT-based, returns MTV)
    static CollisionHit OBBvsAABB(const PhysicsBody& obbBody, const AABB& aabb);

    // Ray vs OBB test
    static bool RayOBB(const XMFLOAT3& origin, const XMFLOAT3& direction,
                       const PhysicsBody& obbBody, float& tHit, XMFLOAT3& hitNormal);

    // AABB vs ground plane
    CollisionHit GroundTest(const AABB& box) const;

    // Ray vs AABB test
    static bool RayAABB(const XMFLOAT3& origin, const XMFLOAT3& invDir,
                        const AABB& box, float& tMin, float& tMax);

    std::vector<PhysicsBody> m_bodies;
    PhysicsSettings m_settings;
};

} // namespace WT
