#include "NavGrid.h"
#include "Core/Entity.h"
#include "Graphics/DebugRenderer.h"
#include "Util/Log.h"
#include <queue>
#include <unordered_map>
#include <cmath>
#include <algorithm>

namespace WT {

// ==================== Init / Shutdown ====================

void NavGrid::Init(int gridWidth, int gridHeight, float cellSize,
                    float originX, float originZ, float gridY) {
    m_width    = gridWidth;
    m_height   = gridHeight;
    m_cellSize = cellSize;
    m_originX  = originX;
    m_originZ  = originZ;
    m_gridY    = gridY;
    m_cells.assign(m_width * m_height, NavCellState::Walkable);
    LOG_INFO("NavGrid initialized: %dx%d, cellSize=%.1f, origin=(%.1f, %.1f), Y=%.1f",
             m_width, m_height, m_cellSize, m_originX, m_originZ, m_gridY);
}

void NavGrid::Shutdown() {
    m_cells.clear();
    m_width = m_height = 0;
}

// ==================== Cell Access ====================

void NavGrid::SetCell(int x, int z, NavCellState state) {
    if (!InBounds(x, z)) return;
    m_cells[CellIndex(x, z)] = state;
}

NavCellState NavGrid::GetCell(int x, int z) const {
    if (!InBounds(x, z)) return NavCellState::Blocked;
    return m_cells[CellIndex(x, z)];
}

bool NavGrid::IsWalkable(int x, int z) const {
    return GetCell(x, z) == NavCellState::Walkable;
}

bool NavGrid::InBounds(int x, int z) const {
    return x >= 0 && x < m_width && z >= 0 && z < m_height;
}

// ==================== World <-> Grid ====================

NavCoord NavGrid::WorldToGrid(float wx, float wz) const {
    NavCoord c;
    c.x = static_cast<int>(std::floor((wx - m_originX) / m_cellSize));
    c.z = static_cast<int>(std::floor((wz - m_originZ) / m_cellSize));
    return c;
}

XMFLOAT3 NavGrid::GridToWorld(int gx, int gz) const {
    float wx = m_originX + (gx + 0.5f) * m_cellSize;
    float wz = m_originZ + (gz + 0.5f) * m_cellSize;
    return { wx, m_gridY, wz };
}

// ==================== A* Pathfinding ====================

// Hash for NavCoord used in unordered_map
struct NavCoordHash {
    size_t operator()(const NavCoord& c) const {
        return std::hash<int>()(c.x) ^ (std::hash<int>()(c.z) << 16);
    }
};

struct AStarNode {
    NavCoord coord;
    float gCost = 0.0f;    // Cost from start
    float fCost = 0.0f;    // gCost + heuristic
    bool operator>(const AStarNode& o) const { return fCost > o.fCost; }
};

static float Heuristic(NavCoord a, NavCoord b) {
    // Octile distance (allows diagonal)
    int dx = std::abs(a.x - b.x);
    int dz = std::abs(a.z - b.z);
    return static_cast<float>(std::max(dx, dz)) + 0.414f * static_cast<float>(std::min(dx, dz));
}

std::vector<NavCoord> NavGrid::FindPath(NavCoord start, NavCoord goal, bool allowDiagonal) const {
    if (!InBounds(start.x, start.z) || !InBounds(goal.x, goal.z))
        return {};
    if (!IsWalkable(goal.x, goal.z))
        return {};
    if (start == goal)
        return { start };

    // Directions: 4-connected + optional 4 diagonals
    static const int dx4[] = { 1, -1, 0,  0 };
    static const int dz4[] = { 0,  0, 1, -1 };
    static const int dx8[] = { 1, -1, 0,  0, 1, -1,  1, -1 };
    static const int dz8[] = { 0,  0, 1, -1, 1,  1, -1, -1 };
    static const float cost4[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float cost8[] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.414f, 1.414f, 1.414f, 1.414f };

    const int* dxArr   = allowDiagonal ? dx8 : dx4;
    const int* dzArr   = allowDiagonal ? dz8 : dz4;
    const float* costs = allowDiagonal ? cost8 : cost4;
    int numDirs        = allowDiagonal ? 8 : 4;

    std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> openSet;
    std::unordered_map<NavCoord, NavCoord, NavCoordHash> cameFrom;
    std::unordered_map<NavCoord, float, NavCoordHash>    gScore;

    AStarNode startNode;
    startNode.coord = start;
    startNode.gCost = 0.0f;
    startNode.fCost = Heuristic(start, goal);
    openSet.push(startNode);
    gScore[start] = 0.0f;

    int maxIterations = m_width * m_height * 2;  // Safety limit
    int iterations = 0;

    while (!openSet.empty() && iterations < maxIterations) {
        iterations++;
        AStarNode current = openSet.top();
        openSet.pop();

        if (current.coord == goal) {
            // Reconstruct path
            std::vector<NavCoord> path;
            NavCoord c = goal;
            while (c != start) {
                path.push_back(c);
                c = cameFrom[c];
            }
            path.push_back(start);
            std::reverse(path.begin(), path.end());
            return path;
        }

        // Skip if we've already found a better route to this node
        auto git = gScore.find(current.coord);
        if (git != gScore.end() && current.gCost > git->second)
            continue;

        for (int i = 0; i < numDirs; i++) {
            NavCoord neighbor = { current.coord.x + dxArr[i], current.coord.z + dzArr[i] };
            if (!InBounds(neighbor.x, neighbor.z)) continue;
            if (!IsWalkable(neighbor.x, neighbor.z)) continue;

            // For diagonals, check that both adjacent cardinal cells are walkable
            // (prevents corner-cutting through blocked cells)
            if (i >= 4) {
                if (!IsWalkable(current.coord.x + dxArr[i], current.coord.z) ||
                    !IsWalkable(current.coord.x, current.coord.z + dzArr[i]))
                    continue;
            }

            float tentativeG = current.gCost + costs[i];
            auto nit = gScore.find(neighbor);
            if (nit == gScore.end() || tentativeG < nit->second) {
                gScore[neighbor] = tentativeG;
                cameFrom[neighbor] = current.coord;

                AStarNode next;
                next.coord = neighbor;
                next.gCost = tentativeG;
                next.fCost = tentativeG + Heuristic(neighbor, goal);
                openSet.push(next);
            }
        }
    }

    return {};  // No path found
}

std::vector<XMFLOAT3> NavGrid::FindPathWorld(const XMFLOAT3& startPos, const XMFLOAT3& goalPos,
                                               bool allowDiagonal) const {
    NavCoord startGrid = WorldToGrid(startPos.x, startPos.z);
    NavCoord goalGrid  = WorldToGrid(goalPos.x, goalPos.z);

    auto gridPath = FindPath(startGrid, goalGrid, allowDiagonal);

    // Smooth the grid path to remove unnecessary zigzags
    auto smoothed = SmoothPath(gridPath);

    std::vector<XMFLOAT3> worldPath;
    worldPath.reserve(smoothed.size());
    for (const auto& gc : smoothed) {
        worldPath.push_back(GridToWorld(gc.x, gc.z));
    }
    return worldPath;
}

// ==================== Grid Line of Sight (Bresenham) ====================

bool NavGrid::HasGridLOS(NavCoord from, NavCoord to) const {
    // Bresenham's line algorithm — checks all cells along the line are walkable
    int x0 = from.x, z0 = from.z;
    int x1 = to.x,   z1 = to.z;
    int dx = std::abs(x1 - x0);
    int dz = std::abs(z1 - z0);
    int sx = (x0 < x1) ? 1 : -1;
    int sz = (z0 < z1) ? 1 : -1;
    int err = dx - dz;

    while (true) {
        if (!IsWalkable(x0, z0)) return false;
        if (x0 == x1 && z0 == z1) break;
        int e2 = 2 * err;
        // Check diagonal neighbors to prevent corner cutting
        if (e2 > -dz && e2 < dx) {
            // Diagonal step — check both adjacent cells
            if (!IsWalkable(x0 + sx, z0) || !IsWalkable(x0, z0 + sz))
                return false;
        }
        if (e2 > -dz) { err -= dz; x0 += sx; }
        if (e2 < dx)  { err += dx; z0 += sz; }
    }
    return true;
}

bool NavGrid::HasGridLOS(const XMFLOAT3& fromWorld, const XMFLOAT3& toWorld) const {
    return HasGridLOS(WorldToGrid(fromWorld.x, fromWorld.z),
                      WorldToGrid(toWorld.x, toWorld.z));
}

// ==================== Path Smoothing ====================

std::vector<NavCoord> NavGrid::SmoothPath(const std::vector<NavCoord>& path) const {
    if (path.size() <= 2) return path;

    std::vector<NavCoord> smoothed;
    smoothed.push_back(path.front());

    size_t current = 0;
    while (current < path.size() - 1) {
        // Try to skip as far ahead as possible while maintaining grid LOS
        size_t farthest = current + 1;
        for (size_t test = path.size() - 1; test > current + 1; test--) {
            if (HasGridLOS(path[current], path[test])) {
                farthest = test;
                break;
            }
        }
        smoothed.push_back(path[farthest]);
        current = farthest;
    }

    return smoothed;
}

// ==================== Obstacle Placement ====================

void NavGrid::BlockBox(const XMFLOAT3& center, const XMFLOAT3& halfExtents) {
    float minX = center.x - halfExtents.x;
    float maxX = center.x + halfExtents.x;
    float minZ = center.z - halfExtents.z;
    float maxZ = center.z + halfExtents.z;

    NavCoord c0 = WorldToGrid(minX, minZ);
    NavCoord c1 = WorldToGrid(maxX, maxZ);

    for (int z = c0.z; z <= c1.z; z++) {
        for (int x = c0.x; x <= c1.x; x++) {
            SetCell(x, z, NavCellState::Blocked);
        }
    }
}

void NavGrid::ClearGrid() {
    std::fill(m_cells.begin(), m_cells.end(), NavCellState::Walkable);
}

void NavGrid::RebuildFromEntities(const Scene& scene) {
    ClearGrid();
    for (int i = 0; i < scene.GetEntityCount(); i++) {
        const auto& e = scene.GetEntity(i);
        if (!e.visible || e.noCollision) continue;
        // Skip pickup entities — they don't block navigation
        if (e.pickupType != PickupType::None) continue;

        XMFLOAT3 center = { e.position[0], e.position[1], e.position[2] };
        XMFLOAT3 halfExt = { e.scale[0] * 0.5f, e.scale[1] * 0.5f, e.scale[2] * 0.5f };

        bool hasRotation = (e.rotation[0] != 0.0f || e.rotation[1] != 0.0f || e.rotation[2] != 0.0f);
        if (!hasRotation) {
            // Fast path: axis-aligned
            BlockBox(center, halfExt);
        } else {
            // Rotated entity: transform 4 XZ corners through rotation, then find AABB
            XMMATRIX R = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(e.rotation[0]),
                XMConvertToRadians(e.rotation[1]),
                XMConvertToRadians(e.rotation[2]));

            // 4 corner offsets in local space (XZ plane)
            XMFLOAT3 localCorners[4] = {
                { -halfExt.x, 0, -halfExt.z },
                {  halfExt.x, 0, -halfExt.z },
                {  halfExt.x, 0,  halfExt.z },
                { -halfExt.x, 0,  halfExt.z },
            };

            float minX = FLT_MAX, maxX = -FLT_MAX;
            float minZ = FLT_MAX, maxZ = -FLT_MAX;
            for (int c = 0; c < 4; c++) {
                XMVECTOR v = XMVector3TransformNormal(XMLoadFloat3(&localCorners[c]), R);
                XMFLOAT3 rc;
                XMStoreFloat3(&rc, v);
                float wx = center.x + rc.x;
                float wz = center.z + rc.z;
                minX = (std::min)(minX, wx);
                maxX = (std::max)(maxX, wx);
                minZ = (std::min)(minZ, wz);
                maxZ = (std::max)(maxZ, wz);
            }

            // Block all cells in the rotated bounding rectangle
            // For more accurate blocking, test each cell center against the OBB
            NavCoord c0 = WorldToGrid(minX, minZ);
            NavCoord c1 = WorldToGrid(maxX, maxZ);

            // Rotation axes for OBB point-test (XZ plane only)
            XMFLOAT3X3 rotMat;
            XMStoreFloat3x3(&rotMat, R);
            // Local X axis in world space
            float axU_x = rotMat._11, axU_z = rotMat._13;
            // Local Z axis in world space
            float axV_x = rotMat._31, axV_z = rotMat._33;

            for (int z = c0.z; z <= c1.z; z++) {
                for (int x = c0.x; x <= c1.x; x++) {
                    XMFLOAT3 cellWorld = GridToWorld(x, z);
                    // Project cell center onto OBB local axes
                    float dx = cellWorld.x - center.x;
                    float dz = cellWorld.z - center.z;
                    float projU = dx * axU_x + dz * axU_z;
                    float projV = dx * axV_x + dz * axV_z;
                    // Test if within half-extents (with cell-size padding)
                    float pad = m_cellSize * 0.5f;
                    if (std::abs(projU) <= halfExt.x + pad && std::abs(projV) <= halfExt.z + pad) {
                        SetCell(x, z, NavCellState::Blocked);
                    }
                }
            }
        }
    }
}

// ==================== Debug Visualization ====================

void NavGrid::DebugDraw(DebugRenderer& debug) const {
    if (!showDebug || m_cells.empty()) return;

    const XMFLOAT4 walkableColor = { 0.2f, 0.6f, 0.2f, 0.3f };
    const XMFLOAT4 blockedColor  = { 0.8f, 0.2f, 0.2f, 0.5f };
    const XMFLOAT4 gridLineColor = { 0.4f, 0.4f, 0.4f, 0.2f };
    float y = m_gridY + 0.02f;  // Slight offset to avoid z-fighting with ground

    // Draw grid lines
    float totalW = m_width * m_cellSize;
    float totalH = m_height * m_cellSize;
    for (int x = 0; x <= m_width; x++) {
        float wx = m_originX + x * m_cellSize;
        debug.DrawLine({ wx, y, m_originZ }, { wx, y, m_originZ + totalH }, gridLineColor);
    }
    for (int z = 0; z <= m_height; z++) {
        float wz = m_originZ + z * m_cellSize;
        debug.DrawLine({ m_originX, y, wz }, { m_originX + totalW, y, wz }, gridLineColor);
    }

    // Draw blocked cells as filled boxes
    float halfCell = m_cellSize * 0.5f;
    for (int z = 0; z < m_height; z++) {
        for (int x = 0; x < m_width; x++) {
            if (m_cells[CellIndex(x, z)] == NavCellState::Blocked) {
                XMFLOAT3 center = GridToWorld(x, z);
                center.y = y;
                debug.DrawBox(center, { halfCell * 0.9f, 0.05f, halfCell * 0.9f }, blockedColor);
            }
        }
    }
}

void NavGrid::DebugDrawPath(DebugRenderer& debug, const std::vector<NavCoord>& path,
                             const XMFLOAT4& color) const {
    if (path.size() < 2) return;
    float y = m_gridY + 0.05f;  // Slightly above grid

    for (size_t i = 0; i < path.size() - 1; i++) {
        XMFLOAT3 a = GridToWorld(path[i].x, path[i].z);
        XMFLOAT3 b = GridToWorld(path[i + 1].x, path[i + 1].z);
        a.y = y;
        b.y = y;
        debug.DrawLine(a, b, color);
    }

    // Draw start/end markers
    if (!path.empty()) {
        XMFLOAT3 s = GridToWorld(path.front().x, path.front().z);
        s.y = y;
        debug.DrawSphere(s, m_cellSize * 0.2f, { 0.0f, 1.0f, 0.0f, 0.8f }, 8);
    }
    if (path.size() > 1) {
        XMFLOAT3 e = GridToWorld(path.back().x, path.back().z);
        e.y = y;
        debug.DrawSphere(e, m_cellSize * 0.2f, { 1.0f, 0.0f, 0.0f, 0.8f }, 8);
    }
}

} // namespace WT
