#include "FPSArmsAnimator.h"
#include "Graphics/SkinnedMesh.h"
#include <cmath>
#include <algorithm>

namespace WT {

// ============================================================
// Constants
// ============================================================

// Fire recoil (very subtle — matched to gun recoil)
static constexpr float FIRE_SNAP_TIME    = 0.035f;   // seconds to peak recoil
static constexpr float FIRE_RECOVER_TIME = 0.20f;    // seconds to settle
static constexpr float FIRE_PITCH        = -0.008f;  // radians (backward pitch) — very subtle
static constexpr float FIRE_TRANSLATE_Z  = -0.001f;  // backward push — minimal
static constexpr float FIRE_HAND_SNAP    = 0.004f;   // hand snap-back radians — minimal

// Finger grip curl (radians per joint)
static constexpr float FINGER_CURL_01    = 0.7f;
static constexpr float FINGER_CURL_02    = 0.9f;
static constexpr float FINGER_CURL_03    = 0.6f;
static constexpr float THUMB_CURL_01     = 0.4f;
static constexpr float THUMB_CURL_02     = 0.3f;
static constexpr float THUMB_CURL_03     = 0.2f;

// ============================================================
// Init
// ============================================================

void FPSArmsAnimator::Init(SkinnedMesh& mesh) {
    m_mesh = &mesh;

    // Cache bind-pose local transforms
    m_bindLocals = mesh.GetLocalTransforms();

    // Look up bone indices by name
    m_shoulderL = mesh.FindBone("shoulder.L");
    m_shoulderR = mesh.FindBone("shoulder.R");
    m_upperArmL = mesh.FindBone("upper_arm.L");
    m_upperArmR = mesh.FindBone("upper_arm.R");
    m_forearmL  = mesh.FindBone("forearm.L");
    m_forearmR  = mesh.FindBone("forearm.R");
    m_handL     = mesh.FindBone("hand.L");
    m_handR     = mesh.FindBone("hand.R");

    // Finger bones
    const char* fingerNames[] = { "f_index", "f_middle", "f_ring", "f_pinky" };
    int (*fingerArrays[])[2] = { m_indexFinger, m_middleFinger, m_ringFinger, m_pinkyFinger };

    for (int f = 0; f < 4; f++) {
        for (int j = 0; j < 3; j++) {
            char nameL[32], nameR[32];
            snprintf(nameL, sizeof(nameL), "%s.%02d.L", fingerNames[f], j + 1);
            snprintf(nameR, sizeof(nameR), "%s.%02d.R", fingerNames[f], j + 1);
            fingerArrays[f][j][0] = mesh.FindBone(nameL);
            fingerArrays[f][j][1] = mesh.FindBone(nameR);
        }
    }

    for (int j = 0; j < 3; j++) {
        char nameL[32], nameR[32];
        snprintf(nameL, sizeof(nameL), "thumb.%02d.L", j + 1);
        snprintf(nameR, sizeof(nameR), "thumb.%02d.R", j + 1);
        m_thumb[j][0] = mesh.FindBone(nameL);
        m_thumb[j][1] = mesh.FindBone(nameR);
    }

    // Compute bone lengths from bind pose world positions
    mesh.ResetToBindPose();
    const auto& worldPoses = mesh.GetWorldPoses();

    auto getBoneWorldPos = [&](int idx) -> XMVECTOR {
        if (idx < 0 || idx >= static_cast<int>(worldPoses.size()))
            return XMVectorZero();
        XMMATRIX wp = XMLoadFloat4x4(&worldPoses[idx]);
        return wp.r[3];
    };

    // Left arm chain lengths
    if (m_upperArmL >= 0 && m_forearmL >= 0 && m_handL >= 0) {
        XMVECTOR pUpper = getBoneWorldPos(m_upperArmL);
        XMVECTOR pFore  = getBoneWorldPos(m_forearmL);
        XMVECTOR pHand  = getBoneWorldPos(m_handL);
        m_upperArmLenL = XMVectorGetX(XMVector3Length(XMVectorSubtract(pFore, pUpper)));
        m_forearmLenL  = XMVectorGetX(XMVector3Length(XMVectorSubtract(pHand, pFore)));
    }

    // Right arm chain lengths
    if (m_upperArmR >= 0 && m_forearmR >= 0 && m_handR >= 0) {
        XMVECTOR pUpper = getBoneWorldPos(m_upperArmR);
        XMVECTOR pFore  = getBoneWorldPos(m_forearmR);
        XMVECTOR pHand  = getBoneWorldPos(m_handR);
        m_upperArmLenR = XMVectorGetX(XMVector3Length(XMVectorSubtract(pFore, pUpper)));
        m_forearmLenR  = XMVectorGetX(XMVector3Length(XMVectorSubtract(pHand, pFore)));
    }

    // Reset timers
    m_breathTimer = 0.0f;
    m_walkTimer   = 0.0f;
    m_fireTimer   = -1.0f;
    m_reloadTimer = -1.0f;
}

// ============================================================
// Set IK Targets
// ============================================================

void FPSArmsAnimator::SetHandIKTargets(const XMFLOAT3& rightTarget, const XMFLOAT3& leftTarget,
                                        const XMFLOAT3& poleOffset) {
    m_rightHandTarget = rightTarget;
    m_leftHandTarget  = leftTarget;
    m_poleOffset      = poleOffset;
}

// ============================================================
// Main Update
// ============================================================

void FPSArmsAnimator::Update(float dt, bool isMoving, bool isFiring, bool isReloading, bool isADS) {
    if (!m_mesh || m_bindLocals.empty()) return;

    m_isADS = isADS;

    // Detect fire trigger
    if (isFiring && !m_wasFiring) {
        TriggerFire();
    }
    m_wasFiring = isFiring;

    // Detect reload trigger
    if (isReloading && m_reloadTimer < 0.0f) {
        TriggerReload(2.0f);
    }
    if (!isReloading) {
        m_reloadTimer = -1.0f;
    }

    // Reset all bones to bind pose local transforms
    int numBones = m_mesh->GetBoneCount();
    for (int i = 0; i < numBones && i < static_cast<int>(m_bindLocals.size()); i++) {
        m_mesh->SetBoneLocalTransform(i, XMLoadFloat4x4(&m_bindLocals[i]));
    }

    // Compute world poses so IK can read current bone positions
    m_mesh->ComputeFinalMatrices();

    // Apply two-bone IK to position hands on gun sockets
    if (m_ikEnabled) {
        // Right arm IK
        if (m_upperArmR >= 0 && m_forearmR >= 0 && m_handR >= 0) {
            const auto& wp = m_mesh->GetWorldPoses();
            XMFLOAT3 upperPos;
            XMMATRIX upperWorld = XMLoadFloat4x4(&wp[m_upperArmR]);
            upperPos.x = XMVectorGetX(upperWorld.r[3]);
            upperPos.y = XMVectorGetY(upperWorld.r[3]);
            upperPos.z = XMVectorGetZ(upperWorld.r[3]);
            XMFLOAT3 poleR = {
                upperPos.x + m_poleOffset.x,
                upperPos.y + m_poleOffset.y,
                upperPos.z + m_poleOffset.z
            };
            SolveTwoBoneIK(m_upperArmR, m_forearmR, m_handR, m_rightHandTarget, poleR);
        }

        // Left arm IK
        if (m_upperArmL >= 0 && m_forearmL >= 0 && m_handL >= 0) {
            const auto& wp = m_mesh->GetWorldPoses();
            XMFLOAT3 upperPos;
            XMMATRIX upperWorld = XMLoadFloat4x4(&wp[m_upperArmL]);
            upperPos.x = XMVectorGetX(upperWorld.r[3]);
            upperPos.y = XMVectorGetY(upperWorld.r[3]);
            upperPos.z = XMVectorGetZ(upperWorld.r[3]);
            XMFLOAT3 poleL = {
                upperPos.x - m_poleOffset.x,
                upperPos.y + m_poleOffset.y,
                upperPos.z + m_poleOffset.z
            };
            SolveTwoBoneIK(m_upperArmL, m_forearmL, m_handL, m_leftHandTarget, poleL);
        }
    }

    // Apply fire recoil (on top of IK position)
    if (m_fireTimer >= 0.0f) {
        ApplyFire(dt);
    }

    if (m_reloadTimer >= 0.0f) {
        ApplyReload(dt);
    }

    // Always apply finger grip
    ApplyFingerGrip();

    // Recompute final bone matrices for GPU
    m_mesh->ComputeFinalMatrices();
}

// ============================================================
// Trigger Methods
// ============================================================

void FPSArmsAnimator::TriggerFire() {
    m_fireTimer = 0.0f;
}

void FPSArmsAnimator::TriggerReload(float reloadDuration) {
    m_reloadTimer = 0.0f;
    m_reloadDuration = reloadDuration;
}

// ============================================================
// Two-Bone IK Solver
// ============================================================

void FPSArmsAnimator::SolveTwoBoneIK(int upperIdx, int forearmIdx, int handIdx,
                                      const XMFLOAT3& target, const XMFLOAT3& poleTarget) {
    if (!m_mesh) return;

    const auto& bones = m_mesh->GetBones();
    int numBones = m_mesh->GetBoneCount();

    if (upperIdx >= numBones || forearmIdx >= numBones || handIdx >= numBones) return;

    // Helper: get world-space position from world pose matrix
    auto getWorldPos = [&]() {
        // Return a lambda that captures fresh world poses each time
        return [&](int idx) -> XMVECTOR {
            const auto& wp = m_mesh->GetWorldPoses();
            XMMATRIX m = XMLoadFloat4x4(&wp[idx]);
            return m.r[3];
        };
    };

    auto getPos = getWorldPos();

    // Current joint positions
    XMVECTOR posA = getPos(upperIdx);   // upper arm
    XMVECTOR posB = getPos(forearmIdx); // elbow
    XMVECTOR posC = getPos(handIdx);    // hand

    // Current bone directions (from world positions)
    XMVECTOR curDirAB = XMVectorSubtract(posB, posA);
    XMVECTOR curDirBC = XMVectorSubtract(posC, posB);

    float lenAB = XMVectorGetX(XMVector3Length(curDirAB));
    float lenBC = XMVectorGetX(XMVector3Length(curDirBC));

    if (lenAB < 0.0001f || lenBC < 0.0001f) return;

    curDirAB = XMVectorScale(curDirAB, 1.0f / lenAB);
    curDirBC = XMVectorScale(curDirBC, 1.0f / lenBC);

    // Compute target hand position (clamped to reachable range)
    XMVECTOR posTarget = XMLoadFloat3(&target);
    XMVECTOR toTarget = XMVectorSubtract(posTarget, posA);
    float dist = XMVectorGetX(XMVector3Length(toTarget));

    if (dist < 0.0001f) return;

    float maxReach = lenAB + lenBC - 0.001f;
    float minReach = fabsf(lenAB - lenBC) + 0.001f;
    dist = (std::max)(minReach, (std::min)(maxReach, dist));
    posTarget = XMVectorAdd(posA, XMVectorScale(XMVector3Normalize(toTarget), dist));

    // Law of cosines: angle at upper arm
    float cosAngleA = (lenAB * lenAB + dist * dist - lenBC * lenBC) / (2.0f * lenAB * dist);
    cosAngleA = (std::max)(-1.0f, (std::min)(1.0f, cosAngleA));
    float angleA = acosf(cosAngleA);

    // Direction from root to target
    XMVECTOR dirToTarget = XMVector3Normalize(XMVectorSubtract(posTarget, posA));

    // IK plane from pole target
    XMVECTOR posPole = XMLoadFloat3(&poleTarget);
    XMVECTOR toPole = XMVectorSubtract(posPole, posA);
    XMVECTOR planeNormal = XMVector3Cross(dirToTarget, toPole);
    float planeNormLen = XMVectorGetX(XMVector3Length(planeNormal));

    if (planeNormLen < 0.001f) {
        planeNormal = XMVectorSet(0, 0, 1, 0);
    }
    planeNormal = XMVector3Normalize(planeNormal);
    XMVECTOR planeUp = XMVector3Normalize(XMVector3Cross(planeNormal, dirToTarget));

    // New elbow position
    XMVECTOR newDirAB = XMVectorAdd(
        XMVectorScale(dirToTarget, cosAngleA),
        XMVectorScale(planeUp, sinf(angleA))
    );
    newDirAB = XMVector3Normalize(newDirAB);

    XMVECTOR newPosB = XMVectorAdd(posA, XMVectorScale(newDirAB, lenAB));
    XMVECTOR newDirBC = XMVector3Normalize(XMVectorSubtract(posTarget, newPosB));

    // === Apply rotation deltas to bones ===
    // The idea: compute the rotation that swings the current bone direction to the
    // desired direction, then apply that rotation to the bone's local transform.

    auto computeSwingRotation = [](XMVECTOR fromDir, XMVECTOR toDir) -> XMMATRIX {
        float dot = XMVectorGetX(XMVector3Dot(fromDir, toDir));
        if (dot > 0.9999f) return XMMatrixIdentity();
        if (dot < -0.9999f) {
            // 180 degree rotation
            XMVECTOR perp = XMVectorSet(1, 0, 0, 0);
            if (fabsf(XMVectorGetX(XMVector3Dot(fromDir, perp))) > 0.9f)
                perp = XMVectorSet(0, 1, 0, 0);
            XMVECTOR axis = XMVector3Normalize(XMVector3Cross(fromDir, perp));
            return XMMatrixRotationAxis(axis, PI);
        }
        XMVECTOR axis = XMVector3Cross(fromDir, toDir);
        float axisLen = XMVectorGetX(XMVector3Length(axis));
        if (axisLen < 0.0001f) return XMMatrixIdentity();
        axis = XMVectorScale(axis, 1.0f / axisLen);
        float angle = acosf((std::max)(-1.0f, (std::min)(1.0f, dot)));
        return XMMatrixRotationAxis(axis, angle);
    };

    // Upper arm: rotate from current direction to new direction
    // Convert world-space swing to local space using row-vector convention:
    // localSwing = parentRot * worldSwing * inv(parentRot)
    // newLocalRot = bindRot * localSwing
    {
        XMMATRIX worldSwing = computeSwingRotation(curDirAB, newDirAB);

        const auto& wp = m_mesh->GetWorldPoses();
        int parentIdx = bones[upperIdx].parentIndex;
        XMMATRIX parentWorld = (parentIdx >= 0)
            ? XMLoadFloat4x4(&wp[parentIdx])
            : XMMatrixIdentity();

        // Extract pure rotation (normalize rows to remove any scale)
        XMMATRIX parentRot = parentWorld;
        parentRot.r[3] = XMVectorSet(0, 0, 0, 1);
        parentRot.r[0] = XMVector3Normalize(parentRot.r[0]);
        parentRot.r[1] = XMVector3Normalize(parentRot.r[1]);
        parentRot.r[2] = XMVector3Normalize(parentRot.r[2]);
        XMVECTOR det;
        XMMATRIX parentRotInv = XMMatrixInverse(&det, parentRot);

        // Row-vector convention: localSwing = parentRot * worldSwing * inv(parentRot)
        XMMATRIX localSwing = parentRot * worldSwing * parentRotInv;

        XMMATRIX bindLocal = XMLoadFloat4x4(&m_bindLocals[upperIdx]);
        XMVECTOR savedTrans = bindLocal.r[3];

        XMMATRIX bindRot = bindLocal;
        bindRot.r[3] = XMVectorSet(0, 0, 0, 1);

        // Row-vector convention: newRot = bindRot * localSwing
        XMMATRIX newRot = bindRot * localSwing;
        newRot.r[3] = savedTrans;

        m_mesh->SetBoneLocalTransform(upperIdx, newRot);
    }

    // Recompute world poses after upper arm change
    m_mesh->ComputeFinalMatrices();

    // Forearm: rotate from current direction to new direction
    {
        auto getPos2 = getWorldPos();
        XMVECTOR curPosB2 = getPos2(forearmIdx);
        XMVECTOR curPosC2 = getPos2(handIdx);
        XMVECTOR curDirBC2 = XMVector3Normalize(XMVectorSubtract(curPosC2, curPosB2));

        XMMATRIX worldSwing = computeSwingRotation(curDirBC2, newDirBC);

        const auto& wp = m_mesh->GetWorldPoses();
        int parentIdx = bones[forearmIdx].parentIndex;
        XMMATRIX parentWorld = (parentIdx >= 0)
            ? XMLoadFloat4x4(&wp[parentIdx])
            : XMMatrixIdentity();

        XMMATRIX parentRot = parentWorld;
        parentRot.r[3] = XMVectorSet(0, 0, 0, 1);
        parentRot.r[0] = XMVector3Normalize(parentRot.r[0]);
        parentRot.r[1] = XMVector3Normalize(parentRot.r[1]);
        parentRot.r[2] = XMVector3Normalize(parentRot.r[2]);
        XMVECTOR det;
        XMMATRIX parentRotInv = XMMatrixInverse(&det, parentRot);

        // Row-vector convention: localSwing = parentRot * worldSwing * inv(parentRot)
        XMMATRIX localSwing = parentRot * worldSwing * parentRotInv;

        XMMATRIX bindLocal = XMLoadFloat4x4(&m_bindLocals[forearmIdx]);
        XMVECTOR savedTrans = bindLocal.r[3];

        XMMATRIX bindRot = bindLocal;
        bindRot.r[3] = XMVectorSet(0, 0, 0, 1);

        // Row-vector convention: newRot = bindRot * localSwing
        XMMATRIX newRot = bindRot * localSwing;
        newRot.r[3] = savedTrans;

        m_mesh->SetBoneLocalTransform(forearmIdx, newRot);
    }

    // Recompute for hand
    m_mesh->ComputeFinalMatrices();

    // Hand: keep bind-pose local transform (fingers will curl on top of it)
    // Just keep it as-is from the bind-pose reset
}

// ============================================================
// Fire Recoil (improved — damped spring with bounce)
// ============================================================

void FPSArmsAnimator::ApplyFire(float dt) {
    m_fireTimer += dt;

    float totalDuration = FIRE_SNAP_TIME + FIRE_RECOVER_TIME;
    if (m_fireTimer > totalDuration) {
        m_fireTimer = -1.0f;
        return;
    }

    float recoilAmount;
    if (m_fireTimer < FIRE_SNAP_TIME) {
        float t = m_fireTimer / FIRE_SNAP_TIME;
        recoilAmount = 1.0f - (1.0f - t) * (1.0f - t);
    } else {
        float t = (m_fireTimer - FIRE_SNAP_TIME) / FIRE_RECOVER_TIME;
        float decay = expf(-4.0f * t);
        float bounce = cosf(t * PI * 1.5f);
        recoilAmount = decay * bounce;
    }

    recoilAmount = (std::max)(-0.2f, (std::min)(1.0f, recoilAmount));

    ApplyRotationToBone(m_upperArmL, FIRE_PITCH * recoilAmount, 0.0f, 0.0f);
    ApplyRotationToBone(m_upperArmR, FIRE_PITCH * recoilAmount, 0.0f, 0.0f);

    float forearmRecoil = recoilAmount * 0.6f;
    ApplyRotationToBone(m_forearmL, FIRE_PITCH * forearmRecoil, 0.0f, 0.0f);
    ApplyRotationToBone(m_forearmR, FIRE_PITCH * forearmRecoil, 0.0f, 0.0f);

    ApplyRotationToBone(m_handL, FIRE_HAND_SNAP * recoilAmount, 0.0f, 0.0f);
    ApplyRotationToBone(m_handR, FIRE_HAND_SNAP * recoilAmount, 0.0f, 0.0f);

    ApplyTranslationToBone(m_handL, 0.0f, 0.0f, FIRE_TRANSLATE_Z * recoilAmount);
    ApplyTranslationToBone(m_handR, 0.0f, 0.0f, FIRE_TRANSLATE_Z * recoilAmount);
}

// ============================================================
// Reload Motion
// ============================================================

void FPSArmsAnimator::ApplyReload(float dt) {
    m_reloadTimer += dt;

    if (m_reloadTimer > m_reloadDuration) {
        m_reloadTimer = -1.0f;
        return;
    }

    float progress = m_reloadTimer / m_reloadDuration;

    float leftHandDrop   = 0.0f;
    float leftHandRotate = 0.0f;

    if (progress < 0.3f) {
        float t = progress / 0.3f;
        leftHandDrop   = sinf(t * PI * 0.5f);
        leftHandRotate = t;
    } else if (progress < 0.7f) {
        leftHandDrop   = 1.0f;
        float t = (progress - 0.3f) / 0.4f;
        leftHandRotate = 1.0f - 0.3f * sinf(t * PI);
    } else {
        float t = (progress - 0.7f) / 0.3f;
        leftHandDrop   = 1.0f - sinf(t * PI * 0.5f);
        leftHandRotate = 1.0f - t;
    }

    ApplyRotationToBone(m_forearmL, -0.4f * leftHandDrop, 0.0f, -0.2f * leftHandRotate);
    ApplyTranslationToBone(m_handL, 0.0f, -0.06f * leftHandDrop, 0.02f * leftHandDrop);

    float wobble = sinf(progress * PI * 4.0f) * 0.01f * (1.0f - progress);
    ApplyRotationToBone(m_upperArmR, wobble, 0.0f, 0.0f);
}

// ============================================================
// Finger Grip
// ============================================================

void FPSArmsAnimator::ApplyFingerGrip() {
    float curlAmounts[3] = { FINGER_CURL_01, FINGER_CURL_02, FINGER_CURL_03 };
    float thumbCurls[3]  = { THUMB_CURL_01, THUMB_CURL_02, THUMB_CURL_03 };

    int (*fingerArrays[])[2] = { m_indexFinger, m_middleFinger, m_ringFinger, m_pinkyFinger };

    for (int f = 0; f < 4; f++) {
        for (int j = 0; j < 3; j++) {
            for (int side = 0; side < 2; side++) {
                ApplyRotationToBone(fingerArrays[f][j][side], curlAmounts[j], 0.0f, 0.0f);
            }
        }
    }

    for (int j = 0; j < 3; j++) {
        for (int side = 0; side < 2; side++) {
            ApplyRotationToBone(m_thumb[j][side], thumbCurls[j], 0.0f, 0.0f);
        }
    }
}

// ============================================================
// Helpers
// ============================================================

void FPSArmsAnimator::ApplyRotationToBone(int boneIndex, float pitch, float yaw, float roll) {
    if (boneIndex < 0 || !m_mesh) return;
    if (boneIndex >= m_mesh->GetBoneCount()) return;

    const auto& locals = m_mesh->GetLocalTransforms();
    if (boneIndex >= static_cast<int>(locals.size())) return;

    XMMATRIX localMat = XMLoadFloat4x4(&locals[boneIndex]);
    XMMATRIX rot = XMMatrixRotationRollPitchYaw(pitch, yaw, roll);
    XMMATRIX result = XMMatrixMultiply(rot, localMat);

    m_mesh->SetBoneLocalTransform(boneIndex, result);
}

void FPSArmsAnimator::ApplyTranslationToBone(int boneIndex, float dx, float dy, float dz) {
    if (boneIndex < 0 || !m_mesh) return;
    if (boneIndex >= m_mesh->GetBoneCount()) return;

    const auto& locals = m_mesh->GetLocalTransforms();
    if (boneIndex >= static_cast<int>(locals.size())) return;

    XMMATRIX localMat = XMLoadFloat4x4(&locals[boneIndex]);
    XMMATRIX trans = XMMatrixTranslation(dx, dy, dz);
    XMMATRIX result = XMMatrixMultiply(localMat, trans);

    m_mesh->SetBoneLocalTransform(boneIndex, result);
}

} // namespace WT
