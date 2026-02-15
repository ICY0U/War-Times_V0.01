#pragma once

#include <DirectXMath.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace WT {

using namespace DirectX;

// ============================================================
// Animation State Machine — data-driven state/transition system
// Drives procedural animation (walk cycles, limb swing, etc.)
// ============================================================

// ---- Animation clip types ----
enum class AnimClipType : uint8_t {
    Idle = 0,
    Walk,
    Sprint,
    Crouch,
    CrouchWalk,
    Jump,
    Fall,
    Land,
    Count
};

inline const char* AnimClipTypeName(AnimClipType t) {
    switch (t) {
        case AnimClipType::Idle:       return "Idle";
        case AnimClipType::Walk:       return "Walk";
        case AnimClipType::Sprint:     return "Sprint";
        case AnimClipType::Crouch:     return "Crouch";
        case AnimClipType::CrouchWalk: return "CrouchWalk";
        case AnimClipType::Jump:       return "Jump";
        case AnimClipType::Fall:       return "Fall";
        case AnimClipType::Land:       return "Land";
        default:                       return "Unknown";
    }
}

// ---- Animation clip — defines procedural animation parameters ----
struct AnimClip {
    AnimClipType type    = AnimClipType::Idle;
    float cycleSpeed     = 0.0f;    // Walk cycle oscillation speed
    float limbSwingAngle = 0.0f;    // Max limb swing in degrees
    float bobSpeed       = 0.0f;    // Head bob speed multiplier
    float bobAmount      = 0.0f;    // Bob vertical amplitude
    float bobSway        = 0.0f;    // Bob horizontal amplitude
    bool  looping        = true;    // Does the clip loop?
    float duration       = 0.0f;    // For non-looping clips (seconds)
};

// ---- Transition condition function ----
// Returns true if transition should fire
using TransitionCondition = std::function<bool()>;

// ---- State transition ----
struct AnimTransition {
    AnimClipType from;
    AnimClipType to;
    float        blendTime = 0.15f;    // Crossfade duration in seconds
    int          priority  = 0;        // Higher priority transitions checked first
    TransitionCondition condition;     // When does this transition fire?
};

// ---- Computed animation output ----
struct AnimOutput {
    float walkCycle     = 0.0f;   // Current walk cycle phase (0 to 2PI)
    float limbSwing     = 0.0f;   // Current arm/leg swing angle (degrees)
    float headBobY      = 0.0f;   // Vertical head bob offset
    float headBobX      = 0.0f;   // Horizontal head bob offset
    float blendFactor   = 1.0f;   // 0 = fully previous, 1 = fully current
    AnimClipType activeClip = AnimClipType::Idle;
    float stateTime     = 0.0f;   // Time spent in current state
};

// ---- Animation State Machine ----
class AnimStateMachine {
public:
    void Init();

    // ---- Clip registration ----
    void RegisterClip(const AnimClip& clip);

    // ---- Transition registration ----
    void AddTransition(AnimClipType from, AnimClipType to,
                       TransitionCondition condition,
                       float blendTime = 0.15f, int priority = 0);

    // Add a transition that can fire from ANY state
    void AddAnyStateTransition(AnimClipType to,
                               TransitionCondition condition,
                               float blendTime = 0.15f, int priority = 0);

    // ---- Update ----
    void Update(float dt);

    // ---- Force state (bypass transitions, e.g. for initialization) ----
    void ForceState(AnimClipType clip);

    // ---- Output ----
    const AnimOutput& GetOutput() const { return m_output; }
    AnimClipType GetCurrentState() const { return m_currentClip; }
    AnimClipType GetPreviousState() const { return m_previousClip; }
    float GetStateTime() const { return m_stateTime; }
    bool IsBlending() const { return m_blendTimer > 0.0f; }

    // ---- Debug ----
    const char* GetCurrentStateName() const { return AnimClipTypeName(m_currentClip); }
    int GetTransitionCount() const { return static_cast<int>(m_transitions.size()); }

private:
    const AnimClip* FindClip(AnimClipType type) const;
    void EvaluateTransitions();
    void TransitionTo(AnimClipType newClip, float blendTime);

    // Registered clips
    std::vector<AnimClip> m_clips;

    // Transitions (sorted by priority)
    std::vector<AnimTransition> m_transitions;

    // Current state
    AnimClipType m_currentClip  = AnimClipType::Idle;
    AnimClipType m_previousClip = AnimClipType::Idle;
    float m_stateTime   = 0.0f;    // Time in current state
    float m_walkCycle   = 0.0f;    // Accumulated walk cycle phase
    float m_headBobTimer = 0.0f;

    // Blending
    float m_blendTimer    = 0.0f;  // Remaining blend time
    float m_blendDuration = 0.0f;  // Total blend duration
    float m_prevLimbSwing = 0.0f;  // Previous state's last limb swing
    float m_prevBobY      = 0.0f;
    float m_prevBobX      = 0.0f;

    // Output
    AnimOutput m_output;
};

} // namespace WT
