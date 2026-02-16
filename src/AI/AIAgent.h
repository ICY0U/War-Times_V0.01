#pragma once

#include <DirectXMath.h>
#include <vector>
#include <string>
#include <cstdint>
#include "NavGrid.h"
#include "Physics/PhysicsWorld.h"

namespace WT {

using namespace DirectX;

// ---- Sound event types for AI hearing ----
enum class SoundType : uint8_t {
    Footstep = 0,   // Quiet — short range
    Gunshot,        // Loud  — large range
    BulletImpact,   // Medium — bullet hitting nearby surface
    Count
};

// ---- A sound event broadcast to the AI system each frame ----
struct SoundEvent {
    XMFLOAT3  position = { 0, 0, 0 };  // World-space origin of the sound
    float     radius   = 0.0f;         // How far the sound carries
    SoundType type     = SoundType::Footstep;
    int       sourceId = -1;           // Entity/player that made the sound (-1 = player)
};

// ---- AI Agent behavior state ----
enum class AIState : uint8_t {
    Idle = 0,
    Patrol,
    WaitAtWaypoint,   // Pausing at a patrol waypoint
    Investigate,      // Heard/saw something, moving to investigate
    Chase,
    TakeCover,        // Under fire — running to cover position
    Return,
    Count
};

inline const char* AIStateName(AIState s) {
    switch (s) {
        case AIState::Idle:            return "Idle";
        case AIState::Patrol:          return "Patrol";
        case AIState::WaitAtWaypoint:  return "Waiting";
        case AIState::Investigate:     return "Investigating";
        case AIState::Chase:           return "Chase";
        case AIState::TakeCover:       return "Taking Cover";
        case AIState::Return:          return "Return";
        default:                       return "Unknown";
    }
}

// ---- AI Agent type ----
enum class AIAgentType : uint8_t {
    Ground = 0,  // Standard ground-based NPC — uses NavGrid
    Drone,       // Flying drone — ignores NavGrid, moves in 3D
    Count
};

inline const char* AIAgentTypeName(AIAgentType t) {
    switch (t) {
        case AIAgentType::Ground: return "Ground";
        case AIAgentType::Drone:  return "Drone";
        default:                  return "Unknown";
    }
}

// ---- Patrol mode ----
enum class PatrolMode : uint8_t {
    Loop = 0,       // A → B → C → A → B → ...
    PingPong,       // A → B → C → B → A → B → ...
    Random,         // Pick random waypoint
    AreaRoam,       // Wander randomly within a radius of home
    Count
};

inline const char* PatrolModeName(PatrolMode m) {
    switch (m) {
        case PatrolMode::Loop:      return "Loop";
        case PatrolMode::PingPong:  return "Ping-Pong";
        case PatrolMode::Random:    return "Random";
        case PatrolMode::AreaRoam:  return "Area Roam";
        default:                    return "Unknown";
    }
}

// ---- AI Agent settings ----
struct AIAgentSettings {
    float moveSpeed    = 3.0f;
    float chaseSpeed   = 5.0f;
    float detectRange  = 10.0f;   // Distance to detect player
    float loseRange    = 15.0f;   // Distance to lose player
    float waypointDist = 0.3f;    // How close to get to a waypoint before advancing
    float bodyScale    = 0.8f;    // Visual scale relative to 1-unit cube
    float bodyColor[4] = { 0.7f, 0.2f, 0.2f, 1.0f };   // Red tint

    // Field of view (degrees) — player must be within this cone to be detected
    float fovAngle     = 120.0f;  // Total FOV cone angle
    bool  requireLOS   = true;    // Require physics line-of-sight for detection

    // Patrol behavior
    PatrolMode patrolMode  = PatrolMode::Loop;
    float waypointWaitMin  = 1.0f;   // Min seconds to pause at each waypoint
    float waypointWaitMax  = 3.0f;   // Max seconds to pause
    float areaRoamRadius   = 10.0f;  // Radius for AreaRoam mode
    bool  lookAroundAtWait = true;   // Randomly look around when waiting

    // Re-path interval during chase (seconds)
    float chaseRepathInterval = 0.5f;

    // Steering — agent-to-agent avoidance
    float avoidRadius  = 1.5f;   // Separation distance from other agents
    float avoidForce   = 4.0f;   // Strength of avoidance push

    // Hearing — sound detection ranges
    float hearFootstepRange = 5.0f;    // Max range to hear footsteps
    float hearGunshotRange  = 30.0f;   // Max range to hear gunshots
    float hearImpactRange   = 15.0f;   // Max range to hear bullet impacts

    // Cover behavior
    float coverSearchRadius = 8.0f;    // How far to search for cover (grid cells)
    float coverMinDist      = 2.0f;    // Minimum distance from threat for valid cover
    float coverStayTime     = 3.0f;    // How long to stay in cover before peeking
    bool  seekCoverOnDamage = true;    // Auto-seek cover when taking damage
    bool  seekCoverOnGunfire = true;   // Seek cover when hearing nearby gunfire (not hit)
    float coverRelocateTime = 2.0f;    // If still taking damage in cover, relocate after this many seconds

    // Cover advanced
    float coverPeekInterval   = 2.5f;  // Time between peek attempts while in cover
    float coverSuppressionMax = 3.0f;  // Suppression level that prevents peeking
    float coverFlankAngle     = 100.0f; // Degrees — if player moves this far from original cover angle, cover is compromised

    // Drone-specific settings (only used when type == Drone)
    float droneHoverHeight    = 4.0f;    // Altitude above ground
    float droneBobAmplitude   = 0.3f;    // Vertical bob amount
    float droneBobSpeed       = 2.0f;    // Bob oscillation speed
    float droneOrbitRadius    = 5.0f;    // Orbit radius around patrol/home point
    float droneOrbitSpeed     = 1.0f;    // Orbit angular speed (rad/s)
    float droneChaseSpeed     = 3.5f;    // Chase speed (slower than ground agents — user request)
    float droneClimbSpeed     = 4.0f;    // Vertical climb rate when avoiding obstacles
    float droneDiveSpeed      = 3.0f;    // Vertical dive rate when going under obstacles
    float droneMaxPitch       = 25.0f;   // Max forward tilt (degrees) during movement
    float droneMaxRoll        = 30.0f;   // Max bank angle (degrees) during turns
    float droneObstacleDist   = 4.0f;    // Lookahead distance for obstacle avoidance raycasts
    float droneMinAltitude    = 1.0f;    // Minimum flight altitude (above ground)
    float droneMaxAltitude    = 8.0f;    // Maximum flight altitude
    float droneDownwashRate   = 0.1f;    // Seconds between downwash particle bursts
};

// ---- AI Agent — NPC that navigates the grid ----
struct AIAgent {
    std::string name = "Agent";
    XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    float    yaw = 0.0f;  // Facing direction in degrees

    AIAgentType type = AIAgentType::Ground;
    AIState state = AIState::Idle;
    AIAgentSettings settings;

    // Patrol waypoints (world space)
    std::vector<XMFLOAT3> patrolPoints;
    int currentPatrolIndex = 0;
    int patrolDirection    = 1;          // +1 forward, -1 backward (for PingPong)

    // Wait state
    float waitTimer     = 0.0f;          // Countdown at waypoint
    float lookTimer     = 0.0f;          // Time until next random look
    float targetLookYaw = 0.0f;          // Yaw to look toward while waiting

    // Investigation
    XMFLOAT3 investigatePos = { 0, 0, 0 };
    float investigateTimer  = 0.0f;      // Time spent investigating

    // Current path being followed
    std::vector<XMFLOAT3> currentPath;
    int pathIndex = 0;

    // Home position (where agent was spawned, for Return state)
    XMFLOAT3 homePosition = { 0.0f, 0.0f, 0.0f };

    // Re-path timer (for periodic re-pathing during chase)
    float repathTimer = 0.0f;

    // Line of sight tracking
    bool  canSeePlayer = false;   // True if last LOS check succeeded
    float losCheckTimer = 0.0f;   // Countdown to next LOS check

    // Sound hearing
    XMFLOAT3 lastHeardSoundPos = { 0, 0, 0 };
    float    soundAlertTimer   = 0.0f;    // Cooldown between sound reactions

    // Cover
    XMFLOAT3 coverPos     = { 0, 0, 0 };   // Position of cover destination
    XMFLOAT3 threatPos    = { 0, 0, 0 };   // Where the threat is coming from
    float    coverTimer   = 0.0f;           // Time spent in cover
    float    coverDamageTimer = 0.0f;       // Time taking damage while in cover (triggers relocate)
    bool     inCover      = false;          // Currently at cover position
    bool     wasRecentlyShot = false;       // Set when taking damage
    float    recentlyShotTimer = 0.0f;      // Decay timer for was-shot flag
    float    coverPeekTimer   = 0.0f;       // Timer until next peek attempt
    float    coverSuppressionLevel = 0.0f;  // Builds up from incoming fire, prevents peeking
    float    coverThreatYaw   = 0.0f;       // Yaw toward threat when cover was taken (for flank detection)

    // Drone flight state (only used when type == Drone)
    float droneBobPhase     = 0.0f;     // Current bob oscillation phase
    float droneOrbitAngle   = 0.0f;     // Current orbit angle (radians)
    XMFLOAT3 droneOrbitCenter = { 0, 0, 0 };  // Center point being orbited
    float dronePitch        = 0.0f;     // Forward/back tilt (degrees, + = nose down)
    float droneRoll         = 0.0f;     // Side bank (degrees, + = right)
    float droneTargetAlt    = 4.0f;     // Desired altitude (dynamically adjusted for obstacles)
    float droneVerticalVel  = 0.0f;     // Current vertical velocity for smooth altitude
    float droneDownwashTimer = 0.0f;    // Timer for downwash particle emission
    float droneSpeedCurrent = 0.0f;     // Smoothed current horizontal speed (for tilt)

    bool active  = true;
    bool visible = true;

    // Health
    float health    = 100.0f;
    float maxHealth = 100.0f;
    bool  alive     = true;

    // Damage flash (visual feedback)
    float damageFlashTimer = 0.0f;

    // Apply damage — returns true if agent died
    bool TakeDamage(float amount) {
        if (!alive) return false;
        health -= amount;
        damageFlashTimer = 0.15f;
        wasRecentlyShot = true;
        recentlyShotTimer = 1.0f;   // Flag stays active for 1 second
        if (health <= 0.0f) {
            health = 0.0f;
            alive = false;
            active = false;
            return true;
        }
        return false;
    }
};

// ---- AI System — manages all agents and updates them ----
class AISystem {
public:
    void Init();
    void Shutdown();

    // ---- Agent management ----
    int  AddAgent(const std::string& name, const XMFLOAT3& position);
    void RemoveAgent(int index);
    int  GetAgentCount() const { return static_cast<int>(m_agents.size()); }
    AIAgent& GetAgent(int index) { return m_agents[index]; }
    const AIAgent& GetAgent(int index) const { return m_agents[index]; }
    std::vector<AIAgent>& GetAgents() { return m_agents; }
    const std::vector<AIAgent>& GetAgents() const { return m_agents; }

    // ---- Add patrol waypoint to an agent ----
    void AddPatrolPoint(int agentIndex, const XMFLOAT3& point);
    void ClearPatrolPoints(int agentIndex);

    // ---- Sound events ----
    // Call these from game code to notify AI of sounds
    void PostSoundEvent(const SoundEvent& evt);
    void PostGunshot(const XMFLOAT3& position, float radius = 30.0f, int sourceId = -1);
    void PostFootstep(const XMFLOAT3& position, float radius = 5.0f, int sourceId = -1);
    void PostBulletImpact(const XMFLOAT3& position, float radius = 15.0f);

    // ---- Update all agents ----
    // playerPos: used for chase/detect logic
    // physics: optional, for collision with scene entities and LOS raycasts
    void Update(float dt, const NavGrid& navGrid, const XMFLOAT3& playerPos,
                PhysicsWorld* physics = nullptr);

    // ---- Debug drawing ----
    void DebugDraw(class DebugRenderer& debug, const NavGrid& navGrid) const;

    bool showDebug = false;

private:
    void UpdateAgent(float dt, AIAgent& agent, const NavGrid& navGrid, const XMFLOAT3& playerPos,
                     PhysicsWorld* physics);
    void UpdateDrone(float dt, AIAgent& agent, const NavGrid& navGrid, const XMFLOAT3& playerPos,
                     PhysicsWorld* physics);
    void MoveAlongPath(float dt, AIAgent& agent, float speed);
    void MoveDroneToward(float dt, AIAgent& agent, const XMFLOAT3& target, float speed);
    void RequestPath(AIAgent& agent, const NavGrid& navGrid, const XMFLOAT3& target);
    void FaceToward(AIAgent& agent, const XMFLOAT3& target, float dt);
    void AdvancePatrolIndex(AIAgent& agent, const NavGrid& navGrid);

    // Check if agent has clear LOS to target (physics raycast)
    bool HasLineOfSight(const AIAgent& agent, const XMFLOAT3& target, PhysicsWorld* physics) const;

    // Check if target is within agent's FOV cone
    bool IsInFieldOfView(const AIAgent& agent, const XMFLOAT3& target) const;

    // Apply separation steering from other agents
    void ApplyAgentAvoidance(float dt, AIAgent& agent);

    // Sound hearing — check events against agent and react
    void ProcessSoundEvents(AIAgent& agent, const NavGrid& navGrid, PhysicsWorld* physics);

    // Cover system
    bool FindCoverPosition(const AIAgent& agent, const XMFLOAT3& threatPos,
                           const NavGrid& navGrid, PhysicsWorld* physics,
                           XMFLOAT3& outCoverPos) const;

    std::vector<AIAgent> m_agents;
    std::vector<SoundEvent> m_pendingSounds;   // Cleared each frame after processing
    int m_nextId = 0;
};

} // namespace WT
