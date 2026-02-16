#include "WeaponSystem.h"
#include "Core/Input.h"
#include "Graphics/Camera.h"
#include "Graphics/DebugRenderer.h"
#include "Physics/PhysicsWorld.h"
#include "AI/AIAgent.h"
#include "Util/MathHelpers.h"
#include "Util/Log.h"
#include <cmath>
#include <cstdlib>

namespace WT {

// ============================================================
// Init / Shutdown
// ============================================================

void WeaponSystem::Init() {
    // ============================================================
    // Standard hand/arm scale (shared across all weapons)
    // ============================================================
    const float HAND_SX = 0.05f,  HAND_SY = 0.06f,  HAND_SZ = 0.05f;
    const float ARM_SX  = 0.055f, ARM_SY  = 0.06f,  ARM_SZ  = 0.12f;

    // ---- Rifle (default) ----
    auto& rifle = m_weaponDefs[static_cast<int>(WeaponType::Rifle)];
    rifle.type          = WeaponType::Rifle;
    rifle.fireRate      = 0.12f;
    rifle.damage        = 25.0f;
    rifle.range         = 200.0f;
    rifle.maxAmmo       = 30;
    rifle.reserveAmmo   = 90;
    rifle.reloadTime    = 2.0f;
    rifle.recoilPitch   = 1.5f;
    rifle.recoilYaw     = 0.3f;
    rifle.recoilRecovery = 6.0f;
    rifle.spread        = 0.5f;
    rifle.adsSpreadMult = 0.3f;
    rifle.pelletsPerShot = 1;
    rifle.automatic     = true;
    rifle.barrelLength  = 0.6f;
    rifle.barrelWidth   = 0.06f;
    rifle.stockLength   = 0.25f;
    rifle.bodyWidth     = 0.1f;
    rifle.bodyHeight    = 0.12f;
    rifle.gunModelName  = "Gun/AssaultRifle1";
    rifle.gunTextureName = "Guns/Palette -Gun";
    rifle.modelScale    = 1.0f;
    rifle.modelOffsetX  = -0.14f;
    rifle.modelOffsetY  = 0.07f;
    rifle.modelOffsetZ  = -0.19f;
    rifle.modelRotX     = 3.0f;
    rifle.modelRotY     = 3.0f;
    // Rifle hands — two-handed grip, relative to gun model origin
    rifle.rightHandFwd   = -0.02f;
    rifle.rightHandRight =  0.01f;
    rifle.rightHandDown  =  0.07f;
    rifle.rightHandScaleX = HAND_SX; rifle.rightHandScaleY = HAND_SY; rifle.rightHandScaleZ = HAND_SZ;
    rifle.rightArmFwd    = -0.10f;
    rifle.rightArmRight  =  0.02f;
    rifle.rightArmDown   =  0.05f;
    rifle.rightArmPitch  =  12.0f;
    rifle.rightArmScaleX = ARM_SX; rifle.rightArmScaleY = ARM_SY; rifle.rightArmScaleZ = ARM_SZ;
    rifle.leftHandFwd    =  0.15f;
    rifle.leftHandRight  =  0.00f;
    rifle.leftHandDown   =  0.04f;
    rifle.leftHandScaleX = HAND_SX; rifle.leftHandScaleY = HAND_SY; rifle.leftHandScaleZ = HAND_SZ;
    rifle.leftArmFwd     =  0.06f;
    rifle.leftArmRight   = -0.02f;
    rifle.leftArmDown    =  0.02f;
    rifle.leftArmPitch   = -5.0f;
    rifle.leftArmScaleX  = ARM_SX; rifle.leftArmScaleY = ARM_SY; rifle.leftArmScaleZ = ARM_SZ;

    // Rifle muzzle flash: sharp directional burst — bright core + elongated forward spike + side flares
    rifle.flashDuration     = 0.045f;
    rifle.flashMuzzleOffset = 0.0f;
    rifle.flashLayerCount   = 4;
    // Layer 0: Hot white core (small, bright)
    rifle.flashLayers[0] = { 0.025f, 0.025f, 0.03f,  0.0f, 0.0f, 0.0f,  0.0f,   1.0f, 1.0f, 0.95f,  1.2f, 0.3f };
    // Layer 1: Forward spike (elongated along barrel)
    rifle.flashLayers[1] = { 0.015f, 0.015f, 0.08f,  0.03f, 0.0f, 0.0f, 0.0f,   1.0f, 0.85f, 0.3f,  1.0f, 0.6f };
    // Layer 2: Side flare (horizontal, rotated 90 deg)
    rifle.flashLayers[2] = { 0.04f, 0.012f, 0.015f,  0.0f, 0.0f, 0.0f, 0.0f,    1.0f, 0.7f, 0.2f,   1.5f, 0.4f };
    // Layer 3: Vertical flare (cross pattern with horizontal)
    rifle.flashLayers[3] = { 0.012f, 0.04f, 0.015f,  0.0f, 0.0f, 0.0f, 0.0f,    1.0f, 0.7f, 0.2f,   1.5f, 0.4f };

    // ---- Pistol ----
    auto& pistol = m_weaponDefs[static_cast<int>(WeaponType::Pistol)];
    pistol.type          = WeaponType::Pistol;
    pistol.fireRate      = 0.2f;
    pistol.damage        = 35.0f;
    pistol.range         = 100.0f;
    pistol.maxAmmo       = 12;
    pistol.reserveAmmo   = 36;
    pistol.reloadTime    = 1.5f;
    pistol.recoilPitch   = 3.0f;
    pistol.recoilYaw     = 0.5f;
    pistol.recoilRecovery = 8.0f;
    pistol.spread        = 0.8f;
    pistol.adsSpreadMult = 0.4f;
    pistol.pelletsPerShot = 1;
    pistol.automatic     = false;
    pistol.barrelLength  = 0.3f;
    pistol.barrelWidth   = 0.04f;
    pistol.stockLength   = 0.0f;
    pistol.bodyWidth     = 0.08f;
    pistol.bodyHeight    = 0.14f;
    pistol.gunModelName  = "Gun/Pistol1";
    pistol.gunTextureName = "Guns/Palette -Gun";
    pistol.modelScale    = 1.0f;
    pistol.modelOffsetX  = -0.13f;
    pistol.modelOffsetY  =  0.07f;
    pistol.modelOffsetZ  = -0.13f;
    pistol.modelRotX     = 4.0f;
    pistol.modelRotY     = 0.0f;
    // Pistol hands — right hand on grip, left hand wraps under for support
    pistol.rightHandFwd   = -0.01f;
    pistol.rightHandRight =  0.01f;
    pistol.rightHandDown  =  0.05f;
    pistol.rightHandScaleX = HAND_SX; pistol.rightHandScaleY = HAND_SY; pistol.rightHandScaleZ = HAND_SZ;
    pistol.rightArmFwd    = -0.09f;
    pistol.rightArmRight  =  0.03f;
    pistol.rightArmDown   =  0.03f;
    pistol.rightArmPitch  =  15.0f;
    pistol.rightArmScaleX = ARM_SX; pistol.rightArmScaleY = ARM_SY; pistol.rightArmScaleZ = ARM_SZ;
    pistol.leftHandFwd    =  0.01f;
    pistol.leftHandRight  = -0.02f;
    pistol.leftHandDown   =  0.05f;
    pistol.leftHandScaleX = HAND_SX; pistol.leftHandScaleY = HAND_SY; pistol.leftHandScaleZ = HAND_SZ;
    pistol.leftArmFwd     = -0.05f;
    pistol.leftArmRight   = -0.04f;
    pistol.leftArmDown    =  0.03f;
    pistol.leftArmPitch   =  5.0f;
    pistol.leftArmScaleX  = ARM_SX; pistol.leftArmScaleY = ARM_SY; pistol.leftArmScaleZ = ARM_SZ;

    // Pistol muzzle flash: punchy round burst — bright core + wide round bloom
    pistol.flashDuration     = 0.04f;
    pistol.flashMuzzleOffset = 0.0f;
    pistol.flashLayerCount   = 3;
    // Layer 0: Hot white-yellow core
    pistol.flashLayers[0] = { 0.02f, 0.02f, 0.02f,   0.0f, 0.0f, 0.0f,  0.0f,   1.0f, 1.0f, 0.9f,   1.3f, 0.2f };
    // Layer 1: Round orange bloom (equal XY, shallow Z)
    pistol.flashLayers[1] = { 0.035f, 0.035f, 0.01f,  0.0f, 0.0f, 0.0f, 45.0f,  1.0f, 0.7f, 0.15f,  1.0f, 0.8f };
    // Layer 2: Tiny forward spit
    pistol.flashLayers[2] = { 0.01f, 0.01f, 0.04f,    0.015f, 0.0f, 0.0f, 0.0f,  1.0f, 0.9f, 0.4f,   1.5f, 0.5f };

    // ---- Shotgun ----
    auto& shotgun = m_weaponDefs[static_cast<int>(WeaponType::Shotgun)];
    shotgun.type          = WeaponType::Shotgun;
    shotgun.fireRate      = 0.8f;
    shotgun.damage        = 15.0f;   // Per pellet
    shotgun.range         = 40.0f;
    shotgun.maxAmmo       = 6;
    shotgun.reserveAmmo   = 24;
    shotgun.reloadTime    = 2.5f;
    shotgun.recoilPitch   = 5.0f;
    shotgun.recoilYaw     = 0.8f;
    shotgun.recoilRecovery = 4.0f;
    shotgun.spread        = 4.0f;
    shotgun.adsSpreadMult = 0.6f;
    shotgun.pelletsPerShot = 8;
    shotgun.automatic     = false;
    shotgun.barrelLength  = 0.7f;
    shotgun.barrelWidth   = 0.07f;
    shotgun.stockLength   = 0.3f;
    shotgun.bodyWidth     = 0.12f;
    shotgun.bodyHeight    = 0.1f;
    shotgun.gunModelName  = "Guns/Shotgun_I";
    shotgun.modelScale    = 0.50f;
    shotgun.modelOffsetX  = -0.12f;
    shotgun.modelOffsetY  =  0.06f;
    shotgun.modelOffsetZ  = -0.15f;
    shotgun.modelRotY     = -90.0f;
    // Shotgun hands — two-handed, left hand farther forward on pump
    shotgun.rightHandFwd   = -0.02f;
    shotgun.rightHandRight =  0.01f;
    shotgun.rightHandDown  =  0.06f;
    shotgun.rightHandScaleX = HAND_SX; shotgun.rightHandScaleY = HAND_SY; shotgun.rightHandScaleZ = HAND_SZ;
    shotgun.rightArmFwd    = -0.10f;
    shotgun.rightArmRight  =  0.02f;
    shotgun.rightArmDown   =  0.04f;
    shotgun.rightArmPitch  =  10.0f;
    shotgun.rightArmScaleX = ARM_SX; shotgun.rightArmScaleY = ARM_SY; shotgun.rightArmScaleZ = ARM_SZ;
    shotgun.leftHandFwd    =  0.26f;
    shotgun.leftHandRight  = -0.01f;
    shotgun.leftHandDown   =  0.05f;
    shotgun.leftHandScaleX = HAND_SX; shotgun.leftHandScaleY = HAND_SY; shotgun.leftHandScaleZ = HAND_SZ;
    shotgun.leftArmFwd     =  0.15f;
    shotgun.leftArmRight   = -0.04f;
    shotgun.leftArmDown    =  0.04f;
    shotgun.leftArmPitch   = -3.0f;
    shotgun.leftArmScaleX  = ARM_SX; shotgun.leftArmScaleY = ARM_SY; shotgun.leftArmScaleZ = ARM_SZ;

    // Shotgun muzzle flash: big violent blast — huge fireball + wide spread + sparks
    shotgun.flashDuration     = 0.07f;
    shotgun.flashMuzzleOffset = 0.0f;
    shotgun.flashLayerCount   = 5;
    // Layer 0: Massive white-hot core
    shotgun.flashLayers[0] = { 0.04f, 0.04f, 0.04f,   0.0f, 0.0f, 0.0f,  0.0f,   1.0f, 1.0f, 0.9f,   1.0f, 0.4f };
    // Layer 1: Big forward fireball
    shotgun.flashLayers[1] = { 0.03f, 0.03f, 0.12f,   0.04f, 0.0f, 0.0f, 0.0f,   1.0f, 0.75f, 0.2f,  0.8f, 0.9f };
    // Layer 2: Wide horizontal blast
    shotgun.flashLayers[2] = { 0.07f, 0.02f, 0.025f,  0.0f, 0.0f, 0.0f, 0.0f,    1.0f, 0.6f, 0.1f,   1.2f, 0.6f };
    // Layer 3: Vertical cross blast
    shotgun.flashLayers[3] = { 0.02f, 0.06f, 0.025f,  0.0f, 0.0f, 0.0f, 0.0f,    1.0f, 0.6f, 0.1f,   1.2f, 0.6f };
    // Layer 4: Upper-right spark/ember
    shotgun.flashLayers[4] = { 0.015f, 0.015f, 0.02f,  0.02f, 0.015f, 0.01f, 25.0f, 1.0f, 0.5f, 0.05f, 2.0f, 1.5f };

    // Start with rifle
    SwitchWeapon(WeaponType::Rifle);

    LOG_INFO("WeaponSystem initialized (%d weapon types)", static_cast<int>(WeaponType::Count));
}

void WeaponSystem::Shutdown() {
    m_viewmodelParts.clear();
    LOG_INFO("WeaponSystem shutdown");
}

// ============================================================
// Switch Weapon
// ============================================================

void WeaponSystem::SwitchWeapon(WeaponType type) {
    m_currentWeapon = type;
    const auto& def = GetCurrentDef();
    m_currentAmmo  = def.maxAmmo;
    m_reserveAmmo  = def.reserveAmmo;
    m_reloading    = false;
    m_reloadTimer  = 0.0f;
    m_fireTimer    = 0.0f;
    m_muzzleFlashTimer = 0.0f;
    m_recoilPitchAccum = 0.0f;
    m_recoilYawAccum   = 0.0f;
    m_triggerHeld  = false;
    LOG_INFO("Switched to %s", WeaponTypeName(type));
}

// ============================================================
// Update
// ============================================================

void WeaponSystem::Update(float dt, Input& input, Camera& camera,
                           const Character& character, bool editorWantsMouse,
                           PhysicsWorld* physics, AISystem* aiSystem) {
    const auto& def = GetCurrentDef();

    // Clear per-frame flag
    m_justFired = false;

    // ---- Timers ----
    if (m_fireTimer > 0.0f) m_fireTimer -= dt;
    if (m_muzzleFlashTimer > 0.0f) m_muzzleFlashTimer -= dt;
    if (m_hitMarkerTimer > 0.0f) m_hitMarkerTimer -= dt;

    // ---- Reloading ----
    if (m_reloading) {
        m_reloadTimer -= dt;
        if (m_reloadTimer <= 0.0f) {
            // Reload complete
            int needed = def.maxAmmo - m_currentAmmo;
            int available = (m_reserveAmmo >= needed) ? needed : m_reserveAmmo;
            m_currentAmmo += available;
            m_reserveAmmo -= available;
            m_reloading = false;
            m_reloadTimer = 0.0f;
        }
    }

    // ---- ADS (right mouse) ----
    m_adsActive = !editorWantsMouse && input.IsRightMouseDown();

    // ---- Weapon switching (1/2/3 keys) ----
    if (input.IsKeyPressed('1')) SwitchWeapon(WeaponType::Rifle);
    if (input.IsKeyPressed('2')) SwitchWeapon(WeaponType::Pistol);
    if (input.IsKeyPressed('3')) SwitchWeapon(WeaponType::Shotgun);

    // ---- Reload (R key) ----
    if (input.IsKeyPressed('R') && !m_reloading && m_currentAmmo < def.maxAmmo && m_reserveAmmo > 0) {
        Reload();
    }

    // ---- Firing ----
    if (!editorWantsMouse && !m_reloading) {
        bool wantFire = false;
        if (def.automatic) {
            wantFire = input.IsLeftMouseDown();
        } else {
            // Semi-auto: fire only on press (not hold)
            if (input.IsLeftMousePressed()) {
                wantFire = true;
                m_triggerHeld = true;
            }
            if (!input.IsLeftMouseDown()) {
                m_triggerHeld = false;
            }
        }

        if (wantFire && m_fireTimer <= 0.0f) {
            if (m_currentAmmo > 0) {
                Fire(camera, physics, aiSystem);
            } else if (m_reserveAmmo > 0) {
                // Auto-reload when empty
                Reload();
            }
        }
    }

    // ---- Recoil recovery ----
    UpdateRecoil(dt);

    // ---- Update viewmodel transforms ----
    UpdateViewmodel(dt, camera, character);
}

// ============================================================
// Fire
// ============================================================

void WeaponSystem::Fire(Camera& camera, PhysicsWorld* physics, AISystem* aiSystem) {
    const auto& def = GetCurrentDef();

    m_currentAmmo--;
    m_fireTimer = def.fireRate;
    // Use per-weapon flash duration if defined, otherwise global setting
    m_muzzleFlashTimer = def.flashLayerCount > 0 ? def.flashDuration : m_settings.muzzleFlashDuration;
    m_justFired = true;

    // Reset last hit
    m_lastHit = {};

    // Calculate fire direction from camera center
    XMFLOAT3 origin = camera.GetPosition();
    XMFLOAT3 forward = camera.GetForward();

    // Fire pellets (1 for rifle/pistol, many for shotgun)
    for (int p = 0; p < def.pelletsPerShot; p++) {
        // Apply spread
        float spreadDeg = def.spread;
        if (m_adsActive) spreadDeg *= def.adsSpreadMult;

        float spreadRad = spreadDeg * DEG_TO_RAD;
        float rndYaw   = ((rand() / (float)RAND_MAX) * 2.0f - 1.0f) * spreadRad;
        float rndPitch = ((rand() / (float)RAND_MAX) * 2.0f - 1.0f) * spreadRad;

        // Rotate forward by spread angles
        XMVECTOR fwd = XMLoadFloat3(&forward);
        XMVECTOR right = XMVector3Cross(XMVectorSet(0, 1, 0, 0), fwd);
        right = XMVector3Normalize(right);
        XMVECTOR up = XMVector3Cross(fwd, right);

        XMVECTOR spreadDir = fwd
            + right * rndYaw
            + up * rndPitch;
        spreadDir = XMVector3Normalize(spreadDir);

        XMFLOAT3 dir;
        XMStoreFloat3(&dir, spreadDir);

        auto hitResult = DoRaycast(origin, dir, def.range, physics, aiSystem);

        // Keep the closest hit for hit marker
        if (hitResult.hit) {
            if (!m_lastHit.hit || hitResult.distance < m_lastHit.distance) {
                m_lastHit = hitResult;
            }
        }
    }

    // Apply damage to hit agents
    if (m_lastHit.hit && m_lastHit.agentIndex >= 0 && aiSystem) {
        auto& agent = aiSystem->GetAgent(m_lastHit.agentIndex);
        bool killed = agent.TakeDamage(def.damage * def.pelletsPerShot);
        m_hitMarkerTimer = m_settings.hitMarkerDuration;
        if (killed) {
            LOG_INFO("Agent '%s' eliminated!", agent.name.c_str());
        }
    }

    // Apply recoil
    m_recoilPitchAccum += def.recoilPitch;
    float rndRecoilYaw = ((rand() / (float)RAND_MAX) * 2.0f - 1.0f) * def.recoilYaw;
    m_recoilYawAccum += rndRecoilYaw;
}

// ============================================================
// Raycast (against physics world + AI agents)
// ============================================================

WeaponHitResult WeaponSystem::DoRaycast(const XMFLOAT3& origin, const XMFLOAT3& direction,
                                         float range, PhysicsWorld* physics, AISystem* aiSystem) {
    WeaponHitResult result;
    float closestDist = range;

    // 1) Raycast physics world (static entities + ground)
    if (physics) {
        auto physHit = physics->Raycast(origin, direction, range);
        if (physHit.hit && physHit.depth < closestDist) {
            closestDist = physHit.depth;
            result.hit = true;
            result.distance = physHit.depth;
            result.hitNormal = physHit.normal;
            result.entityIndex = physHit.entityIndex;
            result.voxelCellIndex = physHit.voxelCellIndex;
            result.hitPosition = {
                origin.x + direction.x * physHit.depth,
                origin.y + direction.y * physHit.depth,
                origin.z + direction.z * physHit.depth
            };
        }
    }

    // 2) Raycast AI agents (manual AABB test)
    if (aiSystem) {
        // Compute inverse direction
        XMFLOAT3 invDir = {
            (fabsf(direction.x) > 1e-8f) ? 1.0f / direction.x : 1e8f,
            (fabsf(direction.y) > 1e-8f) ? 1.0f / direction.y : 1e8f,
            (fabsf(direction.z) > 1e-8f) ? 1.0f / direction.z : 1e8f
        };

        for (int i = 0; i < aiSystem->GetAgentCount(); i++) {
            const auto& agent = aiSystem->GetAgent(i);
            if (!agent.active || !agent.visible) continue;

            // Build agent AABB
            float halfScale = agent.settings.bodyScale * 0.5f;
            AABB agentBox = AABB::FromCenterHalf(
                { agent.position.x, agent.position.y + halfScale, agent.position.z },
                { halfScale, halfScale, halfScale }
            );

            // Ray-AABB intersection (slab method)
            float tMin = 0.0f;
            float tMax = closestDist;

            float t1 = (agentBox.min.x - origin.x) * invDir.x;
            float t2 = (agentBox.max.x - origin.x) * invDir.x;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            tMin = (t1 > tMin) ? t1 : tMin;
            tMax = (t2 < tMax) ? t2 : tMax;
            if (tMin > tMax) continue;

            t1 = (agentBox.min.y - origin.y) * invDir.y;
            t2 = (agentBox.max.y - origin.y) * invDir.y;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            tMin = (t1 > tMin) ? t1 : tMin;
            tMax = (t2 < tMax) ? t2 : tMax;
            if (tMin > tMax) continue;

            t1 = (agentBox.min.z - origin.z) * invDir.z;
            t2 = (agentBox.max.z - origin.z) * invDir.z;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            tMin = (t1 > tMin) ? t1 : tMin;
            tMax = (t2 < tMax) ? t2 : tMax;
            if (tMin > tMax) continue;

            if (tMin > 0.0f && tMin < closestDist) {
                closestDist = tMin;
                result.hit = true;
                result.distance = tMin;
                result.agentIndex = i;
                result.entityIndex = -1;
                result.hitPosition = {
                    origin.x + direction.x * tMin,
                    origin.y + direction.y * tMin,
                    origin.z + direction.z * tMin
                };

                // Compute hit normal
                XMFLOAT3 center = agentBox.Center();
                XMFLOAT3 half = agentBox.HalfExtents();
                float dx = (result.hitPosition.x - center.x) / half.x;
                float dy = (result.hitPosition.y - center.y) / half.y;
                float dz = (result.hitPosition.z - center.z) / half.z;
                float ax = fabsf(dx), ay = fabsf(dy), az = fabsf(dz);
                if (ax > ay && ax > az)
                    result.hitNormal = { dx > 0 ? 1.0f : -1.0f, 0, 0 };
                else if (ay > az)
                    result.hitNormal = { 0, dy > 0 ? 1.0f : -1.0f, 0 };
                else
                    result.hitNormal = { 0, 0, dz > 0 ? 1.0f : -1.0f };
            }
        }
    }

    return result;
}

// ============================================================
// Reload
// ============================================================

void WeaponSystem::Reload() {
    const auto& def = GetCurrentDef();
    if (m_currentAmmo >= def.maxAmmo || m_reserveAmmo <= 0) return;
    m_reloading = true;
    m_reloadTimer = def.reloadTime;
    LOG_INFO("Reloading %s...", WeaponTypeName(m_currentWeapon));
}

float WeaponSystem::GetReloadProgress() const {
    if (!m_reloading) return 1.0f;
    const auto& def = GetCurrentDef();
    return 1.0f - (m_reloadTimer / def.reloadTime);
}

// ============================================================
// Recoil
// ============================================================

void WeaponSystem::UpdateRecoil(float dt) {
    const auto& def = GetCurrentDef();
    float recovery = def.recoilRecovery * dt;

    // Recover pitch
    if (m_recoilPitchAccum > 0.0f) {
        m_recoilPitchAccum -= recovery;
        if (m_recoilPitchAccum < 0.0f) m_recoilPitchAccum = 0.0f;
    }

    // Recover yaw
    if (fabsf(m_recoilYawAccum) > 0.01f) {
        float sign = (m_recoilYawAccum > 0.0f) ? 1.0f : -1.0f;
        m_recoilYawAccum -= sign * recovery * 0.5f;
        if (fabsf(m_recoilYawAccum) < 0.01f) m_recoilYawAccum = 0.0f;
    } else {
        m_recoilYawAccum = 0.0f;
    }
}

// ============================================================
// Viewmodel Update
// ============================================================

void WeaponSystem::UpdateViewmodel(float dt, const Camera& camera, const Character& character) {
    m_viewmodelParts.clear();

    const auto& def = GetCurrentDef();
    const auto& s   = m_settings;

    // ---- Viewmodel sway (disabled) ----
    m_swayOffsetX = 0.0f;
    m_swayOffsetY = 0.0f;

    // ---- Base viewmodel position in camera space ----
    XMFLOAT3 camPos = camera.GetPosition();
    XMFLOAT3 camFwd = camera.GetForward();
    XMFLOAT3 camRight = camera.GetRight();
    XMFLOAT3 camUp = camera.GetUp();

    // Combine offsets
    float offX = s.viewmodelOffsetX + m_swayOffsetX;
    float offY = s.viewmodelOffsetY + m_swayOffsetY;
    float offZ = s.viewmodelOffsetZ;

    // Recoil kick (push gun back + up — subtle, matched to arm recoil)
    float recoilKick = m_recoilPitchAccum * 0.001f;
    offY += recoilKick * 0.3f;
    offZ -= recoilKick * 0.5f;

    // Reload animation: lower gun
    if (m_reloading) {
        float reloadProg = GetReloadProgress();
        // Dip down then back up
        float dip = sinf(reloadProg * PI) * 0.15f;
        offY -= dip;
    }

    // Compute world-space base position for viewmodel
    XMFLOAT3 vmBase = {
        camPos.x + camFwd.x * offZ + camRight.x * offX + camUp.x * offY,
        camPos.y + camFwd.y * offZ + camRight.y * offX + camUp.y * offY,
        camPos.z + camFwd.z * offZ + camRight.z * offX + camUp.z * offY
    };

    // Viewmodel rotation (follows camera yaw/pitch)
    float yawDeg = camera.GetYaw() * RAD_TO_DEG;
    float pitchDeg = camera.GetPitch() * RAD_TO_DEG;

    bool useGunModel = !def.gunModelName.empty();

    if (useGunModel) {
        // ---- Gun model mesh (single transform for the whole gun) ----
        // Position at vmBase with model-specific offsets
        XMFLOAT3 modelPos = {
            vmBase.x + camFwd.x * def.modelOffsetZ + camRight.x * def.modelOffsetX + camUp.x * def.modelOffsetY,
            vmBase.y + camFwd.y * def.modelOffsetZ + camRight.y * def.modelOffsetX + camUp.y * def.modelOffsetY,
            vmBase.z + camFwd.z * def.modelOffsetZ + camRight.z * def.modelOffsetX + camUp.z * def.modelOffsetY
        };

        m_viewmodelMesh.position = modelPos;
        m_viewmodelMesh.rotation = { pitchDeg + def.modelRotX, yawDeg + def.modelRotY, def.modelRotZ };
        m_viewmodelMesh.scale = { def.modelScale, def.modelScale, def.modelScale };
        m_viewmodelMesh.meshName = def.gunModelName;
        m_viewmodelMesh.textureName = def.gunTextureName;
        memcpy(m_viewmodelMesh.color, s.gunMetalColor, sizeof(float) * 4);
    } else {
        // Clear mesh info when using cubes
        m_viewmodelMesh.meshName.clear();
    }

    if (!useGunModel) {
        // ---- Cube-based gun parts (fallback when no model loaded) ----

        // Gun receiver (main body)
        {
            ViewmodelPart part;
            part.transform.position = vmBase;
            part.transform.rotation = { pitchDeg, yawDeg, 0.0f };
            part.transform.scale = { def.bodyWidth, def.bodyHeight, def.barrelLength * 0.5f };
            memcpy(part.color, s.gunMetalColor, sizeof(float) * 4);
            m_viewmodelParts.push_back(part);
        }

        // Barrel (forward extension)
        {
            float barrelOffset = def.barrelLength * 0.5f + def.barrelLength * 0.25f;
            XMFLOAT3 barrelPos = {
                vmBase.x + camFwd.x * barrelOffset,
                vmBase.y + camFwd.y * barrelOffset - def.bodyHeight * 0.15f,
                vmBase.z + camFwd.z * barrelOffset
            };

            ViewmodelPart part;
            part.transform.position = barrelPos;
            part.transform.rotation = { pitchDeg, yawDeg, 0.0f };
            part.transform.scale = { def.barrelWidth, def.barrelWidth, def.barrelLength * 0.5f };
            memcpy(part.color, s.gunMetalColor, sizeof(float) * 4);
            m_viewmodelParts.push_back(part);
        }

        // Stock (behind grip)
        if (def.stockLength > 0.01f) {
            float stockOffset = -(def.barrelLength * 0.25f + def.stockLength * 0.5f);
            XMFLOAT3 stockPos = {
                vmBase.x + camFwd.x * stockOffset,
                vmBase.y + camFwd.y * stockOffset,
                vmBase.z + camFwd.z * stockOffset
            };

            ViewmodelPart part;
            part.transform.position = stockPos;
            part.transform.rotation = { pitchDeg, yawDeg, 0.0f };
            part.transform.scale = { def.bodyWidth * 0.8f, def.bodyHeight * 0.9f, def.stockLength * 0.5f };
            memcpy(part.color, s.gunWoodColor, sizeof(float) * 4);
            m_viewmodelParts.push_back(part);
        }
    }

    // ---- Arms/Hands (always rendered) ----
    {

    // When using a gun model, offset hands to match the model position
    float handBaseX = 0.0f, handBaseY = 0.0f, handBaseZ = 0.0f;
    if (useGunModel) {
        handBaseX = def.modelOffsetX;
        handBaseY = def.modelOffsetY;
        handBaseZ = def.modelOffsetZ;
    }

    // ---- Right hand (grip position) — per-weapon ----
    {
        float hx = handBaseX + def.rightHandRight;
        float hy = handBaseY - def.rightHandDown;
        float hz = handBaseZ + def.rightHandFwd;
        XMFLOAT3 handPos = {
            vmBase.x + camFwd.x * hz + camRight.x * hx + camUp.x * hy,
            vmBase.y + camFwd.y * hz + camRight.y * hx + camUp.y * hy,
            vmBase.z + camFwd.z * hz + camRight.z * hx + camUp.z * hy
        };

        ViewmodelPart part;
        part.transform.position = handPos;
        part.transform.rotation = { pitchDeg, yawDeg, 0.0f };
        part.transform.scale = { def.rightHandScaleX, def.rightHandScaleY, def.rightHandScaleZ };
        memcpy(part.color, s.handColor, sizeof(float) * 4);
        m_viewmodelParts.push_back(part);
    }

    // ---- Right forearm — per-weapon ----
    {
        float ax = handBaseX + def.rightArmRight;
        float ay = handBaseY - def.rightArmDown;
        float az = handBaseZ + def.rightArmFwd;
        XMFLOAT3 armPos = {
            vmBase.x + camFwd.x * az + camRight.x * ax + camUp.x * ay,
            vmBase.y + camFwd.y * az + camRight.y * ax + camUp.y * ay,
            vmBase.z + camFwd.z * az + camRight.z * ax + camUp.z * ay
        };

        ViewmodelPart part;
        part.transform.position = armPos;
        part.transform.rotation = { pitchDeg + def.rightArmPitch, yawDeg, 0.0f };
        part.transform.scale = { def.rightArmScaleX, def.rightArmScaleY, def.rightArmScaleZ };
        memcpy(part.color, s.armColor, sizeof(float) * 4);
        m_viewmodelParts.push_back(part);
    }

    // ---- Left hand (forward grip / handguard) — per-weapon ----
    {
        float lhx = handBaseX + def.leftHandRight;
        float lhy = handBaseY - def.leftHandDown;
        float lhz = handBaseZ + def.leftHandFwd;
        XMFLOAT3 leftHandPos = {
            vmBase.x + camFwd.x * lhz + camRight.x * lhx + camUp.x * lhy,
            vmBase.y + camFwd.y * lhz + camRight.y * lhx + camUp.y * lhy,
            vmBase.z + camFwd.z * lhz + camRight.z * lhx + camUp.z * lhy
        };

        ViewmodelPart part;
        part.transform.position = leftHandPos;
        part.transform.rotation = { pitchDeg, yawDeg, 0.0f };
        part.transform.scale = { def.leftHandScaleX, def.leftHandScaleY, def.leftHandScaleZ };
        memcpy(part.color, s.handColor, sizeof(float) * 4);
        m_viewmodelParts.push_back(part);
    }

    // ---- Left forearm — per-weapon ----
    {
        float lax = handBaseX + def.leftArmRight;
        float lay = handBaseY - def.leftArmDown;
        float laz = handBaseZ + def.leftArmFwd;
        XMFLOAT3 leftArmPos = {
            vmBase.x + camFwd.x * laz + camRight.x * lax + camUp.x * lay,
            vmBase.y + camFwd.y * laz + camRight.y * lax + camUp.y * lay,
            vmBase.z + camFwd.z * laz + camRight.z * lax + camUp.z * lay
        };

        ViewmodelPart part;
        part.transform.position = leftArmPos;
        part.transform.rotation = { pitchDeg + def.leftArmPitch, yawDeg, 0.0f };
        part.transform.scale = { def.leftArmScaleX, def.leftArmScaleY, def.leftArmScaleZ };
        memcpy(part.color, s.armColor, sizeof(float) * 4);
        m_viewmodelParts.push_back(part);
    }

    } // end arms/hands block

    // ---- Muzzle flash FX (per-weapon layered flash) ----
    if (m_muzzleFlashTimer > 0.0f) {
        float baseDist = def.barrelLength + def.flashMuzzleOffset;
        // Muzzle tip position
        XMFLOAT3 muzzleTip = {
            vmBase.x + camFwd.x * baseDist,
            vmBase.y + camFwd.y * baseDist - def.bodyHeight * 0.15f,
            vmBase.z + camFwd.z * baseDist
        };

        float duration = def.flashDuration > 0.0f ? def.flashDuration : s.muzzleFlashDuration;
        float t = m_muzzleFlashTimer / duration; // 1.0 → 0.0

        if (def.flashLayerCount > 0) {
            // Per-weapon multi-layer flash
            for (int i = 0; i < def.flashLayerCount && i < WeaponDef::kMaxFlashLayers; ++i) {
                const auto& layer = def.flashLayers[i];
                float layerAlpha = t * layer.fadeSpeed;
                if (layerAlpha > 1.0f) layerAlpha = 1.0f;
                if (layerAlpha <= 0.0f) continue;

                float grow = 1.0f + (1.0f - t) * layer.growSpeed;
                float sx = layer.scaleX * grow;
                float sy = layer.scaleY * grow;
                float sz = layer.scaleZ * grow;

                // Position offset from muzzle tip
                XMFLOAT3 layerPos = {
                    muzzleTip.x + camFwd.x * layer.offsetFwd + camUp.x * layer.offsetUp + camRight.x * layer.offsetRight,
                    muzzleTip.y + camFwd.y * layer.offsetFwd + camUp.y * layer.offsetUp + camRight.y * layer.offsetRight,
                    muzzleTip.z + camFwd.z * layer.offsetFwd + camUp.z * layer.offsetUp + camRight.z * layer.offsetRight
                };

                ViewmodelPart part;
                part.transform.position = layerPos;
                part.transform.rotation = { pitchDeg, yawDeg, layer.rollDeg };
                part.transform.scale = { sx, sy, sz };
                part.color[0] = layer.r;
                part.color[1] = layer.g;
                part.color[2] = layer.b;
                part.color[3] = layerAlpha;
                m_viewmodelParts.push_back(part);
            }
        } else {
            // Fallback: single cube flash (legacy)
            float flashSize = s.muzzleFlashScale * (0.5f + t * 0.5f);
            ViewmodelPart part;
            part.transform.position = muzzleTip;
            part.transform.rotation = { pitchDeg, yawDeg, 45.0f };
            part.transform.scale = { flashSize, flashSize, flashSize * 0.5f };
            part.color[0] = s.muzzleFlashColor[0];
            part.color[1] = s.muzzleFlashColor[1];
            part.color[2] = s.muzzleFlashColor[2];
            part.color[3] = t;
            m_viewmodelParts.push_back(part);
        }
    }
}

// ============================================================
// Debug Drawing
// ============================================================

void WeaponSystem::DebugDraw(DebugRenderer& debug) const {
    if (!showDebug) return;

    // Draw last hit position
    if (m_lastHit.hit) {
        debug.DrawSphere(m_lastHit.hitPosition, 0.1f, { 1.0f, 0.0f, 0.0f, 0.8f }, 8);

        // Draw hit normal
        XMFLOAT3 normalEnd = {
            m_lastHit.hitPosition.x + m_lastHit.hitNormal.x * 0.5f,
            m_lastHit.hitPosition.y + m_lastHit.hitNormal.y * 0.5f,
            m_lastHit.hitPosition.z + m_lastHit.hitNormal.z * 0.5f
        };
        debug.DrawLine(m_lastHit.hitPosition, normalEnd, { 0.0f, 1.0f, 0.0f, 0.8f });
    }
}

} // namespace WT
