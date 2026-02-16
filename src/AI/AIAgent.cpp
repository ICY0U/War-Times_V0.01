#include "AIAgent.h"
#include "Graphics/DebugRenderer.h"
#include "Util/Log.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace WT {

static float RandFloat(float lo, float hi) {
    return lo + static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * (hi - lo);
}

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
        // Process pending sound events for this agent
        ProcessSoundEvents(agent, navGrid, physics);
        // Dispatch to correct update based on type
        if (agent.type == AIAgentType::Drone) {
            UpdateDrone(dt, agent, navGrid, playerPos, physics);
        } else {
            UpdateAgent(dt, agent, navGrid, playerPos, physics);
        }
    }
    // Clear sound events after all agents have processed them
    m_pendingSounds.clear();
}

void AISystem::UpdateAgent(float dt, AIAgent& agent, const NavGrid& navGrid,
                            const XMFLOAT3& playerPos, PhysicsWorld* physics) {
    float speed = agent.settings.moveSpeed;

    // Check distance to player (XZ only)
    float dx = playerPos.x - agent.position.x;
    float dz = playerPos.z - agent.position.z;
    float distToPlayer = std::sqrt(dx * dx + dz * dz);

    // Damage flash decay
    if (agent.damageFlashTimer > 0.0f) agent.damageFlashTimer -= dt;

    // Recently-shot timer decay
    if (agent.recentlyShotTimer > 0.0f) {
        agent.recentlyShotTimer -= dt;
        if (agent.recentlyShotTimer <= 0.0f)
            agent.wasRecentlyShot = false;
    }

    // Sound alert cooldown
    if (agent.soundAlertTimer > 0.0f) agent.soundAlertTimer -= dt;

    // Periodic LOS check (every 0.15s to avoid per-frame raycasts)
    agent.losCheckTimer -= dt;
    if (agent.losCheckTimer <= 0.0f) {
        agent.losCheckTimer = 0.15f;
        if (distToPlayer < agent.settings.loseRange) {
            bool inFOV = IsInFieldOfView(agent, playerPos);
            bool hasLOS = !agent.settings.requireLOS || HasLineOfSight(agent, playerPos, physics);
            agent.canSeePlayer = inFOV && hasLOS;
        } else {
            agent.canSeePlayer = false;
        }
    }

    // Helper: can the agent detect the player right now?
    auto canDetectPlayer = [&]() -> bool {
        return distToPlayer < agent.settings.detectRange && agent.canSeePlayer;
    };

    // ---- Damage reaction: seek cover when shot at (any state except already in cover) ----
    if (agent.wasRecentlyShot && agent.settings.seekCoverOnDamage &&
        agent.state != AIState::TakeCover) {
        XMFLOAT3 coverTarget;
        if (FindCoverPosition(agent, playerPos, navGrid, physics, coverTarget)) {
            agent.state = AIState::TakeCover;
            agent.coverPos = coverTarget;
            agent.threatPos = playerPos;
            agent.coverTimer = 0.0f;
            agent.inCover = false;
            agent.wasRecentlyShot = false;
            RequestPath(agent, navGrid, coverTarget);
            // Skip the rest of the state machine this frame
            goto afterStateMachine;
        }
        // If no cover found, fall through to normal state behavior
    }

    switch (agent.state) {
        case AIState::Idle: {
            // If has patrol points or AreaRoam, start patrolling
            if (!agent.patrolPoints.empty() || agent.settings.patrolMode == PatrolMode::AreaRoam) {
                agent.state = AIState::Patrol;
                agent.currentPatrolIndex = 0;
                agent.patrolDirection = 1;
                if (agent.settings.patrolMode == PatrolMode::AreaRoam) {
                    float r = agent.settings.areaRoamRadius;
                    XMFLOAT3 target = {
                        agent.homePosition.x + RandFloat(-r, r),
                        agent.homePosition.y,
                        agent.homePosition.z + RandFloat(-r, r)
                    };
                    RequestPath(agent, navGrid, target);
                } else if (!agent.patrolPoints.empty()) {
                    RequestPath(agent, navGrid, agent.patrolPoints[0]);
                }
            }
            // Check for player detection with LOS
            if (canDetectPlayer()) {
                agent.state = AIState::Chase;
                agent.repathTimer = 0.0f;
                RequestPath(agent, navGrid, playerPos);
            }
            break;
        }

        case AIState::Patrol: {
            if (canDetectPlayer()) {
                agent.state = AIState::Chase;
                agent.repathTimer = 0.0f;
                RequestPath(agent, navGrid, playerPos);
                break;
            }

            if (agent.currentPath.empty() || agent.pathIndex >= static_cast<int>(agent.currentPath.size())) {
                agent.state = AIState::WaitAtWaypoint;
                agent.waitTimer = RandFloat(agent.settings.waypointWaitMin, agent.settings.waypointWaitMax);
                agent.lookTimer = RandFloat(0.5f, 1.5f);
                agent.targetLookYaw = agent.yaw;
            } else {
                MoveAlongPath(dt, agent, speed);
            }
            break;
        }

        case AIState::WaitAtWaypoint: {
            if (canDetectPlayer()) {
                agent.state = AIState::Chase;
                agent.repathTimer = 0.0f;
                RequestPath(agent, navGrid, playerPos);
                break;
            }

            if (agent.settings.lookAroundAtWait) {
                agent.lookTimer -= dt;
                if (agent.lookTimer <= 0.0f) {
                    agent.targetLookYaw = agent.yaw + RandFloat(-90.0f, 90.0f);
                    agent.lookTimer = RandFloat(1.0f, 3.0f);
                }
                float diff = agent.targetLookYaw - agent.yaw;
                while (diff > 180.0f)  diff -= 360.0f;
                while (diff < -180.0f) diff += 360.0f;
                float rotSpeed = 120.0f * dt;
                if (std::abs(diff) < rotSpeed) agent.yaw = agent.targetLookYaw;
                else agent.yaw += (diff > 0 ? rotSpeed : -rotSpeed);
            }

            agent.waitTimer -= dt;
            if (agent.waitTimer <= 0.0f) {
                AdvancePatrolIndex(agent, navGrid);
                agent.state = AIState::Patrol;
            }
            break;
        }

        case AIState::Investigate: {
            if (canDetectPlayer()) {
                agent.state = AIState::Chase;
                agent.repathTimer = 0.0f;
                RequestPath(agent, navGrid, playerPos);
                break;
            }

            if (agent.currentPath.empty() || agent.pathIndex >= static_cast<int>(agent.currentPath.size())) {
                agent.investigateTimer -= dt;
                if (agent.investigateTimer <= 0.0f) {
                    agent.state = agent.patrolPoints.empty() && agent.settings.patrolMode != PatrolMode::AreaRoam
                        ? AIState::Return : AIState::Patrol;
                    if (agent.state == AIState::Patrol) {
                        AdvancePatrolIndex(agent, navGrid);
                    } else {
                        RequestPath(agent, navGrid, agent.homePosition);
                    }
                } else {
                    agent.lookTimer -= dt;
                    if (agent.lookTimer <= 0.0f) {
                        agent.targetLookYaw = agent.yaw + RandFloat(-120.0f, 120.0f);
                        agent.lookTimer = RandFloat(0.8f, 2.0f);
                    }
                    float diff = agent.targetLookYaw - agent.yaw;
                    while (diff > 180.0f)  diff -= 360.0f;
                    while (diff < -180.0f) diff += 360.0f;
                    float rotSpeed = 150.0f * dt;
                    if (std::abs(diff) < rotSpeed) agent.yaw = agent.targetLookYaw;
                    else agent.yaw += (diff > 0 ? rotSpeed : -rotSpeed);
                }
            } else {
                MoveAlongPath(dt, agent, speed);
            }
            break;
        }

        case AIState::Chase: {
            speed = agent.settings.chaseSpeed;

            // Lost player? (out of range OR lost LOS for sustained period)
            if (distToPlayer > agent.settings.loseRange || !agent.canSeePlayer) {
                // If we can't see them, investigate their last known position
                if (!agent.canSeePlayer && distToPlayer <= agent.settings.loseRange) {
                    agent.state = AIState::Investigate;
                    agent.investigatePos = playerPos;
                    agent.investigateTimer = 3.0f;
                    RequestPath(agent, navGrid, playerPos);
                } else {
                    agent.state = AIState::Return;
                    RequestPath(agent, navGrid, agent.homePosition);
                }
                break;
            }

            // Periodic re-path toward player
            agent.repathTimer -= dt;
            if (agent.repathTimer <= 0.0f) {
                agent.repathTimer = agent.settings.chaseRepathInterval;
                RequestPath(agent, navGrid, playerPos);
            }

            MoveAlongPath(dt, agent, speed);

            // Face the player directly during chase (not just path waypoint)
            FaceToward(agent, playerPos, dt);
            break;
        }

        case AIState::TakeCover: {
            speed = agent.settings.chaseSpeed;  // Run to cover fast

            if (!agent.inCover) {
                // Moving toward cover position
                if (agent.currentPath.empty() || agent.pathIndex >= static_cast<int>(agent.currentPath.size())) {
                    // Arrived at cover
                    agent.inCover = true;
                    agent.coverTimer = agent.settings.coverStayTime;
                    agent.coverDamageTimer = 0.0f;
                    agent.coverPeekTimer = agent.settings.coverPeekInterval;
                    agent.coverSuppressionLevel = 0.0f;
                    // Record threat direction for flank detection
                    float tdx = agent.threatPos.x - agent.position.x;
                    float tdz = agent.threatPos.z - agent.position.z;
                    agent.coverThreatYaw = XMConvertToDegrees(std::atan2(tdx, tdz));
                    // Face toward the threat
                    FaceToward(agent, agent.threatPos, dt);
                } else {
                    MoveAlongPath(dt, agent, speed);
                }
            } else {
                // ---- In cover: suppression, peeking, flank detection ----
                agent.coverTimer -= dt;

                // Keep facing the threat
                FaceToward(agent, agent.threatPos, dt);

                // Suppression: builds up when taking fire, decays when safe
                if (agent.wasRecentlyShot) {
                    agent.coverSuppressionLevel += dt * 3.0f;  // Build fast
                    if (agent.coverSuppressionLevel > agent.settings.coverSuppressionMax)
                        agent.coverSuppressionLevel = agent.settings.coverSuppressionMax;
                    agent.coverDamageTimer += dt;
                    agent.wasRecentlyShot = false;

                    // Relocate if taking sustained damage in cover
                    if (agent.coverDamageTimer >= agent.settings.coverRelocateTime) {
                        XMFLOAT3 newCover;
                        if (FindCoverPosition(agent, playerPos, navGrid, physics, newCover)) {
                            agent.coverPos = newCover;
                            agent.inCover = false;
                            agent.coverDamageTimer = 0.0f;
                            agent.coverSuppressionLevel = 0.0f;
                            RequestPath(agent, navGrid, newCover);
                            break;
                        }
                        agent.coverDamageTimer = 0.0f;
                    }
                } else {
                    // Not being shot — decay timers
                    agent.coverDamageTimer = (std::max)(0.0f, agent.coverDamageTimer - dt);
                    agent.coverSuppressionLevel = (std::max)(0.0f, agent.coverSuppressionLevel - dt * 1.5f);
                }

                // Flank detection: if player has moved far from the original threat
                // direction, our cover is compromised — find new cover
                {
                    float fdx = playerPos.x - agent.position.x;
                    float fdz = playerPos.z - agent.position.z;
                    float currentThreatYaw = XMConvertToDegrees(std::atan2(fdx, fdz));
                    float flankDiff = currentThreatYaw - agent.coverThreatYaw;
                    while (flankDiff > 180.0f)  flankDiff -= 360.0f;
                    while (flankDiff < -180.0f) flankDiff += 360.0f;
                    if (std::abs(flankDiff) > agent.settings.coverFlankAngle) {
                        // Player has flanked us — our cover is useless
                        XMFLOAT3 newCover;
                        if (FindCoverPosition(agent, playerPos, navGrid, physics, newCover)) {
                            agent.coverPos = newCover;
                            agent.threatPos = playerPos;
                            agent.inCover = false;
                            agent.coverDamageTimer = 0.0f;
                            agent.coverSuppressionLevel = 0.0f;
                            RequestPath(agent, navGrid, newCover);
                            break;
                        }
                        // No new cover — break to chase
                        agent.state = AIState::Chase;
                        agent.repathTimer = 0.0f;
                        agent.inCover = false;
                        RequestPath(agent, navGrid, playerPos);
                        break;
                    }
                }

                // Periodic peek: when not suppressed, peek out to check for player
                if (agent.coverSuppressionLevel < 0.5f) {
                    agent.coverPeekTimer -= dt;
                    if (agent.coverPeekTimer <= 0.0f) {
                        agent.coverPeekTimer = agent.settings.coverPeekInterval;
                        // Peek — temporarily check LOS/detection
                        if (canDetectPlayer()) {
                            // Spotted the player while peeking — engage!
                            agent.state = AIState::Chase;
                            agent.repathTimer = 0.0f;
                            agent.inCover = false;
                            RequestPath(agent, navGrid, playerPos);
                            break;
                        }
                        // Update threat position if we can't see them
                        agent.threatPos = playerPos;
                        float tdx = playerPos.x - agent.position.x;
                        float tdz = playerPos.z - agent.position.z;
                        agent.coverThreatYaw = XMConvertToDegrees(std::atan2(tdx, tdz));
                    }
                }

                if (agent.coverTimer <= 0.0f) {
                    // Done hiding — peek out and decide
                    if (canDetectPlayer()) {
                        agent.state = AIState::Chase;
                        agent.repathTimer = 0.0f;
                        RequestPath(agent, navGrid, playerPos);
                    } else {
                        // Can't see player, investigate last threat
                        agent.state = AIState::Investigate;
                        agent.investigatePos = agent.threatPos;
                        agent.investigateTimer = 3.0f;
                        RequestPath(agent, navGrid, agent.threatPos);
                    }
                    agent.inCover = false;
                }
            }
            break;
        }

        case AIState::Return: {
            if (canDetectPlayer()) {
                agent.state = AIState::Chase;
                agent.repathTimer = 0.0f;
                RequestPath(agent, navGrid, playerPos);
                break;
            }

            if (agent.currentPath.empty() || agent.pathIndex >= static_cast<int>(agent.currentPath.size())) {
                agent.state = (agent.patrolPoints.empty() && agent.settings.patrolMode != PatrolMode::AreaRoam)
                    ? AIState::Idle : AIState::Patrol;
                agent.currentPatrolIndex = 0;
                agent.patrolDirection = 1;
                if (agent.state == AIState::Patrol) {
                    AdvancePatrolIndex(agent, navGrid);
                }
            } else {
                MoveAlongPath(dt, agent, speed);
            }
            break;
        }

        default: break;
    }

    afterStateMachine:

    // Keep agent on grid Y
    agent.position.y = navGrid.GetGridY();

    // ---- Agent-to-agent avoidance ----
    ApplyAgentAvoidance(dt, agent);

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

// ==================== Drone AI Update ====================

void AISystem::UpdateDrone(float dt, AIAgent& agent, const NavGrid& navGrid,
                            const XMFLOAT3& playerPos, PhysicsWorld* physics) {
    float speed = agent.settings.moveSpeed;
    const float groundY = navGrid.GetGridY();

    // Distance to player (full 3D for drones)
    float dx = playerPos.x - agent.position.x;
    float dy = playerPos.y - agent.position.y;
    float dz = playerPos.z - agent.position.z;
    float distToPlayer = std::sqrt(dx * dx + dy * dy + dz * dz);

    // Damage flash decay
    if (agent.damageFlashTimer > 0.0f) agent.damageFlashTimer -= dt;
    if (agent.recentlyShotTimer > 0.0f) {
        agent.recentlyShotTimer -= dt;
        if (agent.recentlyShotTimer <= 0.0f) agent.wasRecentlyShot = false;
    }
    if (agent.soundAlertTimer > 0.0f) agent.soundAlertTimer -= dt;

    // ---- Bob hover effect ----
    agent.droneBobPhase += agent.settings.droneBobSpeed * dt;
    if (agent.droneBobPhase > 6.2831853f) agent.droneBobPhase -= 6.2831853f;
    float bobOffset = std::sin(agent.droneBobPhase) * agent.settings.droneBobAmplitude;

    // ---- Obstacle avoidance — raycast ahead and above/below ----
    // Adjusts droneTargetAlt dynamically to fly over or under obstacles
    if (physics) {
        float lookDist = agent.settings.droneObstacleDist;
        float yawRad = XMConvertToRadians(agent.yaw);
        float fwdX = std::sin(yawRad);
        float fwdZ = std::cos(yawRad);
        XMFLOAT3 fwd = { fwdX, 0.0f, fwdZ };
        XMFLOAT3 rayOrig = agent.position;

        // Forward raycast at current altitude
        CollisionHit fwdHit = physics->Raycast(rayOrig, fwd, lookDist);
        if (fwdHit.hit) {
            // Something ahead — decide: go over or under?
            // Check if there's clearance above the obstacle
            float checkAbove = agent.position.y + 2.0f;
            XMFLOAT3 aboveOrigin = { rayOrig.x, checkAbove, rayOrig.z };
            CollisionHit aboveHit = physics->Raycast(aboveOrigin, fwd, lookDist);

            // Check if there's clearance below (but above minimum altitude)
            float checkBelow = (std::max)(groundY + agent.settings.droneMinAltitude,
                                          agent.position.y - 2.0f);
            XMFLOAT3 belowOrigin = { rayOrig.x, checkBelow, rayOrig.z };
            CollisionHit belowHit = physics->Raycast(belowOrigin, fwd, lookDist);

            if (!aboveHit.hit) {
                // Clearance above — climb over
                agent.droneTargetAlt = (std::min)(agent.droneTargetAlt + agent.settings.droneClimbSpeed * dt,
                                                   agent.settings.droneMaxAltitude);
            } else if (!belowHit.hit && checkBelow > groundY + agent.settings.droneMinAltitude) {
                // Clearance below — dive under
                agent.droneTargetAlt = (std::max)(agent.droneTargetAlt - agent.settings.droneDiveSpeed * dt,
                                                   agent.settings.droneMinAltitude);
            } else {
                // Blocked everywhere — climb as high as possible
                agent.droneTargetAlt = (std::min)(agent.droneTargetAlt + agent.settings.droneClimbSpeed * dt,
                                                   agent.settings.droneMaxAltitude);
            }
        } else {
            // Nothing ahead — return toward default hover height
            float defaultAlt = agent.settings.droneHoverHeight;
            float altDiff = defaultAlt - agent.droneTargetAlt;
            if (std::abs(altDiff) > 0.1f) {
                agent.droneTargetAlt += altDiff * 2.0f * dt;  // Smooth return
            }
        }

        // Collision resolution — push drone out of entities if overlapping
        float hs = agent.settings.bodyScale * 0.5f;
        AABB droneBox = AABB::FromCenterHalf(agent.position, { hs, hs * 0.25f, hs });
        for (int iter = 0; iter < 3; iter++) {
            CollisionHit boxHit = physics->TestAABB(droneBox, -1);
            if (!boxHit.hit) break;
            float push = boxHit.depth + 0.01f;
            agent.position.x += boxHit.normal.x * push;
            agent.position.y += boxHit.normal.y * push;
            agent.position.z += boxHit.normal.z * push;
            droneBox = AABB::FromCenterHalf(agent.position, { hs, hs * 0.25f, hs });
        }
    }

    // ---- Smooth altitude tracking ----
    float desiredY = groundY + agent.droneTargetAlt + bobOffset;
    float altError = desiredY - agent.position.y;
    // Smooth vertical velocity (spring-damper)
    agent.droneVerticalVel += altError * 8.0f * dt;  // Spring
    agent.droneVerticalVel *= (1.0f - 3.0f * dt);    // Damping
    agent.position.y += agent.droneVerticalVel * dt;
    // Clamp altitude
    float minY = groundY + agent.settings.droneMinAltitude;
    float maxY = groundY + agent.settings.droneMaxAltitude + agent.settings.droneBobAmplitude;
    if (agent.position.y < minY) { agent.position.y = minY; agent.droneVerticalVel = 0.0f; }
    if (agent.position.y > maxY) { agent.position.y = maxY; agent.droneVerticalVel = 0.0f; }

    // ---- LOS check (drones have 360° FOV by default) ----
    agent.losCheckTimer -= dt;
    if (agent.losCheckTimer <= 0.0f) {
        agent.losCheckTimer = 0.15f;
        if (distToPlayer < agent.settings.loseRange) {
            bool hasLOS = !agent.settings.requireLOS || HasLineOfSight(agent, playerPos, physics);
            agent.canSeePlayer = hasLOS;  // 360° vision for drones
        } else {
            agent.canSeePlayer = false;
        }
    }

    auto canDetectPlayer = [&]() -> bool {
        return distToPlayer < agent.settings.detectRange && agent.canSeePlayer;
    };

    // ---- Damage reaction: drones gain altitude and investigate when shot ----
    if (agent.wasRecentlyShot && agent.state != AIState::Chase) {
        agent.wasRecentlyShot = false;
        agent.state = AIState::Investigate;
        agent.investigatePos = playerPos;
        agent.investigateTimer = 4.0f;
        // Jolt upward to evade
        agent.droneTargetAlt = (std::min)(agent.droneTargetAlt + 2.0f, agent.settings.droneMaxAltitude);
        agent.droneVerticalVel += 4.0f;
    }

    // ---- Horizontal speed tracking for tilt ----
    float prevX = agent.position.x;
    float prevZ = agent.position.z;

    switch (agent.state) {
        case AIState::Idle: {
            if (!agent.patrolPoints.empty() || agent.settings.patrolMode == PatrolMode::AreaRoam) {
                agent.state = AIState::Patrol;
                agent.droneOrbitCenter = agent.homePosition;
                agent.droneOrbitAngle = 0.0f;
            }
            if (canDetectPlayer()) {
                agent.state = AIState::Chase;
                agent.repathTimer = 0.0f;
            }
            break;
        }

        case AIState::Patrol: {
            if (canDetectPlayer()) {
                agent.state = AIState::Chase;
                agent.repathTimer = 0.0f;
                break;
            }

            if (!agent.patrolPoints.empty()) {
                XMFLOAT3 target = agent.patrolPoints[agent.currentPatrolIndex];
                target.y = agent.position.y;  // Keep current altitude
                float pdx = target.x - agent.position.x;
                float pdz = target.z - agent.position.z;
                float pDist = std::sqrt(pdx * pdx + pdz * pdz);

                if (pDist < 1.0f) {
                    agent.state = AIState::WaitAtWaypoint;
                    agent.waitTimer = RandFloat(agent.settings.waypointWaitMin, agent.settings.waypointWaitMax);
                    agent.droneOrbitCenter = target;
                } else {
                    MoveDroneToward(dt, agent, target, speed);
                }
            } else {
                // Area roam — orbit around home
                agent.droneOrbitAngle += agent.settings.droneOrbitSpeed * dt;
                float orbitR = agent.settings.droneOrbitRadius;
                XMFLOAT3 orbitTarget = {
                    agent.homePosition.x + std::cos(agent.droneOrbitAngle) * orbitR,
                    agent.position.y,
                    agent.homePosition.z + std::sin(agent.droneOrbitAngle) * orbitR
                };
                MoveDroneToward(dt, agent, orbitTarget, speed);
            }
            break;
        }

        case AIState::WaitAtWaypoint: {
            if (canDetectPlayer()) {
                agent.state = AIState::Chase;
                agent.repathTimer = 0.0f;
                break;
            }

            // Orbit around current waypoint while waiting
            agent.droneOrbitAngle += agent.settings.droneOrbitSpeed * dt;
            float orbitR = agent.settings.droneOrbitRadius * 0.5f;
            XMFLOAT3 orbitPos = {
                agent.droneOrbitCenter.x + std::cos(agent.droneOrbitAngle) * orbitR,
                agent.position.y,
                agent.droneOrbitCenter.z + std::sin(agent.droneOrbitAngle) * orbitR
            };
            MoveDroneToward(dt, agent, orbitPos, speed * 0.6f);

            agent.waitTimer -= dt;
            if (agent.waitTimer <= 0.0f) {
                if (!agent.patrolPoints.empty()) {
                    agent.currentPatrolIndex++;
                    if (agent.currentPatrolIndex >= static_cast<int>(agent.patrolPoints.size()))
                        agent.currentPatrolIndex = 0;
                }
                agent.state = AIState::Patrol;
            }
            break;
        }

        case AIState::Investigate: {
            if (canDetectPlayer()) {
                agent.state = AIState::Chase;
                agent.repathTimer = 0.0f;
                break;
            }

            XMFLOAT3 target = agent.investigatePos;
            target.y = agent.position.y;
            float idst = std::sqrt(
                (target.x - agent.position.x) * (target.x - agent.position.x) +
                (target.z - agent.position.z) * (target.z - agent.position.z));

            if (idst < 2.0f) {
                // At investigation point — orbit and scan
                agent.investigateTimer -= dt;
                agent.droneOrbitAngle += agent.settings.droneOrbitSpeed * 1.5f * dt;
                float r = agent.settings.droneOrbitRadius * 0.4f;
                XMFLOAT3 orbitPos = {
                    agent.investigatePos.x + std::cos(agent.droneOrbitAngle) * r,
                    agent.position.y,
                    agent.investigatePos.z + std::sin(agent.droneOrbitAngle) * r
                };
                MoveDroneToward(dt, agent, orbitPos, speed * 0.5f);

                // Vary altitude while scanning — bob lower to look closer
                agent.droneTargetAlt = agent.settings.droneHoverHeight
                    + std::sin(agent.droneOrbitAngle * 0.5f) * 1.5f;

                if (agent.investigateTimer <= 0.0f) {
                    agent.state = agent.patrolPoints.empty() ? AIState::Return : AIState::Patrol;
                    if (agent.state == AIState::Patrol && !agent.patrolPoints.empty())
                        agent.currentPatrolIndex = 0;
                    agent.droneTargetAlt = agent.settings.droneHoverHeight;
                }
            } else {
                MoveDroneToward(dt, agent, target, speed);
            }
            break;
        }

        case AIState::Chase: {
            speed = agent.settings.droneChaseSpeed;  // Slower drone chase speed

            if (distToPlayer > agent.settings.loseRange || !agent.canSeePlayer) {
                if (!agent.canSeePlayer && distToPlayer <= agent.settings.loseRange) {
                    agent.state = AIState::Investigate;
                    agent.investigatePos = playerPos;
                    agent.investigateTimer = 4.0f;
                } else {
                    agent.state = AIState::Return;
                }
                break;
            }

            // Fly toward player — maintain altitude advantage
            XMFLOAT3 chaseTarget = playerPos;
            chaseTarget.y = agent.position.y;  // XZ movement only, altitude handled separately
            MoveDroneToward(dt, agent, chaseTarget, speed);

            // Dynamically lower altitude when close, raise when far
            float horizDist = std::sqrt(
                (playerPos.x - agent.position.x) * (playerPos.x - agent.position.x) +
                (playerPos.z - agent.position.z) * (playerPos.z - agent.position.z));
            if (horizDist < 5.0f) {
                // Close — dip down slightly for better targeting angle
                agent.droneTargetAlt = agent.settings.droneHoverHeight * 0.7f;
            } else {
                agent.droneTargetAlt = agent.settings.droneHoverHeight;
            }

            // Face the player
            FaceToward(agent, playerPos, dt);
            break;
        }

        case AIState::TakeCover:
            // Drones don't use cover — switch to investigate
            agent.state = AIState::Investigate;
            agent.investigatePos = agent.threatPos;
            agent.investigateTimer = 3.0f;
            break;

        case AIState::Return: {
            if (canDetectPlayer()) {
                agent.state = AIState::Chase;
                agent.repathTimer = 0.0f;
                break;
            }

            XMFLOAT3 home = agent.homePosition;
            home.y = agent.position.y;
            float hdist = std::sqrt(
                (home.x - agent.position.x) * (home.x - agent.position.x) +
                (home.z - agent.position.z) * (home.z - agent.position.z));

            if (hdist < 1.5f) {
                agent.state = agent.patrolPoints.empty() ? AIState::Idle : AIState::Patrol;
                agent.currentPatrolIndex = 0;
                agent.droneTargetAlt = agent.settings.droneHoverHeight;
            } else {
                MoveDroneToward(dt, agent, home, speed);
            }
            break;
        }

        default: break;
    }

    // ---- Banking / tilting based on movement ----
    float movedX = agent.position.x - prevX;
    float movedZ = agent.position.z - prevZ;
    float hSpeed = std::sqrt(movedX * movedX + movedZ * movedZ) / (std::max)(dt, 0.001f);
    agent.droneSpeedCurrent += (hSpeed - agent.droneSpeedCurrent) * 5.0f * dt;  // Smooth

    // Pitch — tilt forward proportional to speed
    float targetPitch = (agent.droneSpeedCurrent / (std::max)(agent.settings.droneChaseSpeed, 0.1f))
                        * agent.settings.droneMaxPitch;
    targetPitch = (std::min)(targetPitch, agent.settings.droneMaxPitch);
    agent.dronePitch += (targetPitch - agent.dronePitch) * 4.0f * dt;

    // Roll — bank into turns (based on yaw rate)
    {
        float yawRad = XMConvertToRadians(agent.yaw);
        float moveAngle = std::atan2(movedX, movedZ);
        float angleDiff = moveAngle - yawRad;
        while (angleDiff > 3.14159f)  angleDiff -= 6.28318f;
        while (angleDiff < -3.14159f) angleDiff += 6.28318f;
        float targetRoll = angleDiff * (agent.droneSpeedCurrent / (std::max)(speed, 0.1f))
                           * agent.settings.droneMaxRoll;
        targetRoll = (std::max)(-agent.settings.droneMaxRoll, (std::min)(targetRoll, agent.settings.droneMaxRoll));
        agent.droneRoll += (targetRoll - agent.droneRoll) * 4.0f * dt;
    }

    // ---- Downwash timer (particle emission tracked here, spawned in Application) ----
    agent.droneDownwashTimer -= dt;
    if (agent.droneDownwashTimer <= 0.0f)
        agent.droneDownwashTimer = 0.0f;

    // Agent-to-agent avoidance (XZ only)
    ApplyAgentAvoidance(dt, agent);
}

void AISystem::MoveDroneToward(float dt, AIAgent& agent, const XMFLOAT3& target, float speed) {
    // XZ movement only — altitude is handled by the spring-damper system in UpdateDrone
    float dx = target.x - agent.position.x;
    float dz = target.z - agent.position.z;
    float dist = std::sqrt(dx * dx + dz * dz);
    if (dist < 0.05f) return;

    // Smooth approach — decelerate when close
    float effectiveSpeed = speed;
    if (dist < 2.0f) effectiveSpeed *= (dist / 2.0f);  // Slow down near target

    float moveAmount = effectiveSpeed * dt;
    if (moveAmount > dist) moveAmount = dist;
    float inv = moveAmount / dist;

    agent.position.x += dx * inv;
    agent.position.z += dz * inv;

    // Face movement direction (XZ only)
    if (std::abs(dx) > 0.01f || std::abs(dz) > 0.01f) {
        FaceToward(agent, target, dt);
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

void AISystem::AdvancePatrolIndex(AIAgent& agent, const NavGrid& navGrid) {
    switch (agent.settings.patrolMode) {
        case PatrolMode::Loop: {
            if (agent.patrolPoints.empty()) return;
            agent.currentPatrolIndex++;
            if (agent.currentPatrolIndex >= static_cast<int>(agent.patrolPoints.size()))
                agent.currentPatrolIndex = 0;
            RequestPath(agent, navGrid, agent.patrolPoints[agent.currentPatrolIndex]);
            break;
        }
        case PatrolMode::PingPong: {
            if (agent.patrolPoints.empty()) return;
            agent.currentPatrolIndex += agent.patrolDirection;
            if (agent.currentPatrolIndex >= static_cast<int>(agent.patrolPoints.size())) {
                agent.patrolDirection = -1;
                agent.currentPatrolIndex = static_cast<int>(agent.patrolPoints.size()) - 2;
                if (agent.currentPatrolIndex < 0) agent.currentPatrolIndex = 0;
            } else if (agent.currentPatrolIndex < 0) {
                agent.patrolDirection = 1;
                agent.currentPatrolIndex = 1;
                if (agent.currentPatrolIndex >= static_cast<int>(agent.patrolPoints.size()))
                    agent.currentPatrolIndex = 0;
            }
            RequestPath(agent, navGrid, agent.patrolPoints[agent.currentPatrolIndex]);
            break;
        }
        case PatrolMode::Random: {
            if (agent.patrolPoints.empty()) return;
            int newIdx = std::rand() % static_cast<int>(agent.patrolPoints.size());
            agent.currentPatrolIndex = newIdx;
            RequestPath(agent, navGrid, agent.patrolPoints[agent.currentPatrolIndex]);
            break;
        }
        case PatrolMode::AreaRoam: {
            float r = agent.settings.areaRoamRadius;
            XMFLOAT3 target = {
                agent.homePosition.x + RandFloat(-r, r),
                agent.homePosition.y,
                agent.homePosition.z + RandFloat(-r, r)
            };
            RequestPath(agent, navGrid, target);
            break;
        }
        default: break;
    }
}

// ==================== Sound Event System ====================

void AISystem::PostSoundEvent(const SoundEvent& evt) {
    m_pendingSounds.push_back(evt);
}

void AISystem::PostGunshot(const XMFLOAT3& position, float radius, int sourceId) {
    SoundEvent evt;
    evt.position = position;
    evt.radius   = radius;
    evt.type     = SoundType::Gunshot;
    evt.sourceId = sourceId;
    m_pendingSounds.push_back(evt);
}

void AISystem::PostFootstep(const XMFLOAT3& position, float radius, int sourceId) {
    SoundEvent evt;
    evt.position = position;
    evt.radius   = radius;
    evt.type     = SoundType::Footstep;
    evt.sourceId = sourceId;
    m_pendingSounds.push_back(evt);
}

void AISystem::PostBulletImpact(const XMFLOAT3& position, float radius) {
    SoundEvent evt;
    evt.position = position;
    evt.radius   = radius;
    evt.type     = SoundType::BulletImpact;
    evt.sourceId = -1;
    m_pendingSounds.push_back(evt);
}

void AISystem::ProcessSoundEvents(AIAgent& agent, const NavGrid& navGrid, PhysicsWorld* physics) {
    // Don't react to sounds while already chasing or taking cover
    if (agent.state == AIState::Chase || agent.state == AIState::TakeCover) return;

    // Cooldown — don't keep reacting every frame
    if (agent.soundAlertTimer > 0.0f) return;

    // Find the highest-priority sound event within hearing range
    // Priority: Gunshot > BulletImpact > Footstep, closer > farther
    float bestPriority = -1.0f;
    const SoundEvent* bestEvent = nullptr;

    for (const auto& evt : m_pendingSounds) {
        // Determine hearing range for this sound type
        float hearRange = 0.0f;
        float basePriority = 0.0f;
        switch (evt.type) {
            case SoundType::Footstep:
                hearRange = agent.settings.hearFootstepRange;
                basePriority = 1.0f;
                break;
            case SoundType::Gunshot:
                hearRange = agent.settings.hearGunshotRange;
                basePriority = 3.0f;
                break;
            case SoundType::BulletImpact:
                hearRange = agent.settings.hearImpactRange;
                basePriority = 2.0f;
                break;
            default: continue;
        }

        // Clamp to sound's actual radius
        if (hearRange > evt.radius) hearRange = evt.radius;

        // Distance check (XZ for ground agents, 3D for drones)
        float dx = evt.position.x - agent.position.x;
        float dz = evt.position.z - agent.position.z;
        float distSq = dx * dx + dz * dz;
        if (agent.type == AIAgentType::Drone) {
            float dy = evt.position.y - agent.position.y;
            distSq += dy * dy;
        }
        if (distSq > hearRange * hearRange) continue;

        // Sound occlusion — grid LOS for ground agents, physics raycast for drones
        if (agent.type == AIAgentType::Ground) {
            if (!navGrid.HasGridLOS(agent.position, evt.position)) continue;
        } else {
            // Drones in the air — use physics raycast for sound occlusion
            if (physics && !HasLineOfSight(agent, evt.position, physics)) continue;
        }

        // Calculate priority: type priority + closeness bonus
        float dist = std::sqrt(distSq);
        float closenessBonus = 1.0f - (dist / hearRange);  // 1.0 at distance=0, 0.0 at max range
        float priority = basePriority + closenessBonus;

        if (priority > bestPriority) {
            bestPriority = priority;
            bestEvent = &evt;
        }
    }

    if (!bestEvent) return;

    // Agent heard a sound — react!
    agent.lastHeardSoundPos = bestEvent->position;
    agent.soundAlertTimer = 1.5f;  // Don't react again for 1.5 seconds

    float dx = bestEvent->position.x - agent.position.x;
    float dz = bestEvent->position.z - agent.position.z;

    // Gunfire nearby — seek cover if configured to do so
    if (bestEvent->type == SoundType::Gunshot && agent.settings.seekCoverOnGunfire) {
        XMFLOAT3 coverTarget;
        if (agent.type == AIAgentType::Ground &&
            FindCoverPosition(agent, bestEvent->position, navGrid, physics, coverTarget)) {
            agent.state = AIState::TakeCover;
            agent.coverPos = coverTarget;
            agent.threatPos = bestEvent->position;
            agent.coverTimer = 0.0f;
            agent.coverDamageTimer = 0.0f;
            agent.inCover = false;
            RequestPath(agent, navGrid, coverTarget);
            return;
        }
        // No cover found (or drone) — fall through to investigate
    }

    // Transition to Investigate — face the sound and path to it
    agent.state = AIState::Investigate;
    agent.investigatePos = bestEvent->position;
    // Gunshots get longer investigation time
    agent.investigateTimer = (bestEvent->type == SoundType::Gunshot) ? 5.0f : 3.0f;
    agent.lookTimer = 0.0f;
    agent.targetLookYaw = XMConvertToDegrees(std::atan2(dx, dz));

    if (agent.type == AIAgentType::Ground) {
        RequestPath(agent, navGrid, bestEvent->position);
    } else {
        // Drones fly directly — just set the target (handled in UpdateDrone)
        agent.currentPath.clear();
        agent.currentPath.push_back(bestEvent->position);
        agent.pathIndex = 0;
    }
}

// ==================== Cover System ====================

bool AISystem::FindCoverPosition(const AIAgent& agent, const XMFLOAT3& threatPos,
                                  const NavGrid& navGrid, PhysicsWorld* physics,
                                  XMFLOAT3& outCoverPos) const {
    // Strategy: Search walkable cells near the agent that have a blocked cell
    // between them and the threat. The blocked cell acts as a wall for cover.
    // Validate top candidates with physics raycasts for 3D accuracy.

    NavCoord agentCell = navGrid.WorldToGrid(agent.position.x, agent.position.z);
    NavCoord threatCell = navGrid.WorldToGrid(threatPos.x, threatPos.z);

    int searchR = static_cast<int>(agent.settings.coverSearchRadius);

    // Direction from threat to agent (the "away" direction we want to hide behind)
    float threatDx = agent.position.x - threatPos.x;
    float threatDz = agent.position.z - threatPos.z;
    float threatDist = std::sqrt(threatDx * threatDx + threatDz * threatDz);
    if (threatDist < 0.01f) return false;
    float awayX = threatDx / threatDist;
    float awayZ = threatDz / threatDist;

    // Collect and score candidate cover cells
    struct CoverCandidate {
        NavCoord cell;
        float score;
    };
    std::vector<CoverCandidate> candidates;
    candidates.reserve(64);

    float minCellDist = agent.settings.coverMinDist / navGrid.GetCellSize();

    for (int dz = -searchR; dz <= searchR; dz++) {
        for (int dx = -searchR; dx <= searchR; dx++) {
            int cx = agentCell.x + dx;
            int cz = agentCell.z + dz;

            // Must be walkable
            if (!navGrid.IsWalkable(cx, cz)) continue;

            // Must NOT have grid LOS to threat (wall blocks the view)
            NavCoord candidate = { cx, cz };
            if (navGrid.HasGridLOS(candidate, threatCell)) continue;

            // Must be reachable (has grid LOS from agent, or is the agent's current cell)
            if (dx != 0 || dz != 0) {
                if (!navGrid.HasGridLOS(agentCell, candidate)) continue;
            }

            // Not too close to the threat
            float ctdx = static_cast<float>(cx - threatCell.x);
            float ctdz = static_cast<float>(cz - threatCell.z);
            float cellToThreat = std::sqrt(ctdx * ctdx + ctdz * ctdz);
            if (cellToThreat < minCellDist) continue;

            // Score calculation
            float cellDist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
            float cellLen = std::sqrt(static_cast<float>(dx * dx + dz * dz));
            float awayDot = 0.0f;
            if (cellLen > 0.01f)
                awayDot = (static_cast<float>(dx) / cellLen) * awayX +
                          (static_cast<float>(dz) / cellLen) * awayZ;

            float score = (1.0f / (1.0f + cellDist)) * 2.0f   // Prefer close cells
                        + awayDot * 1.5f                        // Prefer cells behind agent
                        + (cellToThreat * 0.1f);                // Slightly prefer farther from threat

            // Bonus: cell is adjacent to a blocked cell (tight to wall = better cover)
            bool adjacentToWall = false;
            for (int nd = 0; nd < 4; nd++) {
                static const int ndx[] = { 0, 0, -1, 1 };
                static const int ndz[] = { -1, 1, 0, 0 };
                if (!navGrid.IsWalkable(cx + ndx[nd], cz + ndz[nd])) {
                    adjacentToWall = true;
                    break;
                }
            }
            if (adjacentToWall) score += 1.5f;  // Wall-hugging bonus

            candidates.push_back({ candidate, score });
        }
    }

    if (candidates.empty()) return false;

    // Sort by score descending
    std::sort(candidates.begin(), candidates.end(),
              [](const CoverCandidate& a, const CoverCandidate& b) { return a.score > b.score; });

    // Validate top candidates with physics raycast (up to 5)
    int checksLeft = (std::min)(5, static_cast<int>(candidates.size()));
    for (int i = 0; i < checksLeft; i++) {
        XMFLOAT3 coverWorld = navGrid.GridToWorld(candidates[i].cell.x, candidates[i].cell.z);

        if (physics) {
            // Raycast from cover position (eye height) toward threat — should be blocked
            float eyeH = agent.settings.bodyScale * 0.8f;
            XMFLOAT3 coverEye = { coverWorld.x, coverWorld.y + eyeH, coverWorld.z };
            XMFLOAT3 threatEye = { threatPos.x, threatPos.y + 0.8f, threatPos.z };
            float rdx = threatEye.x - coverEye.x;
            float rdy = threatEye.y - coverEye.y;
            float rdz = threatEye.z - coverEye.z;
            float rd = std::sqrt(rdx * rdx + rdy * rdy + rdz * rdz);
            if (rd > 0.01f) {
                XMFLOAT3 dir = { rdx / rd, rdy / rd, rdz / rd };
                CollisionHit hit = physics->Raycast(coverEye, dir, rd);
                if (hit.hit) {
                    // Wall blocks sight — confirmed good cover!
                    outCoverPos = coverWorld;
                    return true;
                }
                // Not actually blocked by a 3D wall — try next candidate
                continue;
            }
        }

        // No physics available — trust the grid-based check
        outCoverPos = coverWorld;
        return true;
    }

    // Fall back to best grid-scored candidate if physics rejected top 5
    if (candidates.size() > static_cast<size_t>(checksLeft)) {
        outCoverPos = navGrid.GridToWorld(candidates[checksLeft].cell.x, candidates[checksLeft].cell.z);
        return true;
    }

    return false;
}

// ==================== LOS / FOV / Avoidance ====================

bool AISystem::HasLineOfSight(const AIAgent& agent, const XMFLOAT3& target, PhysicsWorld* physics) const {
    if (!physics) return true;

    // Ray from agent eye height toward target chest height
    float eyeH = agent.settings.bodyScale * 0.8f;
    XMFLOAT3 origin = { agent.position.x, agent.position.y + eyeH, agent.position.z };
    XMFLOAT3 tgt    = { target.x, target.y + 0.8f, target.z };

    float dx = tgt.x - origin.x;
    float dy = tgt.y - origin.y;
    float dz = tgt.z - origin.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 0.01f) return true;

    XMFLOAT3 dir = { dx / dist, dy / dist, dz / dist };
    CollisionHit hit = physics->Raycast(origin, dir, dist);
    return !hit.hit;  // Clear sight if nothing was hit before reaching target
}

bool AISystem::IsInFieldOfView(const AIAgent& agent, const XMFLOAT3& target) const {
    float dx = target.x - agent.position.x;
    float dz = target.z - agent.position.z;
    float dist = std::sqrt(dx * dx + dz * dz);
    if (dist < 0.5f) return true;  // Very close — always aware

    // Agent facing direction from yaw (degrees)
    float yawRad = XMConvertToRadians(agent.yaw);
    float fwdX = std::sin(yawRad);
    float fwdZ = std::cos(yawRad);

    // Direction to target (XZ, normalised)
    float toX = dx / dist;
    float toZ = dz / dist;

    float dot = fwdX * toX + fwdZ * toZ;  // cos(angle between)
    float halfFOV = XMConvertToRadians(agent.settings.fovAngle * 0.5f);
    return dot >= std::cos(halfFOV);
}

void AISystem::ApplyAgentAvoidance(float dt, AIAgent& agent) {
    float avoidR = agent.settings.avoidRadius;
    float avoidF = agent.settings.avoidForce;

    for (const auto& other : m_agents) {
        if (&other == &agent) continue;
        if (!other.visible) continue;

        float dx = agent.position.x - other.position.x;
        float dz = agent.position.z - other.position.z;
        float distSq = dx * dx + dz * dz;

        if (distSq < avoidR * avoidR && distSq > 0.0001f) {
            float dist = std::sqrt(distSq);
            float overlap = avoidR - dist;
            float pushStrength = (overlap / avoidR) * avoidF * dt;
            agent.position.x += (dx / dist) * pushStrength;
            agent.position.z += (dz / dist) * pushStrength;
        }
    }
}

// ==================== Debug Drawing ====================

void AISystem::DebugDraw(DebugRenderer& debug, const NavGrid& navGrid) const {
    if (!showDebug) return;

    for (const auto& agent : m_agents) {
        if (!agent.visible) continue;

        float halfScale = agent.settings.bodyScale * 0.5f;
        XMFLOAT3 pos = agent.position;

        // Agent body rendering depends on type
        XMFLOAT4 agentColor = {
            agent.settings.bodyColor[0],
            agent.settings.bodyColor[1],
            agent.settings.bodyColor[2],
            0.8f
        };

        float yawRad = XMConvertToRadians(agent.yaw);
        float fwdX = std::sin(yawRad);
        float fwdZ = std::cos(yawRad);

        if (agent.type == AIAgentType::Drone) {
            // Drone — flat disc body + propeller arms with tilt visualization
            XMFLOAT3 droneCenter = { pos.x, pos.y, pos.z };
            // Flat body (wide X/Z, thin Y)
            debug.DrawBox(droneCenter, { halfScale, halfScale * 0.25f, halfScale }, agentColor);

            // Four propeller arms extending from center
            float armLen = halfScale * 1.4f;
            float propY = droneCenter.y + halfScale * 0.3f;
            XMFLOAT4 propColor = { 0.8f, 0.8f, 0.8f, 0.6f };

            // Spinning propeller visual — rotate arms slightly each frame
            float spinOffset = agent.droneBobPhase * 3.0f;  // Use bob phase for spin
            for (int p = 0; p < 4; p++) {
                float angle = yawRad + (p * 1.5708f) + 0.7854f + spinOffset;
                float px = std::sin(angle) * armLen;
                float pz = std::cos(angle) * armLen;

                // Apply pitch/roll tilt to propeller positions
                float pitchRad = XMConvertToRadians(agent.dronePitch);
                float rollRad = XMConvertToRadians(agent.droneRoll);
                float tipY = propY - px * std::sin(pitchRad) + pz * std::sin(rollRad);

                debug.DrawLine(droneCenter,
                    { droneCenter.x + px, tipY, droneCenter.z + pz }, propColor);
                // Small circle at propeller tip
                debug.DrawSphere({ droneCenter.x + px, tipY, droneCenter.z + pz },
                                 halfScale * 0.3f, propColor, 6);

                // ---- Spiral downwash from each propeller ----
                XMFLOAT3 propTip = { droneCenter.x + px, tipY, droneCenter.z + pz };
                float washLen = 2.0f + std::sin(agent.droneBobPhase + p * 1.5f) * 0.5f;
                int spiralSteps = 12;
                float spiralRadius = 0.15f;     // Starting radius
                float spiralGrow = 0.25f;       // How much radius grows along length
                float spiralTurns = 2.0f;       // Number of full spiral rotations
                float spiralPhase = agent.droneBobPhase * 6.0f + p * 1.5708f; // Animated spin

                XMFLOAT3 prevPt = propTip;
                for (int s = 1; s <= spiralSteps; s++) {
                    float t = static_cast<float>(s) / static_cast<float>(spiralSteps);
                    float spiralAngle = spiralPhase + t * spiralTurns * 6.2832f;
                    float r = spiralRadius + t * spiralGrow;
                    float sy = propTip.y - t * washLen;
                    float sx = propTip.x + std::cos(spiralAngle) * r;
                    float sz = propTip.z + std::sin(spiralAngle) * r;

                    // Fade alpha along length
                    float alpha = 0.35f * (1.0f - t * 0.7f);
                    XMFLOAT4 washColor = { 0.5f, 0.7f, 1.0f, alpha };
                    debug.DrawLine(prevPt, { sx, sy, sz }, washColor);
                    prevPt = { sx, sy, sz };
                }
            }

            // Direction indicator forward (with pitch tilt)
            float arrowLen = agent.settings.bodyScale;
            float pitchRad = XMConvertToRadians(agent.dronePitch);
            debug.DrawLine(droneCenter,
                { droneCenter.x + fwdX * arrowLen,
                  droneCenter.y - std::sin(pitchRad) * arrowLen,
                  droneCenter.z + fwdZ * arrowLen },
                { 1.0f, 1.0f, 0.0f, 1.0f });

            // Altitude indicator — line to ground
            float groundY = navGrid.GetGridY();
            debug.DrawLine(droneCenter,
                { droneCenter.x, groundY, droneCenter.z },
                { 0.4f, 0.4f, 0.4f, 0.2f });
        } else {
            // Ground agent — torso box + sphere head (matches rendered model)
            float bodyW = halfScale * 0.7f;
            float bodyH = halfScale * 1.0f;
            float bodyD = halfScale * 0.5f;
            float headR = halfScale * 0.35f;

            XMFLOAT3 torsoPos = { pos.x, pos.y + bodyH, pos.z };
            debug.DrawBox(torsoPos, { bodyW, bodyH, bodyD }, agentColor);

            // Head sphere on top
            XMFLOAT3 headPos = { pos.x, pos.y + bodyH * 2.0f + headR, pos.z };
            debug.DrawSphere(headPos, headR, agentColor, 8);

            // Direction arrow (forward facing)
            float arrowLen = agent.settings.bodyScale;
            debug.DrawLine(
                torsoPos,
                { torsoPos.x + fwdX * arrowLen, torsoPos.y, torsoPos.z + fwdZ * arrowLen },
                { 1.0f, 1.0f, 0.0f, 1.0f });
        }

        // Position for state indicator (above body)
        XMFLOAT3 indicatorPos = (agent.type == AIAgentType::Drone)
            ? XMFLOAT3{ pos.x, pos.y + halfScale * 0.5f, pos.z }
            : XMFLOAT3{ pos.x, pos.y + halfScale + halfScale + 0.3f, pos.z };

        // State indicator color
        XMFLOAT4 stateColor;
        switch (agent.state) {
            case AIState::Idle:           stateColor = { 0.5f, 0.5f, 0.5f, 1.0f }; break;
            case AIState::Patrol:         stateColor = { 0.0f, 0.7f, 1.0f, 1.0f }; break;
            case AIState::WaitAtWaypoint: stateColor = { 0.3f, 0.8f, 0.3f, 1.0f }; break;
            case AIState::Investigate:    stateColor = { 1.0f, 0.6f, 0.0f, 1.0f }; break;
            case AIState::Chase:          stateColor = { 1.0f, 0.0f, 0.0f, 1.0f }; break;
            case AIState::TakeCover:      stateColor = { 0.2f, 0.2f, 0.8f, 1.0f }; break;
            case AIState::Return:         stateColor = { 1.0f, 1.0f, 0.0f, 1.0f }; break;
            default:                      stateColor = { 1.0f, 1.0f, 1.0f, 1.0f }; break;
        }
        // Small indicator above head
        debug.DrawSphere(indicatorPos, 0.1f, stateColor, 6);

        // Draw detection range circle
        XMFLOAT3 rangePos = (agent.type == AIAgentType::Drone)
            ? XMFLOAT3{ agent.position.x, agent.position.y, agent.position.z }
            : XMFLOAT3{ agent.position.x, agent.position.y + 0.1f, agent.position.z };
        debug.DrawSphere(rangePos, agent.settings.detectRange, { 1.0f, 1.0f, 0.0f, 0.15f }, 24);

        // Draw cover position if in TakeCover state
        if (agent.state == AIState::TakeCover) {
            float coverY = navGrid.GetGridY() + 0.2f;
            debug.DrawSphere({ agent.coverPos.x, coverY, agent.coverPos.z }, 0.25f,
                             { 0.2f, 0.2f, 1.0f, 0.8f }, 8);
            // Line from agent to cover
            debug.DrawLine(
                { agent.position.x, coverY, agent.position.z },
                { agent.coverPos.x, coverY, agent.coverPos.z },
                { 0.2f, 0.2f, 1.0f, 0.4f });
        }

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
