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

    std::vector<XMFLOAT3> worldPath;
    worldPath.reserve(gridPath.size());
    for (const auto& gc : gridPath) {
        worldPath.push_back(GridToWorld(gc.x, gc.z));
    }
    return worldPath;
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
        if (!e.visible) continue;
        // Treat entity as axis-aligned box at its position with half-extents = scale/2
        XMFLOAT3 center = { e.position[0], e.position[1], e.position[2] };
        XMFLOAT3 halfExt = { e.scale[0] * 0.5f, e.scale[1] * 0.5f, e.scale[2] * 0.5f };
        BlockBox(center, halfExt);
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
