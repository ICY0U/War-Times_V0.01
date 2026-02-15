#pragma once

#include <DirectXMath.h>
#include <vector>
#include <cstdint>
#include <string>
#include "Core/Character.h"

namespace WT {

using namespace DirectX;

// Forward declarations
class Input;
class Camera;
class PhysicsWorld;
class DebugRenderer;
class AISystem;

// ============================================================
// Weapon Definitions
// ============================================================

enum class WeaponType : uint8_t {
    Rifle = 0,
    Pistol,
    Shotgun,
    Count
};

inline const char* WeaponTypeName(WeaponType t) {
    switch (t) {
        case WeaponType::Rifle:   return "Rifle";
        case WeaponType::Pistol:  return "Pistol";
        case WeaponType::Shotgun: return "Shotgun";
        default:                  return "Unknown";
    }
}

// ---- Per-weapon stats ----
struct WeaponDef {
    WeaponType type        = WeaponType::Rifle;
    float fireRate         = 0.12f;   // Seconds between shots (auto)
    float damage           = 25.0f;
    float range            = 200.0f;
    int   maxAmmo          = 30;      // Magazine size
    int   reserveAmmo      = 90;      // Total reserve
    float reloadTime       = 2.0f;    // Seconds to reload
    float recoilPitch      = 1.5f;    // Degrees kicked up per shot
    float recoilYaw        = 0.3f;    // Random horizontal recoil
    float recoilRecovery   = 6.0f;    // Degrees/sec recovery
    float spread           = 0.5f;    // Base spread in degrees
    float adsSpreadMult    = 0.3f;    // Spread multiplier when ADS
    int   pelletsPerShot   = 1;       // >1 for shotgun
    bool  automatic        = true;    // Hold trigger to fire

    // Viewmodel visual
    float barrelLength     = 0.6f;    // How far the barrel extends forward
    float barrelWidth      = 0.06f;   // Barrel thickness
    float stockLength      = 0.25f;   // Stock behind grip
    float bodyWidth        = 0.1f;    // Gun body width
    float bodyHeight       = 0.12f;   // Gun body height

    // Gun model (loaded mesh). If empty, uses cube-based viewmodel.
    std::string gunModelName;
    std::string gunTextureName;  // Texture name from ResourceManager (e.g. "Gun/Palette")
    float modelScale       = 0.5f;    // Scale applied to the model
    float modelOffsetX     = 0.0f;    // Fine-tune model position offset
    float modelOffsetY     = 0.0f;
    float modelOffsetZ     = 0.0f;
    float modelRotX        = 0.0f;    // Extra rotation in degrees
    float modelRotY        = 0.0f;
    float modelRotZ        = 0.0f;

    // Per-weapon arm/hand positioning (relative to vmBase)
    // Right hand (grip)
    float rightHandFwd     = -0.02f;  // Forward offset from vmBase
    float rightHandRight   =  0.01f;  // Rightward offset
    float rightHandDown    =  0.06f;  // How far below gun body center
    float rightHandScaleX  =  0.05f;
    float rightHandScaleY  =  0.06f;
    float rightHandScaleZ  =  0.05f;

    // Right forearm
    float rightArmFwd      = -0.08f;
    float rightArmRight    =  0.02f;
    float rightArmDown     =  0.04f;
    float rightArmPitch    =  10.0f;  // Extra pitch tilt degrees
    float rightArmScaleX   =  0.055f;
    float rightArmScaleY   =  0.06f;
    float rightArmScaleZ   =  0.12f;

    // Left hand (foregrip/handguard)
    float leftHandFwd      =  0.21f;  // Forward offset (fraction of barrelLength by default)
    float leftHandRight    = -0.01f;
    float leftHandDown     =  0.05f;
    float leftHandScaleX   =  0.05f;
    float leftHandScaleY   =  0.055f;
    float leftHandScaleZ   =  0.05f;

    // Left forearm
    float leftArmFwd       =  0.10f;
    float leftArmRight     = -0.04f;
    float leftArmDown      =  0.04f;
    float leftArmPitch     = -5.0f;
    float leftArmScaleX    =  0.055f;
    float leftArmScaleY    =  0.06f;
    float leftArmScaleZ    =  0.12f;

    // Gun grip sockets (positions in gun model local space)
    // These define where the hands should be placed on the gun mesh
    XMFLOAT3 rightGripSocket   = { 0.0f, -0.04f, -0.04f };  // Pistol grip area
    XMFLOAT3 rightGripRotation = { 0.0f, 0.0f, 0.0f };      // Extra hand rotation (degrees)
    XMFLOAT3 leftGripSocket    = { 0.0f, -0.02f,  0.18f };  // Foregrip/handguard area
    XMFLOAT3 leftGripRotation  = { 0.0f, 0.0f, 0.0f };      // Extra hand rotation (degrees)
    XMFLOAT3 elbowPoleOffset   = { 0.0f, -0.3f, 0.0f };     // Elbow hint (downward by default)

    // ---- Per-weapon muzzle flash FX ----
    struct MuzzleFlashLayer {
        float scaleX    = 0.04f;   // Base size X
        float scaleY    = 0.04f;   // Base size Y
        float scaleZ    = 0.02f;   // Base size Z (depth)
        float offsetFwd = 0.0f;    // Forward offset from muzzle tip
        float offsetUp  = 0.0f;    // Upward offset
        float offsetRight = 0.0f;  // Right offset
        float rollDeg   = 0.0f;    // Extra roll rotation (degrees)
        float r = 1.0f, g = 0.9f, b = 0.4f; // Color
        float fadeSpeed = 1.0f;    // How quickly this layer fades (multiplier)
        float growSpeed = 0.5f;    // How much this layer grows as it fades (0=none, 1=double)
    };
    static constexpr int kMaxFlashLayers = 6;
    MuzzleFlashLayer flashLayers[kMaxFlashLayers];
    int flashLayerCount       = 0;      // Active layers (0 = use default single flash)
    float flashDuration       = 0.05f;  // Per-weapon override
    float flashMuzzleOffset   = 0.0f;   // Extra forward offset for muzzle position
};

// ============================================================
// Hit Result
// ============================================================

struct WeaponHitResult {
    bool     hit          = false;
    XMFLOAT3 hitPosition  = { 0, 0, 0 };
    XMFLOAT3 hitNormal    = { 0, 1, 0 };
    int      entityIndex  = -1;       // Scene entity hit (-1 = none/ground)
    int      agentIndex   = -1;       // AI agent hit (-1 = none)
    float    distance     = 0.0f;
};

// ============================================================
// Weapon Settings (editor-tunable)
// ============================================================

struct WeaponSettings {
    // Viewmodel placement (tuned for 79 FOV)
    float viewmodelFOVScale = 0.85f;   // Narrower than scene FOV for natural look
    float viewmodelOffsetX  = 0.22f;   // Right of center
    float viewmodelOffsetY  = -0.20f;  // Below eye level
    float viewmodelOffsetZ  = 0.30f;   // Forward

    // Viewmodel sway
    float swayAmount        = 0.0005f;
    float swaySpeed         = 2.0f;
    float swayMaxAngle      = 0.3f;   // Max sway rotation degrees

    // Muzzle flash
    float muzzleFlashDuration = 0.05f;
    float muzzleFlashScale    = 0.08f;
    float muzzleFlashColor[4] = { 1.0f, 0.85f, 0.3f, 1.0f };

    // Hit marker
    float hitMarkerDuration = 0.15f;
    float hitMarkerSize     = 8.0f;
    float hitMarkerColor[4] = { 1.0f, 0.2f, 0.2f, 1.0f };

    // Crosshair
    float crosshairSize     = 10.0f;
    float crosshairGap      = 4.0f;
    float crosshairThickness = 2.0f;
    float crosshairColor[4] = { 1.0f, 1.0f, 1.0f, 0.8f };
    bool  crosshairDot      = true;

    // Colors (viewmodel parts)
    float gunMetalColor[4]  = { 0.25f, 0.25f, 0.28f, 1.0f };  // Dark steel
    float gunWoodColor[4]   = { 0.45f, 0.30f, 0.15f, 1.0f };  // Wood stock
    float armColor[4]       = { 0.25f, 0.35f, 0.20f, 1.0f };  // OD green (match character)
    float handColor[4]      = { 0.85f, 0.70f, 0.55f, 1.0f };  // Skin
};

// ============================================================
// Weapon System
// ============================================================

class WeaponSystem {
public:
    void Init();
    void Shutdown();

    // ---- Update (called each frame in character mode) ----
    void Update(float dt, Input& input, Camera& camera,
                const Character& character, bool editorWantsMouse,
                PhysicsWorld* physics, AISystem* aiSystem);

    // ---- Viewmodel parts for rendering ----
    // Cube-based parts (hands, arms, muzzle flash)
    struct ViewmodelPart {
        Character::BodyPart transform;
        float color[4];
    };
    const std::vector<ViewmodelPart>& GetViewmodelParts() const { return m_viewmodelParts; }

    // Mesh-based gun model (single transform for the whole gun mesh)
    struct ViewmodelMesh {
        XMFLOAT3 position;
        XMFLOAT3 rotation;  // Degrees
        XMFLOAT3 scale;
        std::string meshName;
        std::string textureName;
        float color[4];
    };
    const ViewmodelMesh& GetViewmodelMesh() const { return m_viewmodelMesh; }
    bool HasGunModel() const { return !GetCurrentDef().gunModelName.empty(); }

    // Get the gun's world matrix (computed from ViewmodelMesh data)
    XMMATRIX GetGunWorldMatrix() const {
        XMMATRIX S = XMMatrixScaling(m_viewmodelMesh.scale.x, m_viewmodelMesh.scale.y, m_viewmodelMesh.scale.z);
        XMMATRIX R = XMMatrixRotationRollPitchYaw(
            XMConvertToRadians(m_viewmodelMesh.rotation.x),
            XMConvertToRadians(m_viewmodelMesh.rotation.y),
            XMConvertToRadians(m_viewmodelMesh.rotation.z));
        XMMATRIX T = XMMatrixTranslation(m_viewmodelMesh.position.x, m_viewmodelMesh.position.y, m_viewmodelMesh.position.z);
        return S * R * T;
    }

    // ---- State queries ----
    WeaponType GetCurrentWeapon() const    { return m_currentWeapon; }
    int   GetCurrentAmmo() const           { return m_currentAmmo; }
    int   GetReserveAmmo() const           { return m_reserveAmmo; }
    int   GetMaxAmmo() const               { return GetCurrentDef().maxAmmo; }
    bool  IsReloading() const              { return m_reloading; }
    float GetReloadProgress() const;
    bool  IsFiring() const                 { return m_fireTimer < 0.06f && m_fireTimer > 0.0f; }
    bool  JustFired() const                 { return m_justFired; }
    bool  IsMuzzleFlashActive() const      { return m_muzzleFlashTimer > 0.0f; }
    bool  IsHitMarkerActive() const        { return m_hitMarkerTimer > 0.0f; }
    bool  IsADS() const                    { return m_adsActive; }

    // ---- Recoil offset (applied to camera) ----
    float GetRecoilPitch() const { return m_recoilPitchAccum; }
    float GetRecoilYaw() const   { return m_recoilYawAccum; }

    // ---- Weapon definitions (mutable for editor) ----
    WeaponDef& GetWeaponDef(WeaponType type) { return m_weaponDefs[static_cast<int>(type)]; }
    const WeaponDef& GetCurrentDef() const   { return m_weaponDefs[static_cast<int>(m_currentWeapon)]; }

    // ---- Switch weapon ----
    void SwitchWeapon(WeaponType type);

    // ---- Settings ----
    WeaponSettings& GetSettings() { return m_settings; }
    const WeaponSettings& GetSettings() const { return m_settings; }

    // ---- Last hit result ----
    const WeaponHitResult& GetLastHit() const { return m_lastHit; }

    // ---- Debug drawing ----
    void DebugDraw(DebugRenderer& debug) const;
    bool showDebug = false;

private:
    void Fire(Camera& camera, PhysicsWorld* physics, AISystem* aiSystem);
    void Reload();
    void UpdateRecoil(float dt);
    void UpdateViewmodel(float dt, const Camera& camera, const Character& character);
    WeaponHitResult DoRaycast(const XMFLOAT3& origin, const XMFLOAT3& direction,
                               float range, PhysicsWorld* physics, AISystem* aiSystem);

    // Weapon definitions
    WeaponDef m_weaponDefs[static_cast<int>(WeaponType::Count)];
    WeaponType m_currentWeapon = WeaponType::Rifle;

    // Ammo state
    int   m_currentAmmo  = 30;
    int   m_reserveAmmo  = 90;

    // Firing state
    float m_fireTimer    = 0.0f;    // Cooldown between shots
    float m_muzzleFlashTimer = 0.0f;
    bool  m_triggerHeld  = false;   // For semi-auto: prevent re-fire on hold

    // Reloading
    bool  m_reloading    = false;
    float m_reloadTimer  = 0.0f;

    // Recoil
    float m_recoilPitchAccum = 0.0f;
    float m_recoilYawAccum   = 0.0f;

    // ADS
    bool  m_adsActive    = false;

    // Viewmodel sway
    float m_swayTime     = 0.0f;
    float m_swayOffsetX  = 0.0f;
    float m_swayOffsetY  = 0.0f;

    // Viewmodel parts (rebuilt each frame)
    std::vector<ViewmodelPart> m_viewmodelParts;
    ViewmodelMesh m_viewmodelMesh;

    // Last hit result
    WeaponHitResult m_lastHit;
    float m_hitMarkerTimer = 0.0f;
    bool  m_justFired      = false;  // Set true on frame of fire, cleared next frame

    // Settings
    WeaponSettings m_settings;
};

} // namespace WT
