#pragma once

#include <DirectXMath.h>
#include <vector>
#include <cstdint>

namespace WT {

using namespace DirectX;

// ---- Cell state in the navigation grid ----
enum class NavCellState : uint8_t {
    Walkable = 0,
    Blocked  = 1
};

// ---- 2D grid coordinate ----
struct NavCoord {
    int x = 0;
    int z = 0;
    bool operator==(const NavCoord& o) const { return x == o.x && z == o.z; }
    bool operator!=(const NavCoord& o) const { return !(*this == o); }
};

// ---- Navigation Grid — 2D walkability grid with A* pathfinding ----
class NavGrid {
public:
    // Create a grid of gridWidth x gridHeight cells
    // originX/Z = world-space position of cell (0,0) corner
    void Init(int gridWidth, int gridHeight, float cellSize,
              float originX = 0.0f, float originZ = 0.0f, float gridY = 0.0f);

    void Shutdown();

    // ---- Cell access ----
    void SetCell(int x, int z, NavCellState state);
    NavCellState GetCell(int x, int z) const;
    bool IsWalkable(int x, int z) const;
    bool InBounds(int x, int z) const;

    // ---- World <-> Grid conversion ----
    NavCoord WorldToGrid(float wx, float wz) const;
    XMFLOAT3 GridToWorld(int gx, int gz) const;  // Returns center of cell at gridY height

    // ---- Pathfinding (A*) ----
    // Returns empty vector if no path found
    // allowDiagonal: if true, 8-connected; if false, 4-connected
    std::vector<NavCoord> FindPath(NavCoord start, NavCoord goal, bool allowDiagonal = true) const;

    // Convenience: world-space path (with optional smoothing)
    std::vector<XMFLOAT3> FindPathWorld(const XMFLOAT3& startPos, const XMFLOAT3& goalPos,
                                         bool allowDiagonal = true) const;

    // ---- Line of sight on the grid (Bresenham) ----
    // Returns true if there is a clear walkable line between two grid cells
    bool HasGridLOS(NavCoord from, NavCoord to) const;
    bool HasGridLOS(const XMFLOAT3& fromWorld, const XMFLOAT3& toWorld) const;

    // ---- Path smoothing ----
    // Remove unnecessary intermediate waypoints where direct grid LOS exists
    std::vector<NavCoord> SmoothPath(const std::vector<NavCoord>& path) const;

    // ---- Obstacle placement ----
    // Mark cells overlapping an axis-aligned box as blocked
    void BlockBox(const XMFLOAT3& center, const XMFLOAT3& halfExtents);
    // Clear entire grid to walkable
    void ClearGrid();
    // Rebuild obstacles from scene entities
    void RebuildFromEntities(const class Scene& scene);

    // ---- Getters ----
    int   GetWidth() const  { return m_width; }
    int   GetHeight() const { return m_height; }
    float GetCellSize() const { return m_cellSize; }
    float GetOriginX() const  { return m_originX; }
    float GetOriginZ() const  { return m_originZ; }
    float GetGridY() const    { return m_gridY; }
    bool  IsInitialized() const { return !m_cells.empty(); }

    // ---- Setters ----
    void SetCellSize(float s) { m_cellSize = s; }
    void SetOrigin(float ox, float oz) { m_originX = ox; m_originZ = oz; }
    void SetGridY(float y) { m_gridY = y; }

    // ---- Debug visualization ----
    // Draws the grid using DebugRenderer (call before DebugRenderer::Flush)
    void DebugDraw(class DebugRenderer& debug) const;
    void DebugDrawPath(class DebugRenderer& debug, const std::vector<NavCoord>& path,
                       const XMFLOAT4& color = { 1.0f, 0.5f, 0.0f, 1.0f }) const;

    bool showDebug = false;

private:
    int CellIndex(int x, int z) const { return z * m_width + x; }

    std::vector<NavCellState> m_cells;
    int   m_width    = 0;
    int   m_height   = 0;       // grid "height" = Z dimension
    float m_cellSize = 1.0f;
    float m_originX  = 0.0f;
    float m_originZ  = 0.0f;
    float m_gridY    = 0.0f;    // Y plane of the grid in world space
};

} // namespace WT
