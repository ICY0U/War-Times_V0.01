#pragma once

#include <DirectXMath.h>
#include <vector>
#include <string>

namespace WT {

using namespace DirectX;
class SkinnedMesh;

// ============================================================
// FPSArmsAnimator — Procedural animation for first-person arms
// Drives SkinnedMesh bone transforms based on gameplay state.
// Includes two-bone IK for placing hands on gun grip sockets.
// ============================================================
class FPSArmsAnimator {
public:
    // Call once after SkinnedMesh::LoadFromFile()
    void Init(SkinnedMesh& mesh);

    // Set IK hand targets (in arm-model local space, before arms world transform)
    // Call this BEFORE Update() each frame when a gun is equipped
    void SetHandIKTargets(const XMFLOAT3& rightTarget, const XMFLOAT3& leftTarget,
                          const XMFLOAT3& poleOffset);
    void SetHandIKEnabled(bool enabled) { m_ikEnabled = enabled; }

    // Call each frame — updates bone transforms procedurally
    // then calls ComputeFinalMatrices() on the SkinnedMesh
    void Update(float deltaTime, bool isMoving, bool isFiring, bool isReloading, bool isADS);

    // Trigger a fire recoil animation
    void TriggerFire();

    // Trigger reload
    void TriggerReload(float reloadDuration);

private:
    // Apply fire recoil
    void ApplyFire(float dt);

    // Apply reload motion
    void ApplyReload(float dt);

    // Apply finger curl (grip)
    void ApplyFingerGrip();

    // Two-bone IK solver for an arm chain (upper_arm → forearm → hand)
    // Positions the hand at 'target' in arm-model space
    void SolveTwoBoneIK(int upperIdx, int forearmIdx, int handIdx,
                        const XMFLOAT3& target, const XMFLOAT3& poleTarget);

    // Helper: compose procedural rotation on top of current local transform
    void ApplyRotationToBone(int boneIndex, float pitch, float yaw, float roll);
    void ApplyTranslationToBone(int boneIndex, float dx, float dy, float dz);

    SkinnedMesh* m_mesh = nullptr;

    // Cached bind-pose local transforms (baseline)
    std::vector<XMFLOAT4X4> m_bindLocals;

    // Cached bone indices (-1 if not found)
    int m_shoulderL = -1, m_shoulderR = -1;
    int m_upperArmL = -1, m_upperArmR = -1;
    int m_forearmL  = -1, m_forearmR  = -1;
    int m_handL     = -1, m_handR     = -1;

    // Finger bones: [joint][side] — joint 0-2, side 0=L 1=R
    int m_indexFinger[3][2]  = {};
    int m_middleFinger[3][2] = {};
    int m_ringFinger[3][2]   = {};
    int m_pinkyFinger[3][2]  = {};
    int m_thumb[3][2]        = {};

    // IK state
    bool     m_ikEnabled = false;
    XMFLOAT3 m_rightHandTarget = { 0, 0, 0 };
    XMFLOAT3 m_leftHandTarget  = { 0, 0, 0 };
    XMFLOAT3 m_poleOffset      = { 0, -0.3f, 0 };

    // Cached bone lengths (computed during Init from bind pose)
    float m_upperArmLenL = 0.0f, m_forearmLenL = 0.0f;
    float m_upperArmLenR = 0.0f, m_forearmLenR = 0.0f;

    // Timers
    float m_breathTimer = 0.0f;
    float m_walkTimer   = 0.0f;
    float m_fireTimer   = -1.0f;
    float m_reloadTimer = -1.0f;
    float m_reloadDuration = 2.0f;

    // State
    bool m_wasMoving  = false;
    bool m_wasFiring  = false;
    bool m_isADS      = false;
};

} // namespace WT
