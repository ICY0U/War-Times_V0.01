#pragma once

#include <DirectXMath.h>
#include <vector>
#include <string>
#include <cstdint>
#include "NavGrid.h"
#include "Physics/PhysicsWorld.h"

namespace WT {

using namespace DirectX;

// ---- AI Agent behavior state ----
enum class AIState : uint8_t {
    Idle = 0,
    Patrol,
    Chase,
    Return,
    Count
};

inline const char* AIStateName(AIState s) {
    switch (s) {
        case AIState::Idle:   return "Idle";
        case AIState::Patrol: return "Patrol";
        case AIState::Chase:  return "Chase";
        case AIState::Return: return "Return";
        default:              return "Unknown";
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
};

// ---- AI Agent — NPC that navigates the grid ----
struct AIAgent {
    std::string name = "Agent";
    XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    float    yaw = 0.0f;  // Facing direction in degrees

    AIState state = AIState::Idle;
    AIAgentSettings settings;

    // Patrol waypoints (world space)
    std::vector<XMFLOAT3> patrolPoints;
    int currentPatrolIndex = 0;

    // Current path being followed
    std::vector<XMFLOAT3> currentPath;
    int pathIndex = 0;

    // Home position (where agent was spawned, for Return state)
    XMFLOAT3 homePosition = { 0.0f, 0.0f, 0.0f };

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

    // ---- Update all agents ----
    // playerPos: used for chase/detect logic
    // physics: optional, for collision with scene entities
    void Update(float dt, const NavGrid& navGrid, const XMFLOAT3& playerPos,
                PhysicsWorld* physics = nullptr);

    // ---- Debug drawing ----
    void DebugDraw(class DebugRenderer& debug, const NavGrid& navGrid) const;

    bool showDebug = false;

private:
    void UpdateAgent(float dt, AIAgent& agent, const NavGrid& navGrid, const XMFLOAT3& playerPos,
                     PhysicsWorld* physics);
    void MoveAlongPath(float dt, AIAgent& agent, float speed);
    void RequestPath(AIAgent& agent, const NavGrid& navGrid, const XMFLOAT3& target);
    void FaceToward(AIAgent& agent, const XMFLOAT3& target, float dt);

    std::vector<AIAgent> m_agents;
    int m_nextId = 0;
};

} // namespace WT
