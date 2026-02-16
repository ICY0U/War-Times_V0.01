#pragma once

#include <DirectXMath.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>

namespace WT {

// Forward declaration for cached texture pointer
class Texture;

using namespace DirectX;

// ---- Mesh type for spawnable objects ----
enum class MeshType : uint8_t {
    Cube = 0,
    Custom,       // OBJ model referenced by meshName
    Count
};

inline const char* MeshTypeName(MeshType type) {
    switch (type) {
        case MeshType::Cube:   return "Cube";
        case MeshType::Custom: return "Custom";
        default:               return "Unknown";
    }
}

// ---- Material type — affects destruction FX, debris, and health scaling ----
enum class MaterialType : uint8_t {
    Concrete = 0,   // Heavy dust, gray debris, some sparks
    Wood,           // Splinters, warm dust/embers, less sparks
    Metal,          // Lots of sparks, metallic debris, minimal dust
    Glass,          // Shatters into fast sparks, minimal debris
    Count
};

inline const char* MaterialTypeName(MaterialType type) {
    switch (type) {
        case MaterialType::Concrete: return "Concrete";
        case MaterialType::Wood:     return "Wood";
        case MaterialType::Metal:    return "Metal";
        case MaterialType::Glass:    return "Glass";
        default:                     return "Unknown";
    }
}

// ---- Pickup type — items the player can collect ----
enum class PickupType : uint8_t {
    None = 0,       // Not a pickup (normal entity)
    Health,         // Restores player health
    Ammo,           // Adds reserve ammo
    Count
};

inline const char* PickupTypeName(PickupType type) {
    switch (type) {
        case PickupType::None:   return "None";
        case PickupType::Health: return "Health";
        case PickupType::Ammo:   return "Ammo";
        default:                 return "Unknown";
    }
}

// ---- Entity — a spawnable scene object ----
struct Entity {
    std::string name       = "Entity";
    MeshType    meshType   = MeshType::Cube;
    std::string meshName;     // For MeshType::Custom — name key in ResourceManager
    std::string textureName;   // Optional texture override (key in ResourceManager)

    // Cached texture pointer — resolved once, avoids per-frame hash map lookup
    mutable Texture* cachedTexture    = nullptr;
    mutable bool     textureCacheDirty = true;

    float       position[3] = { 0.0f, 0.0f, 0.0f };
    float       rotation[3] = { 0.0f, 0.0f, 0.0f };  // Euler angles (degrees)
    float       scale[3]    = { 1.0f, 1.0f, 1.0f };
    float       color[4]    = { 0.6f, 0.6f, 0.6f, 1.0f };
    bool        visible     = true;
    bool        castShadow  = true;

    // Material type (affects destruction behavior & FX)
    MaterialType materialType = MaterialType::Concrete;

    // Destruction properties
    bool        destructible = true;    // Can be destroyed by weapons
    float       health       = 100.0f;  // Hit points remaining
    float       maxHealth    = 100.0f;  // Starting hit points
    int         debrisCount  = 6;       // Number of debris chunks on destroy
    float       debrisScale  = 0.3f;    // Scale of debris relative to entity

    // Damage visual state
    float       damageFlashTimer = 0.0f;      // Remaining flash time (seconds)
    float       damageFlashDuration = 0.15f;   // How long flash lasts
    float       damageFlashColor[3] = { 1.0f, 0.3f, 0.1f }; // Flash color (orange-red)
    bool        smokeOnDamage  = true;         // Spawn smoke when below 50% health

    // Structural support — name of entity that supports this one
    // When the support entity is destroyed, this entity collapses too
    std::string supportedBy;

    // Breakable sub-pieces — on destroy, spawn smaller non-destructible chunks
    int         breakPieceCount = 3;     // Number of sub-piece entities to spawn

    // Debris properties
    bool        noCollision  = false;    // Skip collision (debris pieces)
    float       despawnTimer = -1.0f;    // Time until auto-remove (-1 = never)

    // Pickup system
    PickupType  pickupType   = PickupType::None;  // What this entity gives when collected
    float       pickupAmount = 25.0f;              // Health restored or ammo added
    float       pickupRadius = 1.5f;               // Auto-collect radius
    float       pickupBobSpeed = 2.0f;             // Bobbing animation speed
    float       pickupBobHeight = 0.15f;           // Bobbing amplitude
    float       pickupSpinSpeed = 90.0f;           // Spin degrees/sec
    float       pickupRespawnTime = 15.0f;         // Seconds until respawn (-1 = no respawn)
    float       pickupRespawnTimer = 0.0f;         // Current respawn countdown
    bool        pickupCollected = false;           // Currently collected (waiting for respawn)

    // Hit decals (bullet scars) — up to 4 world-space positions tracked
    static constexpr int MAX_HIT_DECALS = 4;
    XMFLOAT3    hitDecalPos[MAX_HIT_DECALS] = {};
    float       hitDecalIntensity[MAX_HIT_DECALS] = {};
    int         hitDecalCount = 0;
    int         hitDecalNext  = 0;       // Ring-buffer write index

    // Voxel chunk destruction (simple grid subdivision)
    bool        voxelDestruction = false;  // Enable voxel grid mode
    int         voxelRes         = 2;      // Grid resolution (2..8 → 2x2x2 to 8x8x8)
    uint64_t    voxelMask[8]     = { ~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL }; // 512-bit mask

    // ---- Voxel bit helpers ----
    bool IsVoxelCellActive(int idx) const {
        if (idx < 0 || idx >= 512) return false;
        return (voxelMask[idx / 64] & (1ULL << (idx % 64))) != 0;
    }
    void ClearVoxelCell(int idx) {
        if (idx >= 0 && idx < 512)
            voxelMask[idx / 64] &= ~(1ULL << (idx % 64));
    }
    void ResetVoxelMask() {
        int total = voxelRes * voxelRes * voxelRes;
        for (int w = 0; w < 8; w++) {
            int lo = w * 64;
            int hi = lo + 64;
            if (total <= lo) { voxelMask[w] = 0; }
            else if (total >= hi) { voxelMask[w] = ~0ULL; }
            else { voxelMask[w] = (1ULL << (total - lo)) - 1ULL; }
        }
    }

    // ---- Methods ----

    // Add a bullet scar decal at world position
    void AddHitDecal(float wx, float wy, float wz) {
        hitDecalPos[hitDecalNext] = { wx, wy, wz };
        hitDecalIntensity[hitDecalNext] = 1.0f;
        hitDecalNext = (hitDecalNext + 1) % MAX_HIT_DECALS;
        if (hitDecalCount < MAX_HIT_DECALS) hitDecalCount++;
    }

    // Count of active voxel cells
    int GetActiveVoxelCount() const {
        int total = voxelRes * voxelRes * voxelRes;
        int c = 0;
        for (int i = 0; i < total; i++)
            if (IsVoxelCellActive(i)) c++;
        return c;
    }

    // Remove a voxel cell by its direct index (from physics raycast)
    bool RemoveVoxelCell(int cellIndex) {
        if (!voxelDestruction || cellIndex < 0) return false;
        int res = voxelRes;
        int total = res * res * res;
        if (cellIndex >= total) return false;
        if (!IsVoxelCellActive(cellIndex)) return false;

        ClearVoxelCell(cellIndex);

        // Punch through thin axes
        int cx = cellIndex % res;
        int cy = (cellIndex / res) % res;
        int cz = cellIndex / (res * res);

        float cellSX = scale[0] / res;
        float cellSY = scale[1] / res;
        float cellSZ = scale[2] / res;
        bool thinX = cellSX < 0.5f;
        bool thinY = cellSY < 0.5f;
        bool thinZ = cellSZ < 0.5f;

        if (thinX || thinY || thinZ) {
            for (int iz = 0; iz < res; iz++)
            for (int iy = 0; iy < res; iy++)
            for (int ix = 0; ix < res; ix++) {
                bool matchX = thinX || (ix == cx);
                bool matchY = thinY || (iy == cy);
                bool matchZ = thinZ || (iz == cz);
                if (matchX && matchY && matchZ) {
                    int ci = ix + iy * res + iz * res * res;
                    ClearVoxelCell(ci);
                }
            }
        }

        CollapseFloatingCells();
        return true;
    }

    // Remove the voxel cell at the given world-space hit point (fallback)
    bool RemoveVoxelAt(float hitX, float hitY, float hitZ) {
        if (!voxelDestruction) return false;
        // Transform hit into entity local space (inverse rotation)
        float dx = hitX - position[0];
        float dy = hitY - position[1];
        float dz = hitZ - position[2];
        XMMATRIX R = XMMatrixRotationRollPitchYaw(
            XMConvertToRadians(rotation[0]),
            XMConvertToRadians(rotation[1]),
            XMConvertToRadians(rotation[2]));
        XMMATRIX invR = XMMatrixTranspose(R);
        XMVECTOR localV = XMVector3TransformNormal(XMVectorSet(dx, dy, dz, 0), invR);
        XMFLOAT3 local;
        XMStoreFloat3(&local, localV);
        // Normalize to -0.5 .. 0.5 range
        float lx = local.x / scale[0];
        float ly = local.y / scale[1];
        float lz = local.z / scale[2];
        int res = voxelRes;
        int cx = (int)((lx + 0.5f) * res);
        int cy = (int)((ly + 0.5f) * res);
        int cz = (int)((lz + 0.5f) * res);
        cx = (cx < 0) ? 0 : (cx >= res ? res - 1 : cx);
        cy = (cy < 0) ? 0 : (cy >= res ? res - 1 : cy);
        cz = (cz < 0) ? 0 : (cz >= res ? res - 1 : cz);
        int idx = cx + cy * res + cz * res * res;
        if (IsVoxelCellActive(idx)) {
            ClearVoxelCell(idx);

            // Punch through thin axes: if a cell is thinner than 0.5 units
            // along any axis, remove ALL cells along that axis at the same position.
            // This prevents thin walls from having invisible back-layer cells.
            float cellSX = scale[0] / res;
            float cellSY = scale[1] / res;
            float cellSZ = scale[2] / res;
            bool thinX = cellSX < 0.5f;
            bool thinY = cellSY < 0.5f;
            bool thinZ = cellSZ < 0.5f;

            if (thinX || thinY || thinZ) {
                for (int iz = 0; iz < res; iz++)
                for (int iy = 0; iy < res; iy++)
                for (int ix = 0; ix < res; ix++) {
                    // Keep this cell's coordinates on non-thin axes, iterate thin axes
                    bool matchX = thinX || (ix == cx);
                    bool matchY = thinY || (iy == cy);
                    bool matchZ = thinZ || (iz == cz);
                    if (matchX && matchY && matchZ) {
                        int ci = ix + iy * res + iz * res * res;
                        ClearVoxelCell(ci);
                    }
                }
            }

            // Floating cell collapse: remove unsupported cells above
            CollapseFloatingCells();
            return true;
        }
        return false;
    }

    // Collapse any voxel cells that have no support below them (gravity)
    void CollapseFloatingCells() {
        int res = voxelRes;
        bool changed = true;
        while (changed) {
            changed = false;
            for (int vz = 0; vz < res; vz++)
            for (int vx = 0; vx < res; vx++) {
                // Check each cell from bottom to top
                for (int vy = 1; vy < res; vy++) {
                    int idx = vx + vy * res + vz * res * res;
                    if (!IsVoxelCellActive(idx)) continue; // cell empty

                    // Check if ANY cell below (in the same column) is active
                    bool hasSupport = false;
                    for (int by = 0; by < vy; by++) {
                        int belowIdx = vx + by * res + vz * res * res;
                        if (IsVoxelCellActive(belowIdx)) {
                            hasSupport = true;
                            break;
                        }
                    }

                    if (!hasSupport) {
                        ClearVoxelCell(idx);
                        changed = true;
                    }
                }
            }
        }
    }

    // Get health fraction 0..1
    float GetHealthFraction() const {
        return (maxHealth > 0) ? health / maxHealth : 0.0f;
    }

    // Get the damage stage: 0 = pristine, 1 = scratched, 2 = damaged, 3 = critical
    int GetDamageStage() const {
        float frac = GetHealthFraction();
        if (frac > 0.75f) return 0;
        if (frac > 0.50f) return 1;
        if (frac > 0.25f) return 2;
        return 3;
    }

    // Get darkened/damaged color based on health (entities darken as they take damage)
    void GetDamagedColor(float out[4]) const {
        out[0] = color[0];
        out[1] = color[1];
        out[2] = color[2];
        out[3] = color[3];
    }

    // Apply damage, returns true if destroyed
    bool TakeDamage(float damage) {
        if (!destructible) return false;
        health -= damage;
        return health <= 0.0f;
    }

    bool IsDestroyed() const { return destructible && health <= 0.0f; }

    // Compute world matrix
    XMMATRIX GetWorldMatrix() const {
        XMMATRIX S = XMMatrixScaling(scale[0], scale[1], scale[2]);
        XMMATRIX R = XMMatrixRotationRollPitchYaw(
            XMConvertToRadians(rotation[0]),
            XMConvertToRadians(rotation[1]),
            XMConvertToRadians(rotation[2]));
        XMMATRIX T = XMMatrixTranslation(position[0], position[1], position[2]);
        return S * R * T;
    }

    // Compute world matrix for a single voxel cell (used during voxel rendering)
    XMMATRIX GetVoxelCellWorldMatrix(int cx, int cy, int cz) const {
        int res = voxelRes;
        float cellSX = scale[0] / res;
        float cellSY = scale[1] / res;
        float cellSZ = scale[2] / res;

        // Cell center in entity-local space (before rotation)
        float offX = ((cx + 0.5f) / res - 0.5f) * scale[0];
        float offY = ((cy + 0.5f) / res - 0.5f) * scale[1];
        float offZ = ((cz + 0.5f) / res - 0.5f) * scale[2];

        XMMATRIX R = XMMatrixRotationRollPitchYaw(
            XMConvertToRadians(rotation[0]),
            XMConvertToRadians(rotation[1]),
            XMConvertToRadians(rotation[2]));

        // Rotate offset by entity rotation
        XMVECTOR off = XMVectorSet(offX, offY, offZ, 0);
        off = XMVector3TransformNormal(off, R);
        XMFLOAT3 rOff;
        XMStoreFloat3(&rOff, off);

        XMMATRIX S = XMMatrixScaling(cellSX, cellSY, cellSZ);
        XMMATRIX T = XMMatrixTranslation(
            position[0] + rOff.x,
            position[1] + rOff.y,
            position[2] + rOff.z);
        return S * R * T;
    }
};

// ---- Scene — manages entity list ----
class Scene {
public:
    // Add a new entity, returns its index
    int AddEntity(const std::string& name = "", MeshType type = MeshType::Cube) {
        Entity e;
        if (name.empty()) {
            e.name = MeshTypeName(type) + std::string("_") + std::to_string(m_nextId++);
        } else {
            e.name = name;
            m_nextId++;
        }
        e.meshType = type;
        m_entities.push_back(e);
        return static_cast<int>(m_entities.size()) - 1;
    }

    // Remove entity by index
    void RemoveEntity(int index) {
        if (index >= 0 && index < static_cast<int>(m_entities.size())) {
            m_entities.erase(m_entities.begin() + index);
        }
    }

    // Duplicate entity
    int DuplicateEntity(int index) {
        if (index < 0 || index >= static_cast<int>(m_entities.size())) return -1;
        Entity copy = m_entities[index];
        copy.name += "_copy";
        // Offset slightly so it's visible
        copy.position[0] += 1.0f;
        m_entities.push_back(copy);
        return static_cast<int>(m_entities.size()) - 1;
    }

    // Accessors
    Entity& GetEntity(int index) { return m_entities[index]; }
    const Entity& GetEntity(int index) const { return m_entities[index]; }
    int GetEntityCount() const { return static_cast<int>(m_entities.size()); }
    std::vector<Entity>& GetEntities() { return m_entities; }
    const std::vector<Entity>& GetEntities() const { return m_entities; }

    void Clear() { m_entities.clear(); m_nextId = 0; }

private:
    std::vector<Entity> m_entities;
    int m_nextId = 0;
};

} // namespace WT
