#include "AIAgent.h"
#include "Graphics/DebugRenderer.h"
#include "Util/Log.h"
#include <cmath>

namespace WT {

// ==================== Init / Shutdown ====================

void AISystem::Init() {
    m_agents.clear();
    m_nextId = 0;
    LOG_INFO("AISystem initialized");
}

void AISystem::Shutdown() {
    m_agents.clear();
    LOG_INFO("AISystem shutdown");
}

// ==================== Agent Management ====================

int AISystem::AddAgent(const std::string& name, const XMFLOAT3& position) {
    AIAgent agent;
    agent.name = name.empty() ? ("Agent_" + std::to_string(m_nextId)) : name;
    agent.position = position;
    agent.homePosition = position;
    m_nextId++;
    m_agents.push_back(agent);
    LOG_INFO("AI Agent added: %s at (%.1f, %.1f, %.1f)",
             agent.name.c_str(), position.x, position.y, position.z);
    return static_cast<int>(m_agents.size()) - 1;
}

void AISystem::RemoveAgent(int index) {
    if (index >= 0 && index < static_cast<int>(m_agents.size())) {
        LOG_INFO("AI Agent removed: %s", m_agents[index].name.c_str());
        m_agents.erase(m_agents.begin() + index);
    }
}

void AISystem::AddPatrolPoint(int agentIndex, const XMFLOAT3& point) {
    if (agentIndex < 0 || agentIndex >= static_cast<int>(m_agents.size())) return;
    m_agents[agentIndex].patrolPoints.push_back(point);
}

void AISystem::ClearPatrolPoints(int agentIndex) {
    if (agentIndex < 0 || agentIndex >= static_cast<int>(m_agents.size())) return;
    m_agents[agentIndex].patrolPoints.clear();
    m_agents[agentIndex].currentPatrolIndex = 0;
}

// ==================== Update ====================

void AISystem::Update(float dt, const NavGrid& navGrid, const XMFLOAT3& playerPos,
                       PhysicsWorld* physics) {
    for (auto& agent : m_agents) {
        if (!agent.active) continue;
        UpdateAgent(dt, agent, navGrid, playerPos, physics);
    }
}

void AISystem::UpdateAgent(float dt, AIAgent& agent, const NavGrid& navGrid,
                            const XMFLOAT3& playerPos, PhysicsWorld* physics) {
    float speed = agent.settings.moveSpeed;

    // Check distance to player (XZ only)
    float dx = playerPos.x - agent.position.x;
    float dz = playerPos.z - agent.position.z;
    float distToPlayer = std::sqrt(dx * dx + dz * dz);

    switch (agent.state) {
        case AIState::Idle: {
            // If has patrol points, start patrolling
            if (!agent.patrolPoints.empty()) {
                agent.state = AIState::Patrol;
                agent.currentPatrolIndex = 0;
                RequestPath(agent, navGrid, agent.patrolPoints[0]);
            }
            // Check for player detection
            if (distToPlayer < agent.settings.detectRange) {
                agent.state = AIState::Chase;
                RequestPath(agent, navGrid, playerPos);
            }
            break;
        }

        case AIState::Patrol: {
            // Check for player detection
            if (distToPlayer < agent.settings.detectRange) {
                agent.state = AIState::Chase;
                RequestPath(agent, navGrid, playerPos);
                break;
            }

            // Move along patrol path
            if (agent.currentPath.empty() || agent.pathIndex >= static_cast<int>(agent.currentPath.size())) {
                // Reached current patrol point, advance to next
                agent.currentPatrolIndex++;
                if (agent.currentPatrolIndex >= static_cast<int>(agent.patrolPoints.size())) {
                    agent.currentPatrolIndex = 0;  // Loop patrol
                }
                if (!agent.patrolPoints.empty()) {
                    RequestPath(agent, navGrid, agent.patrolPoints[agent.currentPatrolIndex]);
                }
            } else {
                MoveAlongPath(dt, agent, speed);
            }
            break;
        }

        case AIState::Chase: {
            speed = agent.settings.chaseSpeed;

            // Lost player? Return home
            if (distToPlayer > agent.settings.loseRange) {
                agent.state = AIState::Return;
                RequestPath(agent, navGrid, agent.homePosition);
                break;
            }

            // Re-path toward player periodically (when near end of path)
            if (agent.currentPath.empty() || agent.pathIndex >= static_cast<int>(agent.currentPath.size()) - 2) {
                RequestPath(agent, navGrid, playerPos);
            }

            MoveAlongPath(dt, agent, speed);
            break;
        }

        case AIState::Return: {
            // Check for player detection while returning
            if (distToPlayer < agent.settings.detectRange) {
                agent.state = AIState::Chase;
                RequestPath(agent, navGrid, playerPos);
                break;
            }

            if (agent.currentPath.empty() || agent.pathIndex >= static_cast<int>(agent.currentPath.size())) {
                // Reached home, go back to idle/patrol
                agent.state = agent.patrolPoints.empty() ? AIState::Idle : AIState::Patrol;
                agent.currentPatrolIndex = 0;
                if (!agent.patrolPoints.empty()) {
                    RequestPath(agent, navGrid, agent.patrolPoints[0]);
                }
            } else {
                MoveAlongPath(dt, agent, speed);
            }
            break;
        }

        default: break;
    }

    // Keep agent on grid Y
    agent.position.y = navGrid.GetGridY();

    // ---- Collision with scene entities ----
    if (physics) {
        float s = agent.settings.bodyScale;
        AABB agentBox = AABB::FromCenterHalf(
            { agent.position.x, agent.position.y + s * 0.5f, agent.position.z },
            { s * 0.5f, s * 0.5f, s * 0.5f }
        );

        for (int iter = 0; iter < 4; iter++) {
            CollisionHit hit = physics->TestAABB(agentBox, -1);
            if (!hit.hit) break;

            // Only resolve horizontal (XZ) — agents stay on grid Y
            float pushDist = hit.depth + 0.001f;
            agent.position.x += hit.normal.x * pushDist;
            agent.position.z += hit.normal.z * pushDist;

            agentBox = AABB::FromCenterHalf(
                { agent.position.x, agent.position.y + s * 0.5f, agent.position.z },
                { s * 0.5f, s * 0.5f, s * 0.5f }
            );
        }
    }
}

void AISystem::MoveAlongPath(float dt, AIAgent& agent, float speed) {
    if (agent.pathIndex >= static_cast<int>(agent.currentPath.size()))
        return;

    const XMFLOAT3& target = agent.currentPath[agent.pathIndex];
    float dx = target.x - agent.position.x;
    float dz = target.z - agent.position.z;
    float dist = std::sqrt(dx * dx + dz * dz);

    if (dist < agent.settings.waypointDist) {
        agent.pathIndex++;
        return;
    }

    // Normalize direction and move
    float invDist = 1.0f / dist;
    float moveAmount = speed * dt;
    if (moveAmount > dist) moveAmount = dist;  // Don't overshoot

    agent.position.x += dx * invDist * moveAmount;
    agent.position.z += dz * invDist * moveAmount;

    // Face movement direction
    FaceToward(agent, target, dt);
}

void AISystem::RequestPath(AIAgent& agent, const NavGrid& navGrid, const XMFLOAT3& target) {
    agent.currentPath = navGrid.FindPathWorld(agent.position, target, true);
    agent.pathIndex = 0;
}

void AISystem::FaceToward(AIAgent& agent, const XMFLOAT3& target, float dt) {
    float dx = target.x - agent.position.x;
    float dz = target.z - agent.position.z;
    if (std::abs(dx) < 0.001f && std::abs(dz) < 0.001f) return;

    float targetYaw = XMConvertToDegrees(std::atan2(dx, dz));

    // Smooth rotation
    float diff = targetYaw - agent.yaw;
    // Wrap to [-180, 180]
    while (diff > 180.0f)  diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;

    float rotSpeed = 360.0f;  // degrees per second
    float maxTurn = rotSpeed * dt;
    if (std::abs(diff) < maxTurn) {
        agent.yaw = targetYaw;
    } else {
        agent.yaw += (diff > 0 ? maxTurn : -maxTurn);
    }
}

// ==================== Debug Drawing ====================

void AISystem::DebugDraw(DebugRenderer& debug, const NavGrid& navGrid) const {
    if (!showDebug) return;

    for (const auto& agent : m_agents) {
        if (!agent.visible) continue;

        float halfScale = agent.settings.bodyScale * 0.5f;
        XMFLOAT3 pos = agent.position;
        pos.y += halfScale;  // Center body above ground

        // Agent body box
        XMFLOAT4 agentColor = {
            agent.settings.bodyColor[0],
            agent.settings.bodyColor[1],
            agent.settings.bodyColor[2],
            0.8f
        };
        debug.DrawBox(pos, { halfScale, halfScale, halfScale }, agentColor);

        // Direction arrow (forward facing)
        float yawRad = XMConvertToRadians(agent.yaw);
        float fwdX = std::sin(yawRad);
        float fwdZ = std::cos(yawRad);
        float arrowLen = agent.settings.bodyScale;
        debug.DrawLine(
            pos,
            { pos.x + fwdX * arrowLen, pos.y, pos.z + fwdZ * arrowLen },
            { 1.0f, 1.0f, 0.0f, 1.0f });

        // State indicator color
        XMFLOAT4 stateColor;
        switch (agent.state) {
            case AIState::Idle:   stateColor = { 0.5f, 0.5f, 0.5f, 1.0f }; break;
            case AIState::Patrol: stateColor = { 0.0f, 0.7f, 1.0f, 1.0f }; break;
            case AIState::Chase:  stateColor = { 1.0f, 0.0f, 0.0f, 1.0f }; break;
            case AIState::Return: stateColor = { 1.0f, 1.0f, 0.0f, 1.0f }; break;
            default:              stateColor = { 1.0f, 1.0f, 1.0f, 1.0f }; break;
        }
        // Small indicator above head
        debug.DrawSphere({ pos.x, pos.y + halfScale + 0.3f, pos.z }, 0.1f, stateColor, 6);

        // Draw detection range circle
        debug.DrawSphere({ agent.position.x, agent.position.y + 0.1f, agent.position.z },
                          agent.settings.detectRange, { 1.0f, 1.0f, 0.0f, 0.15f }, 24);

        // Draw current path
        if (!agent.currentPath.empty() && agent.pathIndex < static_cast<int>(agent.currentPath.size())) {
            float pathY = navGrid.GetGridY() + 0.05f;
            // From agent to next waypoint
            debug.DrawLine(
                { agent.position.x, pathY, agent.position.z },
                { agent.currentPath[agent.pathIndex].x, pathY, agent.currentPath[agent.pathIndex].z },
                { 0.0f, 1.0f, 0.5f, 0.6f });
            // Rest of path
            for (int i = agent.pathIndex; i < static_cast<int>(agent.currentPath.size()) - 1; i++) {
                debug.DrawLine(
                    { agent.currentPath[i].x, pathY, agent.currentPath[i].z },
                    { agent.currentPath[i + 1].x, pathY, agent.currentPath[i + 1].z },
                    { 0.0f, 1.0f, 0.5f, 0.6f });
            }
        }

        // Draw patrol waypoints
        for (size_t i = 0; i < agent.patrolPoints.size(); i++) {
            const auto& wp = agent.patrolPoints[i];
            float wpY = navGrid.GetGridY() + 0.1f;
            debug.DrawSphere({ wp.x, wpY, wp.z }, 0.15f, { 0.0f, 0.5f, 1.0f, 0.7f }, 8);
            // Connect patrol points with lines
            if (i > 0) {
                const auto& prev = agent.patrolPoints[i - 1];
                debug.DrawLine({ prev.x, wpY, prev.z }, { wp.x, wpY, wp.z },
                               { 0.0f, 0.5f, 1.0f, 0.3f });
            }
        }
        // Loop line from last to first
        if (agent.patrolPoints.size() > 1) {
            const auto& first = agent.patrolPoints.front();
            const auto& last  = agent.patrolPoints.back();
            float wpY = navGrid.GetGridY() + 0.1f;
            debug.DrawLine({ last.x, wpY, last.z }, { first.x, wpY, first.z },
                           { 0.0f, 0.5f, 1.0f, 0.3f });
        }
    }
}

} // namespace WT
