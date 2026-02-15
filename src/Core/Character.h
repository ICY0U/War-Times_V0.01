#pragma once

#include <DirectXMath.h>
#include "AnimStateMachine.h"

namespace WT {

using namespace DirectX;

class Input;
class Camera;
class PhysicsWorld;

// ---- Character Movement Settings ----
struct CharacterSettings {
    float moveSpeed     = 5.0f;
    float sprintMult    = 2.0f;
    float jumpForce     = 6.0f;
    float gravity       = 18.0f;
    float groundY       = 0.0f;     // Ground plane Y level
    float eyeHeight     = 1.6f;     // Camera height above feet
    float bodyHeight    = 1.8f;     // Total character height
    float bodyWidth     = 0.5f;     // Character collision width
    float bodyDepth     = 0.5f;     // Character collision depth

    // Crouch
    float crouchEyeHeight  = 0.9f;  // Eye height when crouched
    float crouchSpeedMult  = 0.5f;  // Speed multiplier when crouched
    float crouchTransSpeed = 8.0f;  // How fast to lerp crouch transition

    // Camera tilt
    bool  cameraTiltEnabled = true;
    float cameraTiltAmount  = 0.4f;  // Max tilt angle in degrees
    float cameraTiltSpeed   = 6.0f;  // Lerp speed

    // Head bob
    bool  headBobEnabled = true;
    float headBobSpeed   = 10.0f;   // Oscillation speed
    float headBobAmount  = 0.04f;   // Vertical bob amplitude
    float headBobSway    = 0.02f;   // Horizontal sway amplitude

    // Physics / Collision
    bool  collisionEnabled = true;   // Collide with scene entities

    // Body colors (voxel figure)
    float headColor[4]   = { 0.85f, 0.70f, 0.55f, 1.0f };  // Skin
    float torsoColor[4]  = { 0.25f, 0.35f, 0.20f, 1.0f };  // OD green
    float legsColor[4]   = { 0.30f, 0.25f, 0.18f, 1.0f };  // Brown
    float armsColor[4]   = { 0.25f, 0.35f, 0.20f, 1.0f };  // Match torso
};

// ---- Animation State ----
enum class CharAnimState : uint8_t {
    Idle = 0,
    Walking,
    Sprinting,
    Crouching,
    Jumping,
    Falling
};

// ---- FPS Character Controller ----
class Character {
public:
    void Init(const XMFLOAT3& startPos, float startYaw = 0.0f);
    void Update(float dt, Input& input, Camera& camera, const CharacterSettings& settings,
                bool editorWantsMouse, bool editorWantsKeyboard,
                PhysicsWorld* physics = nullptr);

    // Position & state
    XMFLOAT3 GetPosition() const      { return m_position; }       // Feet position
    XMFLOAT3 GetEyePosition() const;                               // Camera position
    XMFLOAT3 GetVelocity() const      { return m_velocity; }
    float    GetYaw() const           { return m_yaw; }
    bool     IsGrounded() const       { return m_grounded; }
    bool     IsMoving() const         { return m_moving; }
    bool     IsCrouching() const      { return m_crouching; }
    bool     IsSprinting() const      { return m_sprinting; }
    float    GetCameraTilt() const    { return m_cameraTilt; }  // Roll in degrees
    CharAnimState GetAnimState() const { return m_animState; }
    float    GetMoveSpeed() const     { return m_currentSpeed; }

    // Animation state machine access
    const AnimStateMachine& GetAnimStateMachine() const { return m_animSM; }
    AnimStateMachine& GetAnimStateMachine() { return m_animSM; }

    // Head bob offset (applied to camera)
    XMFLOAT3 GetHeadBobOffset() const { return m_headBobOffset; }

    // Body part transforms for rendering
    struct BodyPart {
        XMFLOAT3 position;
        XMFLOAT3 rotation;   // Euler degrees
        XMFLOAT3 scale;
    };

    BodyPart GetHeadTransform() const;
    BodyPart GetTorsoTransform() const;
    BodyPart GetLeftArmTransform() const;
    BodyPart GetRightArmTransform() const;
    BodyPart GetLeftLegTransform() const;
    BodyPart GetRightLegTransform() const;

    void SetPosition(const XMFLOAT3& pos) { m_position = pos; }

private:
    void UpdateMovement(float dt, Input& input, Camera& camera, const CharacterSettings& settings,
                        bool editorWantsKeyboard);
    void UpdatePhysics(float dt, const CharacterSettings& settings, PhysicsWorld* physics);
    void UpdateAnimation(float dt, const CharacterSettings& settings);
    void UpdateHeadBob(float dt, const CharacterSettings& settings);
    void UpdateCrouch(float dt, Input& input, const CharacterSettings& settings);
    void UpdateCameraTilt(float dt, Input& input, const CharacterSettings& settings);
    void SetupAnimStateMachine();

    // Animation state machine
    AnimStateMachine m_animSM;
    bool m_animSMInitialized = false;

    // Position & physics
    XMFLOAT3 m_position   = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 m_velocity   = { 0.0f, 0.0f, 0.0f };
    float    m_yaw         = 0.0f;
    bool     m_grounded    = true;
    bool     m_moving      = false;
    bool     m_crouching   = false;
    bool     m_sprinting   = false;
    float    m_currentSpeed = 0.0f;
    float    m_cameraTilt  = 0.0f;      // Current roll angle in degrees
    float    m_strafeDir   = 0.0f;      // -1 left, 0 none, +1 right

    // Animation
    CharAnimState m_animState = CharAnimState::Idle;
    float    m_animTimer   = 0.0f;       // General anim timer
    float    m_limbSwing   = 0.0f;       // Arm/leg swing angle
    float    m_walkCycle   = 0.0f;       // 0-2PI walk cycle phase

    // Head bob
    float    m_headBobTimer = 0.0f;
    XMFLOAT3 m_headBobOffset = { 0.0f, 0.0f, 0.0f };

    // Eye height (for smooth transitions)
    float    m_eyeHeight   = 1.6f;
};

} // namespace WT
