#pragma once

#include <DirectXMath.h>
#include <string>
#include <vector>
#include <cstdint>

namespace WT {

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

// ---- Entity — a spawnable scene object ----
struct Entity {
    std::string name       = "Entity";
    MeshType    meshType   = MeshType::Cube;
    std::string meshName;     // For MeshType::Custom — name key in ResourceManager
    std::string textureName;   // Optional texture override (key in ResourceManager)
    float       position[3] = { 0.0f, 0.0f, 0.0f };
    float       rotation[3] = { 0.0f, 0.0f, 0.0f };  // Euler angles (degrees)
    float       scale[3]    = { 1.0f, 1.0f, 1.0f };
    float       color[4]    = { 0.6f, 0.6f, 0.6f, 1.0f };
    bool        visible     = true;
    bool        castShadow  = true;

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
        float frac = GetHealthFraction();
        float dark = 0.4f + 0.6f * frac; // 100% → 1.0, 0% → 0.4
        // Flash override
        if (damageFlashTimer > 0.0f) {
            float flashT = damageFlashTimer / damageFlashDuration;
            out[0] = color[0] * dark * (1.0f - flashT) + damageFlashColor[0] * flashT;
            out[1] = color[1] * dark * (1.0f - flashT) + damageFlashColor[1] * flashT;
            out[2] = color[2] * dark * (1.0f - flashT) + damageFlashColor[2] * flashT;
            out[3] = color[3];
        } else {
            out[0] = color[0] * dark;
            out[1] = color[1] * dark;
            out[2] = color[2] * dark;
            out[3] = color[3];
        }
    }

    // Apply damage, returns true if destroyed
    bool TakeDamage(float damage) {
        if (!destructible) return false;
        health -= damage;
        damageFlashTimer = damageFlashDuration; // trigger flash
        return health <= 0.0f;
    }

    bool IsDestroyed() const { return destructible && health <= 0.0f; }

    // Compute world matrix
    XMMATRIX GetWorldMatrix() const {
        // Slight random tilt when damaged (visual instability)
        float tiltX = 0.0f, tiltZ = 0.0f;
        if (destructible && maxHealth > 0) {
            int stage = GetDamageStage();
            if (stage >= 2) {
                // Use entity position as seed for deterministic wobble
                float seed = position[0] * 3.14f + position[2] * 2.71f;
                tiltX = sinf(seed) * (stage == 3 ? 3.0f : 1.5f);
                tiltZ = cosf(seed * 0.7f) * (stage == 3 ? 2.5f : 1.0f);
            }
        }

        XMMATRIX S = XMMatrixScaling(scale[0], scale[1], scale[2]);
        XMMATRIX R = XMMatrixRotationRollPitchYaw(
            XMConvertToRadians(rotation[0] + tiltX),
            XMConvertToRadians(rotation[1]),
            XMConvertToRadians(rotation[2] + tiltZ));
        XMMATRIX T = XMMatrixTranslation(position[0], position[1], position[2]);
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
