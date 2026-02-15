#include "Character.h"
#include "Input.h"
#include "Graphics/Camera.h"
#include "Util/MathHelpers.h"
#include "Physics/PhysicsWorld.h"
#include <cmath>

namespace WT {

void Character::Init(const XMFLOAT3& startPos, float startYaw) {
    m_position  = startPos;
    m_velocity  = { 0.0f, 0.0f, 0.0f };
    m_yaw       = startYaw;
    m_grounded  = true;
    m_moving    = false;
    m_crouching = false;
    m_sprinting = false;
    m_animState = CharAnimState::Idle;
    m_animTimer = 0.0f;
    m_walkCycle = 0.0f;
    m_limbSwing = 0.0f;
    m_headBobTimer  = 0.0f;
    m_headBobOffset = { 0.0f, 0.0f, 0.0f };
    m_currentSpeed  = 0.0f;
    m_eyeHeight = 1.6f;
    m_cameraTilt = 0.0f;
    m_strafeDir  = 0.0f;

    // Initialize animation state machine
    m_animSMInitialized = false;
}

XMFLOAT3 Character::GetEyePosition() const {
    return {
        m_position.x + m_headBobOffset.x,
        m_position.y + m_eyeHeight + m_headBobOffset.y,
        m_position.z + m_headBobOffset.z
    };
}

void Character::Update(float dt, Input& input, Camera& camera, const CharacterSettings& settings,
                        bool editorWantsMouse, bool editorWantsKeyboard,
                        PhysicsWorld* physics) {
    (void)editorWantsMouse;
    // Sync yaw from camera (FPS: camera controls character facing)
    m_yaw = camera.GetYaw();
    m_eyeHeight = settings.eyeHeight;

    // Initialize animation state machine on first update
    if (!m_animSMInitialized) {
        SetupAnimStateMachine();
        m_animSMInitialized = true;
    }

    UpdateMovement(dt, input, camera, settings, editorWantsKeyboard);
    UpdatePhysics(dt, settings, physics);
    UpdateCrouch(dt, input, settings);
    UpdateAnimation(dt, settings);
    UpdateHeadBob(dt, settings);
    UpdateCameraTilt(dt, input, settings);

    // Place camera at eye position
    XMFLOAT3 eyePos = GetEyePosition();
    camera.SetPosition(eyePos);

    // Apply camera roll (tilt)
    camera.SetRoll(m_cameraTilt * (PI / 180.0f));
}

void Character::UpdateMovement(float dt, Input& input, Camera& camera,
                                const CharacterSettings& settings, bool editorWantsKeyboard) {
    (void)dt;
    (void)camera;
    if (editorWantsKeyboard) {
        m_moving = false;
        m_currentSpeed = 0.0f;
        return;
    }

    // Calculate movement direction from WASD (on XZ plane only, using camera yaw)
    float moveX = 0.0f;
    float moveZ = 0.0f;

    float sinYaw = sinf(m_yaw);
    float cosYaw = cosf(m_yaw);

    // Forward/back (camera's XZ forward, ignoring pitch)
    if (input.IsKeyDown('W')) { moveX += sinYaw; moveZ += cosYaw; }
    if (input.IsKeyDown('S')) { moveX -= sinYaw; moveZ -= cosYaw; }

    // Strafe (camera's right on XZ plane)
    if (input.IsKeyDown('A')) { moveX -= cosYaw; moveZ += sinYaw; }
    if (input.IsKeyDown('D')) { moveX += cosYaw; moveZ -= sinYaw; }

    // Normalize if diagonal
    float lenSq = moveX * moveX + moveZ * moveZ;
    m_moving = lenSq > 0.001f;

    // Track strafe direction for camera tilt
    bool strafeLeft  = input.IsKeyDown('A') && !input.IsKeyDown('D');
    bool strafeRight = input.IsKeyDown('D') && !input.IsKeyDown('A');
    m_strafeDir = strafeLeft ? -1.0f : (strafeRight ? 1.0f : 0.0f);

    float speed = settings.moveSpeed;
    bool sprinting = input.IsKeyDown(VK_SHIFT) && m_moving && !m_crouching;
    m_sprinting = sprinting;
    if (sprinting) speed *= settings.sprintMult;
    if (m_crouching) speed *= settings.crouchSpeedMult;

    if (m_moving) {
        float invLen = 1.0f / sqrtf(lenSq);
        moveX *= invLen;
        moveZ *= invLen;

        m_velocity.x = moveX * speed;
        m_velocity.z = moveZ * speed;
        m_currentSpeed = speed;
    } else {
        // Decelerate quickly
        m_velocity.x *= 0.85f;
        m_velocity.z *= 0.85f;
        if (fabsf(m_velocity.x) < 0.01f) m_velocity.x = 0.0f;
        if (fabsf(m_velocity.z) < 0.01f) m_velocity.z = 0.0f;
        m_currentSpeed = sqrtf(m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z);
    }

    // Jump
    if (input.IsKeyPressed(VK_SPACE) && m_grounded) {
        m_velocity.y = settings.jumpForce;
        m_grounded = false;
    }

    // Update anim state
    if (!m_grounded) {
        m_animState = (m_velocity.y > 0.0f) ? CharAnimState::Jumping : CharAnimState::Falling;
    } else if (m_crouching) {
        m_animState = CharAnimState::Crouching;
    } else if (sprinting) {
        m_animState = CharAnimState::Sprinting;
    } else if (m_moving) {
        m_animState = CharAnimState::Walking;
    } else {
        m_animState = CharAnimState::Idle;
    }
}

void Character::UpdatePhysics(float dt, const CharacterSettings& settings, PhysicsWorld* physics) {
    // Apply gravity
    if (!m_grounded) {
        m_velocity.y -= settings.gravity * dt;
    }

    // Integrate position
    m_position.x += m_velocity.x * dt;
    m_position.y += m_velocity.y * dt;
    m_position.z += m_velocity.z * dt;

    // Reset grounded state — will be re-set by collision checks below
    m_grounded = false;

    // ---- Collision with scene entities via PhysicsWorld ----
    if (physics && settings.collisionEnabled) {
        float charHeight = m_crouching ? (settings.bodyHeight * 0.6f) : settings.bodyHeight;

        // Build character AABB (bottom-center at feet position)
        AABB charBox = AABB::FromBottom(m_position, settings.bodyWidth, charHeight, settings.bodyDepth);

        // Iterative collision resolution (up to 4 passes)
        for (int iter = 0; iter < 4; iter++) {
            CollisionHit hit = physics->TestAABB(charBox, -1);
            if (!hit.hit) break;

            // Push out along collision normal
            float pushDist = hit.depth + 0.001f;
            m_position.x += hit.normal.x * pushDist;
            m_position.y += hit.normal.y * pushDist;
            m_position.z += hit.normal.z * pushDist;

            // Update AABB after push-out
            charBox = AABB::FromBottom(m_position, settings.bodyWidth, charHeight, settings.bodyDepth);

            // Cancel velocity along collision normal
            float vDotN = m_velocity.x * hit.normal.x +
                          m_velocity.y * hit.normal.y +
                          m_velocity.z * hit.normal.z;
            if (vDotN < 0.0f) {
                m_velocity.x -= hit.normal.x * vDotN;
                m_velocity.y -= hit.normal.y * vDotN;
                m_velocity.z -= hit.normal.z * vDotN;
            }

            // Landing on top of entity
            if (hit.normal.y > 0.5f) {
                m_grounded = true;
            }
        }
    }

    // Ground collision (simple flat plane — fallback / always active)
    if (m_position.y <= settings.groundY) {
        m_position.y = settings.groundY;
        m_velocity.y = 0.0f;
        m_grounded = true;
    }
}

void Character::UpdateAnimation(float dt, const CharacterSettings& settings) {
    (void)settings;
    m_animTimer += dt;

    // Update animation state machine
    m_animSM.Update(dt);

    // Read output from state machine
    const AnimOutput& anim = m_animSM.GetOutput();
    m_walkCycle = anim.walkCycle;
    m_limbSwing = anim.limbSwing;
}

void Character::UpdateHeadBob(float dt, const CharacterSettings& settings) {
    (void)dt;
    if (!settings.headBobEnabled || !m_grounded) {
        // Smoothly return to zero
        m_headBobOffset.x *= 0.9f;
        m_headBobOffset.y *= 0.9f;
        m_headBobOffset.z *= 0.9f;
        return;
    }

    // Use AnimStateMachine output for head bob
    const AnimOutput& anim = m_animSM.GetOutput();
    m_headBobOffset.y = anim.headBobY;
    m_headBobOffset.x = anim.headBobX;
    m_headBobOffset.z = 0.0f;
}

// ============================================================
// Crouch — smooth eye height transition
// ============================================================

void Character::UpdateCrouch(float dt, Input& input, const CharacterSettings& settings) {
    // Toggle crouch with Ctrl (hold)
    bool wantsCrouch = input.IsKeyDown(VK_CONTROL) && m_grounded;
    m_crouching = wantsCrouch;

    // Smoothly lerp eye height between standing and crouching
    float targetEye = m_crouching ? settings.crouchEyeHeight : settings.eyeHeight;
    float lerpRate = settings.crouchTransSpeed * dt;
    m_eyeHeight += (targetEye - m_eyeHeight) * (lerpRate > 1.0f ? 1.0f : lerpRate);
}

// ============================================================
// Camera Tilt — roll toward strafe direction
// ============================================================

void Character::UpdateCameraTilt(float dt, Input& input, const CharacterSettings& settings) {
    (void)input;
    if (!settings.cameraTiltEnabled) {
        m_cameraTilt *= 0.9f;
        return;
    }

    // Target tilt based on strafe direction
    float targetTilt = m_strafeDir * settings.cameraTiltAmount;  // Lean into strafe

    // Smooth lerp to target
    float lerpRate = settings.cameraTiltSpeed * dt;
    if (lerpRate > 1.0f) lerpRate = 1.0f;
    m_cameraTilt += (targetTilt - m_cameraTilt) * lerpRate;

    // Snap to zero if very small
    if (fabsf(m_cameraTilt) < 0.01f && fabsf(targetTilt) < 0.01f) {
        m_cameraTilt = 0.0f;
    }
}

// ============================================================
// Body Part Transforms (voxel figure made of cubes)
// Positions relative to feet origin (m_position)
// ============================================================

Character::BodyPart Character::GetHeadTransform() const {
    BodyPart bp;
    float yawDeg = m_yaw * (180.0f / PI);
    float crouchOffset = m_crouching ? -0.5f : 0.0f;
    bp.position = { m_position.x, m_position.y + 1.55f + crouchOffset, m_position.z };
    bp.rotation = { 0.0f, yawDeg, 0.0f };
    bp.scale    = { 0.3f, 0.3f, 0.3f };
    return bp;
}

Character::BodyPart Character::GetTorsoTransform() const {
    BodyPart bp;
    float yawDeg = m_yaw * (180.0f / PI);
    float crouchOffset = m_crouching ? -0.4f : 0.0f;
    float crouchTilt   = m_crouching ? 15.0f : 0.0f;  // Lean forward
    bp.position = { m_position.x, m_position.y + 1.1f + crouchOffset, m_position.z };
    bp.rotation = { crouchTilt, yawDeg, 0.0f };
    bp.scale    = { 0.35f, m_crouching ? 0.3f : 0.4f, 0.2f };
    return bp;
}

Character::BodyPart Character::GetLeftArmTransform() const {
    BodyPart bp;
    float yawDeg = m_yaw * (180.0f / PI);
    // Arm swings opposite to left leg
    float swing = -m_limbSwing;
    float crouchOffset = m_crouching ? -0.4f : 0.0f;

    // Offset arm to the left side of torso
    float sx = sinf(m_yaw);
    float cx = cosf(m_yaw);
    float sideOffset = 0.3f;

    bp.position = {
        m_position.x - cx * sideOffset,
        m_position.y + 1.1f + crouchOffset,
        m_position.z + sx * sideOffset
    };
    bp.rotation = { swing, yawDeg, 0.0f };
    bp.scale    = { 0.12f, 0.35f, 0.12f };
    return bp;
}

Character::BodyPart Character::GetRightArmTransform() const {
    BodyPart bp;
    float yawDeg = m_yaw * (180.0f / PI);
    float swing = m_limbSwing;
    float crouchOffset = m_crouching ? -0.4f : 0.0f;

    float sx = sinf(m_yaw);
    float cx = cosf(m_yaw);
    float sideOffset = 0.3f;

    bp.position = {
        m_position.x + cx * sideOffset,
        m_position.y + 1.1f + crouchOffset,
        m_position.z - sx * sideOffset
    };
    bp.rotation = { swing, yawDeg, 0.0f };
    bp.scale    = { 0.12f, 0.35f, 0.12f };
    return bp;
}

Character::BodyPart Character::GetLeftLegTransform() const {
    BodyPart bp;
    float yawDeg = m_yaw * (180.0f / PI);
    float swing = m_limbSwing;
    float crouchLegBend = m_crouching ? -25.0f : 0.0f;  // Bend knees

    float sx = sinf(m_yaw);
    float cx = cosf(m_yaw);
    float sideOffset = 0.12f;

    bp.position = {
        m_position.x - cx * sideOffset,
        m_position.y + 0.35f,
        m_position.z + sx * sideOffset
    };
    bp.rotation = { swing + crouchLegBend, yawDeg, 0.0f };
    bp.scale    = { 0.14f, 0.35f, 0.14f };
    return bp;
}

Character::BodyPart Character::GetRightLegTransform() const {
    BodyPart bp;
    float yawDeg = m_yaw * (180.0f / PI);
    float swing = -m_limbSwing;
    float crouchLegBend = m_crouching ? -25.0f : 0.0f;

    float sx = sinf(m_yaw);
    float cx = cosf(m_yaw);
    float sideOffset = 0.12f;

    bp.position = {
        m_position.x + cx * sideOffset,
        m_position.y + 0.35f,
        m_position.z - sx * sideOffset
    };
    bp.rotation = { swing + crouchLegBend, yawDeg, 0.0f };
    bp.scale    = { 0.14f, 0.35f, 0.14f };
    return bp;
}

// ============================================================
// Animation State Machine Setup
// ============================================================

void Character::SetupAnimStateMachine() {
    m_animSM.Init();

    // ---- Register clips ----
    // Idle — no cycle, no bob
    AnimClip idle;
    idle.type = AnimClipType::Idle;
    idle.cycleSpeed = 0.0f;
    idle.limbSwingAngle = 0.0f;
    idle.bobSpeed = 0.0f;
    idle.bobAmount = 0.0f;
    idle.bobSway = 0.0f;
    idle.looping = true;
    m_animSM.RegisterClip(idle);

    // Walk
    AnimClip walk;
    walk.type = AnimClipType::Walk;
    walk.cycleSpeed = 8.0f;
    walk.limbSwingAngle = 30.0f;
    walk.bobSpeed = 10.0f;
    walk.bobAmount = 0.04f;
    walk.bobSway = 0.02f;
    walk.looping = true;
    m_animSM.RegisterClip(walk);

    // Sprint
    AnimClip sprint;
    sprint.type = AnimClipType::Sprint;
    sprint.cycleSpeed = 14.0f;
    sprint.limbSwingAngle = 45.0f;
    sprint.bobSpeed = 14.0f;
    sprint.bobAmount = 0.06f;
    sprint.bobSway = 0.03f;
    sprint.looping = true;
    m_animSM.RegisterClip(sprint);

    // Crouch (stationary)
    AnimClip crouch;
    crouch.type = AnimClipType::Crouch;
    crouch.cycleSpeed = 0.0f;
    crouch.limbSwingAngle = 0.0f;
    crouch.bobSpeed = 0.0f;
    crouch.bobAmount = 0.0f;
    crouch.bobSway = 0.0f;
    crouch.looping = true;
    m_animSM.RegisterClip(crouch);

    // Crouch walk
    AnimClip crouchWalk;
    crouchWalk.type = AnimClipType::CrouchWalk;
    crouchWalk.cycleSpeed = 5.0f;
    crouchWalk.limbSwingAngle = 15.0f;
    crouchWalk.bobSpeed = 6.0f;
    crouchWalk.bobAmount = 0.02f;
    crouchWalk.bobSway = 0.01f;
    crouchWalk.looping = true;
    m_animSM.RegisterClip(crouchWalk);

    // Jump
    AnimClip jump;
    jump.type = AnimClipType::Jump;
    jump.cycleSpeed = 0.0f;
    jump.limbSwingAngle = 0.0f;
    jump.bobSpeed = 0.0f;
    jump.bobAmount = 0.0f;
    jump.bobSway = 0.0f;
    jump.looping = false;
    jump.duration = 0.5f;
    m_animSM.RegisterClip(jump);

    // Fall
    AnimClip fall;
    fall.type = AnimClipType::Fall;
    fall.cycleSpeed = 0.0f;
    fall.limbSwingAngle = 0.0f;
    fall.bobSpeed = 0.0f;
    fall.bobAmount = 0.0f;
    fall.bobSway = 0.0f;
    fall.looping = true;
    m_animSM.RegisterClip(fall);

    // Land
    AnimClip land;
    land.type = AnimClipType::Land;
    land.cycleSpeed = 0.0f;
    land.limbSwingAngle = 0.0f;
    land.bobSpeed = 0.0f;
    land.bobAmount = 0.0f;
    land.bobSway = 0.0f;
    land.looping = false;
    land.duration = 0.15f;
    m_animSM.RegisterClip(land);

    // ---- Register transitions ----
    // (priority higher = checked first)

    // Any-state → Jump (highest priority, overrides everything)
    m_animSM.AddAnyStateTransition(AnimClipType::Jump,
        [this]() { return !m_grounded && m_velocity.y > 0.0f; },
        0.05f, 100);

    // Any-state → Fall
    m_animSM.AddAnyStateTransition(AnimClipType::Fall,
        [this]() { return !m_grounded && m_velocity.y <= 0.0f; },
        0.1f, 90);

    // Fall/Jump → Land (when grounded)
    m_animSM.AddTransition(AnimClipType::Fall, AnimClipType::Land,
        [this]() { return m_grounded; },
        0.05f, 80);
    m_animSM.AddTransition(AnimClipType::Jump, AnimClipType::Land,
        [this]() { return m_grounded && m_velocity.y <= 0.0f; },
        0.05f, 80);

    // Land → Idle (after landing duration)
    m_animSM.AddTransition(AnimClipType::Land, AnimClipType::Idle,
        [this]() { return m_animSM.GetStateTime() >= 0.15f && !m_moving; },
        0.1f, 70);
    m_animSM.AddTransition(AnimClipType::Land, AnimClipType::Walk,
        [this]() { return m_animSM.GetStateTime() >= 0.1f && m_moving && !m_sprinting; },
        0.1f, 70);
    m_animSM.AddTransition(AnimClipType::Land, AnimClipType::Sprint,
        [this]() { return m_animSM.GetStateTime() >= 0.1f && m_moving && m_sprinting; },
        0.1f, 70);

    // Idle → Walk
    m_animSM.AddTransition(AnimClipType::Idle, AnimClipType::Walk,
        [this]() { return m_moving && !m_sprinting && !m_crouching; },
        0.15f, 10);

    // Idle → Sprint
    m_animSM.AddTransition(AnimClipType::Idle, AnimClipType::Sprint,
        [this]() { return m_moving && m_sprinting; },
        0.15f, 10);

    // Walk → Idle
    m_animSM.AddTransition(AnimClipType::Walk, AnimClipType::Idle,
        [this]() { return !m_moving && !m_crouching; },
        0.2f, 10);

    // Walk → Sprint
    m_animSM.AddTransition(AnimClipType::Walk, AnimClipType::Sprint,
        [this]() { return m_sprinting; },
        0.15f, 10);

    // Sprint → Walk
    m_animSM.AddTransition(AnimClipType::Sprint, AnimClipType::Walk,
        [this]() { return m_moving && !m_sprinting && !m_crouching; },
        0.15f, 10);

    // Sprint → Idle
    m_animSM.AddTransition(AnimClipType::Sprint, AnimClipType::Idle,
        [this]() { return !m_moving; },
        0.2f, 10);

    // Idle → Crouch
    m_animSM.AddTransition(AnimClipType::Idle, AnimClipType::Crouch,
        [this]() { return m_crouching && !m_moving; },
        0.15f, 20);

    // Walk → CrouchWalk
    m_animSM.AddTransition(AnimClipType::Walk, AnimClipType::CrouchWalk,
        [this]() { return m_crouching && m_moving; },
        0.15f, 20);

    // Crouch → CrouchWalk
    m_animSM.AddTransition(AnimClipType::Crouch, AnimClipType::CrouchWalk,
        [this]() { return m_crouching && m_moving; },
        0.15f, 15);

    // CrouchWalk → Crouch
    m_animSM.AddTransition(AnimClipType::CrouchWalk, AnimClipType::Crouch,
        [this]() { return m_crouching && !m_moving; },
        0.2f, 15);

    // Crouch → Idle (un-crouch)
    m_animSM.AddTransition(AnimClipType::Crouch, AnimClipType::Idle,
        [this]() { return !m_crouching && !m_moving; },
        0.15f, 20);

    // Crouch → Walk (un-crouch while moving)
    m_animSM.AddTransition(AnimClipType::Crouch, AnimClipType::Walk,
        [this]() { return !m_crouching && m_moving; },
        0.15f, 20);

    // CrouchWalk → Walk (un-crouch while moving)
    m_animSM.AddTransition(AnimClipType::CrouchWalk, AnimClipType::Walk,
        [this]() { return !m_crouching && m_moving; },
        0.15f, 20);

    // CrouchWalk → Idle (un-crouch, stopped)
    m_animSM.AddTransition(AnimClipType::CrouchWalk, AnimClipType::Idle,
        [this]() { return !m_crouching && !m_moving; },
        0.2f, 20);

    // Start in idle
    m_animSM.ForceState(AnimClipType::Idle);
}

} // namespace WT
