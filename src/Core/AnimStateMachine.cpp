#include "AnimStateMachine.h"
#include "Util/MathHelpers.h"
#include "Util/Log.h"
#include <algorithm>
#include <cmath>

namespace WT {

void AnimStateMachine::Init() {
    m_clips.clear();
    m_transitions.clear();
    m_currentClip  = AnimClipType::Idle;
    m_previousClip = AnimClipType::Idle;
    m_stateTime    = 0.0f;
    m_walkCycle    = 0.0f;
    m_headBobTimer = 0.0f;
    m_blendTimer   = 0.0f;
    m_blendDuration = 0.0f;
    m_prevLimbSwing = 0.0f;
    m_prevBobY = 0.0f;
    m_prevBobX = 0.0f;
    m_output = {};
}

// ==================== Clip Registration ====================

void AnimStateMachine::RegisterClip(const AnimClip& clip) {
    // Replace if already registered
    for (auto& c : m_clips) {
        if (c.type == clip.type) {
            c = clip;
            return;
        }
    }
    m_clips.push_back(clip);
}

// ==================== Transition Registration ====================

void AnimStateMachine::AddTransition(AnimClipType from, AnimClipType to,
                                      TransitionCondition condition,
                                      float blendTime, int priority) {
    AnimTransition t;
    t.from      = from;
    t.to        = to;
    t.condition = condition;
    t.blendTime = blendTime;
    t.priority  = priority;
    m_transitions.push_back(t);

    // Keep sorted by priority (highest first)
    std::sort(m_transitions.begin(), m_transitions.end(),
              [](const AnimTransition& a, const AnimTransition& b) {
                  return a.priority > b.priority;
              });
}

void AnimStateMachine::AddAnyStateTransition(AnimClipType to,
                                              TransitionCondition condition,
                                              float blendTime, int priority) {
    // Register a transition from every registered clip type
    // Use Count as a sentinel for "any state"
    AnimTransition t;
    t.from      = AnimClipType::Count;  // Sentinel: matches any source state
    t.to        = to;
    t.condition = condition;
    t.blendTime = blendTime;
    t.priority  = priority;
    m_transitions.push_back(t);

    std::sort(m_transitions.begin(), m_transitions.end(),
              [](const AnimTransition& a, const AnimTransition& b) {
                  return a.priority > b.priority;
              });
}

// ==================== Update ====================

void AnimStateMachine::Update(float dt) {
    m_stateTime += dt;

    // Check transitions
    EvaluateTransitions();

    // Get current clip parameters
    const AnimClip* clip = FindClip(m_currentClip);
    if (!clip) {
        m_output.activeClip = m_currentClip;
        m_output.stateTime  = m_stateTime;
        return;
    }

    // Non-looping clip: check if duration expired
    if (!clip->looping && clip->duration > 0.0f && m_stateTime >= clip->duration) {
        // Let transitions handle what happens when clip ends
        // (e.g., Land -> Idle transition with stateTime condition)
    }

    // ---- Walk cycle / limb swing ----
    float targetLimbSwing = 0.0f;
    if (clip->cycleSpeed > 0.0f) {
        m_walkCycle += clip->cycleSpeed * dt;
        if (m_walkCycle > TWO_PI) m_walkCycle -= TWO_PI;
        targetLimbSwing = sinf(m_walkCycle) * clip->limbSwingAngle;
    } else {
        // Return to rest smoothly
        targetLimbSwing = 0.0f;
        m_walkCycle *= 0.85f;
    }

    // ---- Head bob ----
    float targetBobY = 0.0f;
    float targetBobX = 0.0f;
    if (clip->bobSpeed > 0.0f) {
        m_headBobTimer += clip->bobSpeed * dt;
        targetBobY = sinf(m_headBobTimer) * clip->bobAmount;
        targetBobX = cosf(m_headBobTimer * 0.5f) * clip->bobSway;
    } else {
        // Return to rest smoothly
        m_headBobTimer = 0.0f;
        targetBobY = 0.0f;
        targetBobX = 0.0f;
    }

    // ---- Blending ----
    float blendFactor = 1.0f;
    if (m_blendTimer > 0.0f) {
        m_blendTimer -= dt;
        if (m_blendTimer <= 0.0f) {
            m_blendTimer = 0.0f;
            blendFactor = 1.0f;
        } else {
            blendFactor = 1.0f - (m_blendTimer / m_blendDuration);
        }
    }

    // Blend between previous and current values
    float finalLimbSwing = Lerp(m_prevLimbSwing, targetLimbSwing, blendFactor);
    float finalBobY      = Lerp(m_prevBobY, targetBobY, blendFactor);
    float finalBobX      = Lerp(m_prevBobX, targetBobX, blendFactor);

    // Smooth damping for limb swing when returning to idle
    if (clip->cycleSpeed <= 0.0f && blendFactor >= 1.0f) {
        finalLimbSwing = m_output.limbSwing * 0.85f;
        if (fabsf(finalLimbSwing) < 0.1f) finalLimbSwing = 0.0f;

        finalBobY = m_output.headBobY * 0.9f;
        finalBobX = m_output.headBobX * 0.9f;
    }

    // ---- Write output ----
    m_output.walkCycle   = m_walkCycle;
    m_output.limbSwing   = finalLimbSwing;
    m_output.headBobY    = finalBobY;
    m_output.headBobX    = finalBobX;
    m_output.blendFactor = blendFactor;
    m_output.activeClip  = m_currentClip;
    m_output.stateTime   = m_stateTime;
}

// ==================== Force State ====================

void AnimStateMachine::ForceState(AnimClipType clip) {
    m_previousClip = m_currentClip;
    m_currentClip  = clip;
    m_stateTime    = 0.0f;
    m_blendTimer   = 0.0f;
    m_blendDuration = 0.0f;
    m_output.activeClip = clip;
}

// ==================== Internal ====================

const AnimClip* AnimStateMachine::FindClip(AnimClipType type) const {
    for (const auto& c : m_clips) {
        if (c.type == type) return &c;
    }
    return nullptr;
}

void AnimStateMachine::EvaluateTransitions() {
    for (const auto& t : m_transitions) {
        // Check if transition matches current state (or is any-state)
        if (t.from != AnimClipType::Count && t.from != m_currentClip)
            continue;

        // Don't transition to the same state
        if (t.to == m_currentClip)
            continue;

        // Evaluate condition
        if (t.condition && t.condition()) {
            TransitionTo(t.to, t.blendTime);
            break;  // Only take highest-priority matching transition
        }
    }
}

void AnimStateMachine::TransitionTo(AnimClipType newClip, float blendTime) {
    // Snapshot current values for blending
    m_prevLimbSwing = m_output.limbSwing;
    m_prevBobY      = m_output.headBobY;
    m_prevBobX      = m_output.headBobX;

    m_previousClip = m_currentClip;
    m_currentClip  = newClip;
    m_stateTime    = 0.0f;

    m_blendDuration = blendTime;
    m_blendTimer    = blendTime;
}

} // namespace WT
