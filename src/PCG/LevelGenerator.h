#pragma once
// ============================================================
// LevelGenerator — Procedural urban warfare level generation
// ============================================================
// Generates a complete level with streets, buildings, cover,
// and detail objects using a seed-based random approach.
//
// Pipeline:
//   1. Ground plane
//   2. Perimeter walls
//   3. City block grid (streets + intersections)
//   4. Buildings placed in blocks (walls, roof, interior)
//   5. Cover objects scattered in streets and open areas
//   6. Detail props (barrels, crates, debris, fences, windows)
//
// Usage:
//   LevelGenerator gen;
//   gen.settings.seed = 12345;
//   gen.Generate(scene);
// ============================================================

#include "Core/Entity.h"
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>

namespace WT {

// ============================================================
// Generation settings (tunable from editor)
// ============================================================
struct LevelGenSettings {
    uint32_t seed         = 0;       // 0 = random seed
    float    arenaSize    = 40.0f;   // Total arena width/depth
    float    wallHeight   = 3.0f;    // Perimeter wall height
    int      gridCols     = 3;       // City block grid columns (2-5)
    int      gridRows     = 3;       // City block grid rows (2-5)
    float    streetWidth   = 4.0f;   // Width of streets between blocks
    float    buildingMinH  = 2.5f;   // Min building wall height
    float    buildingMaxH  = 5.0f;   // Max building wall height
    float    wallThickness = 0.4f;   // Wall thickness
    int      coverDensity  = 15;     // Number of cover objects in streets (5-30)
    int      detailDensity = 20;     // Number of detail props (5-40)
    bool     addWindows    = true;   // Glass window panes
    bool     addFences     = true;   // Wooden fences
    float    buildingChance = 0.7f;  // Probability a block gets a building (0-1)
    float    roofChance     = 0.8f;  // Probability a building gets a roof
};

// ============================================================
// LevelGenerator
// ============================================================
class LevelGenerator {
public:
    LevelGenSettings settings;

    void Generate(Scene& scene) {
        scene.Clear();
        InitRandom();

        // 1. Ground
        AddGround(scene);

        // 2. Perimeter walls
        AddPerimeterWalls(scene);

        // 3. Compute city block layout
        ComputeBlockGrid();

        // 4. Buildings in blocks
        for (auto& block : m_blocks) {
            if (RandFloat() < settings.buildingChance) {
                AddBuilding(scene, block);
            }
        }

        // 5. Cover objects in streets
        AddStreetCover(scene);

        // 6. Detail props
        AddDetails(scene);
    }

    // Get the seed that was actually used (useful when seed=0 auto-picks)
    uint32_t GetUsedSeed() const { return m_seed; }

private:
    // ---- Random helpers ----
    uint32_t m_seed = 0;

    void InitRandom() {
        if (settings.seed == 0) {
            m_seed = static_cast<uint32_t>(time(nullptr)) ^ 0xDEADBEEF;
        } else {
            m_seed = settings.seed;
        }
        srand(m_seed);
    }

    float RandFloat() {
        return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    }

    float RandRange(float lo, float hi) {
        return lo + RandFloat() * (hi - lo);
    }

    int RandInt(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + (rand() % (hi - lo + 1));
    }

    // ---- Block layout ----
    struct Block {
        float cx, cz;    // Center
        float w, d;      // Width, depth
    };
    std::vector<Block> m_blocks;

    void ComputeBlockGrid() {
        m_blocks.clear();
        float arena = settings.arenaSize;
        float half  = arena * 0.5f;
        float sw    = settings.streetWidth;
        int cols = (std::max)(2, (std::min)(settings.gridCols, 5));
        int rows = (std::max)(2, (std::min)(settings.gridRows, 5));

        // Usable interior (inside perimeter walls, with margin for streets at edges)
        float margin = sw; // street at perimeter edge
        float usableW = arena - 2.0f * margin - (cols - 1) * sw;
        float usableD = arena - 2.0f * margin - (rows - 1) * sw;
        float blockW  = usableW / cols;
        float blockD  = usableD / rows;

        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                Block b;
                b.w = blockW;
                b.d = blockD;
                b.cx = -half + margin + c * (blockW + sw) + blockW * 0.5f;
                b.cz = -half + margin + r * (blockD + sw) + blockD * 0.5f;
                m_blocks.push_back(b);
            }
        }
    }

    // ---- Entity helpers ----
    void SetEntity(Entity& e, const char* name, float px, float py, float pz,
                   float sx, float sy, float sz, float ry = 0.0f) {
        e.name = name;
        e.position[0] = px; e.position[1] = py; e.position[2] = pz;
        e.scale[0] = sx; e.scale[1] = sy; e.scale[2] = sz;
        e.rotation[1] = ry;
    }

    void SetColor(Entity& e, float r, float g, float b, float a = 1.0f) {
        e.color[0] = r; e.color[1] = g; e.color[2] = b; e.color[3] = a;
    }

    void SetDestructible(Entity& e, float hp, MaterialType mat, int debris = 6, float debrisS = 0.3f) {
        e.destructible = true;
        e.health = hp;
        e.maxHealth = hp;
        e.materialType = mat;
        e.debrisCount = debris;
        e.debrisScale = debrisS;
    }

    // ---- 1. Ground ----
    void AddGround(Scene& scene) {
        float half = settings.arenaSize;
        int idx = scene.AddEntity("Ground", MeshType::Cube);
        auto& g = scene.GetEntity(idx);
        SetEntity(g, "Ground", 0, -0.25f, 0, half, 0.5f, half);
        SetColor(g, 0.35f, 0.33f, 0.30f);
        g.textureName = "Ground/texture";
        g.destructible = false;
        g.castShadow = false;
    }

    // ---- 2. Perimeter walls ----
    void AddPerimeterWalls(Scene& scene) {
        float half = settings.arenaSize * 0.5f;
        float h    = settings.wallHeight;
        float t    = settings.wallThickness;
        float len  = settings.arenaSize;
        float py   = h * 0.5f;

        struct WallDef { const char* name; float px, pz, sx, sz; float ry; };
        WallDef walls[] = {
            { "Wall_North",  0,     half,  len, t, 0   },
            { "Wall_South",  0,    -half,  len, t, 0   },
            { "Wall_East",   half,  0,     len, t, 90  },
            { "Wall_West",  -half,  0,     len, t, 90  },
        };
        for (auto& w : walls) {
            int idx = scene.AddEntity(w.name, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            SetEntity(e, w.name, w.px, py, w.pz, w.sx, h, w.sz, w.ry);
            SetColor(e, 0.45f, 0.43f, 0.40f);
            e.textureName = "Walls/texture";
            SetDestructible(e, 500, MaterialType::Concrete, 8, 0.4f);
        }
    }

    // ---- 4. Building ----
    void AddBuilding(Scene& scene, const Block& block) {
        // Shrink building slightly inside the block
        float inset = 0.3f;
        float bw = block.w - inset * 2.0f;
        float bd = block.d - inset * 2.0f;
        float bh = RandRange(settings.buildingMinH, settings.buildingMaxH);
        float t  = settings.wallThickness;
        float py = bh * 0.5f;
        float cx = block.cx;
        float cz = block.cz;

        // Vary color slightly
        float cr = RandRange(0.38f, 0.52f);
        float cg = cr - RandRange(0.0f, 0.04f);
        float cb = cg - RandRange(0.0f, 0.04f);

        float hp = 150.0f + bh * 30.0f;
        static int bldgId = 0;
        bldgId++;
        std::string prefix = "Bldg" + std::to_string(bldgId);

        // Decide how many walls to place (sometimes leave openings for doors)
        bool hasDoor[4] = { false, false, false, false }; // N, S, E, W
        int doorSide = RandInt(0, 3);
        hasDoor[doorSide] = true;
        // Sometimes a second opening
        if (RandFloat() < 0.4f) {
            int door2 = (doorSide + 2) % 4; // opposite side
            hasDoor[door2] = true;
        }

        // Wall definitions: N, S, E, W
        struct WallInfo {
            float px, pz, sx, sz, ry;
            const char* suffix;
        };
        WallInfo wallDefs[4] = {
            { cx,          cz + bd * 0.5f, bw, t, 0,  "_WallN" },
            { cx,          cz - bd * 0.5f, bw, t, 0,  "_WallS" },
            { cx + bw * 0.5f, cz,          bd, t, 90, "_WallE" },
            { cx - bw * 0.5f, cz,          bd, t, 90, "_WallW" },
        };

        for (int wi = 0; wi < 4; wi++) {
            if (hasDoor[wi]) {
                // Split wall into two halves with a gap for door
                float doorGap = RandRange(1.2f, 2.0f);
                float wallLen = (wi < 2) ? bw : bd;
                float halfLen = (wallLen - doorGap) * 0.5f;
                if (halfLen < 0.5f) continue; // too small to split

                for (int side = 0; side < 2; side++) {
                    std::string name = prefix + wallDefs[wi].suffix + (side == 0 ? "L" : "R");
                    int idx = scene.AddEntity(name, MeshType::Cube);
                    auto& e = scene.GetEntity(idx);

                    float offset = (halfLen + doorGap) * 0.5f * (side == 0 ? -1.0f : 1.0f);
                    float px = wallDefs[wi].px;
                    float pz = wallDefs[wi].pz;
                    if (wi < 2) px += offset; // N/S walls: offset along X
                    else        pz += offset; // E/W walls: offset along Z

                    float sx = (wi < 2) ? halfLen : wallDefs[wi].sz;
                    float sz = (wi < 2) ? wallDefs[wi].sz : halfLen;

                    SetEntity(e, name.c_str(), px, py, pz, sx, bh, sz, wallDefs[wi].ry);
                    SetColor(e, cr, cg, cb);
                    e.textureName = "Walls/texture";
                    SetDestructible(e, hp, MaterialType::Concrete, 6, 0.3f);
                }
            } else {
                std::string name = prefix + wallDefs[wi].suffix;
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                SetEntity(e, name.c_str(), wallDefs[wi].px, py, wallDefs[wi].pz,
                         wallDefs[wi].sx, bh, wallDefs[wi].sz, wallDefs[wi].ry);
                SetColor(e, cr, cg, cb);
                e.textureName = "Walls/texture";
                SetDestructible(e, hp, MaterialType::Concrete, 6, 0.3f);
            }
        }

        // Roof
        if (RandFloat() < settings.roofChance) {
            std::string name = prefix + "_Roof";
            int idx = scene.AddEntity(name, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            float roofY = bh + 0.15f;
            SetEntity(e, name.c_str(), cx, roofY, cz, bw + 0.5f, 0.3f, bd + 0.5f);
            SetColor(e, cr * 0.85f, cg * 0.85f, cb * 0.85f);
            e.textureName = "Walls/texture";
            SetDestructible(e, hp * 0.5f, MaterialType::Concrete, 4, 0.3f);
        }

        // Interior detail: sometimes a crate or table inside
        if (RandFloat() < 0.6f) {
            float ix = cx + RandRange(-bw * 0.25f, bw * 0.25f);
            float iz = cz + RandRange(-bd * 0.25f, bd * 0.25f);
            std::string name = prefix + "_Crate";
            int idx = scene.AddEntity(name, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            float cs = RandRange(0.6f, 1.2f);
            SetEntity(e, name.c_str(), ix, cs * 0.5f, iz, cs, cs, cs, RandRange(-20, 20));
            SetColor(e, 0.5f, 0.4f, 0.25f);
            SetDestructible(e, 60, MaterialType::Wood, 4, 0.2f);
        }

        // Windows on solid walls
        if (settings.addWindows) {
            for (int wi = 0; wi < 4; wi++) {
                if (hasDoor[wi]) continue; // no window on door walls
                if (RandFloat() < 0.4f) continue; // skip some walls

                std::string name = prefix + "_Win" + std::to_string(wi);
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);

                // Position slightly in front of the wall
                float wx = wallDefs[wi].px;
                float wz = wallDefs[wi].pz;
                float offset = (t * 0.5f + 0.05f);
                if (wi == 0)      wz -= offset; // North wall, window on inside face
                else if (wi == 1) wz += offset;
                else if (wi == 2) wx -= offset;
                else              wx += offset;

                float ww = RandRange(1.2f, 2.5f);
                float wh = RandRange(1.0f, 1.8f);
                float winY = bh * 0.5f + RandRange(-0.3f, 0.5f);

                SetEntity(e, name.c_str(), wx, winY, wz, ww, wh, 0.08f, wallDefs[wi].ry);
                SetColor(e, 0.55f, 0.7f, 0.82f, 0.6f);
                SetDestructible(e, 25, MaterialType::Glass, 8, 0.12f);
                e.castShadow = false;
            }
        }
    }

    // ---- 5. Street cover ----
    void AddStreetCover(Scene& scene) {
        float half = settings.arenaSize * 0.5f;
        int count = settings.coverDensity;

        for (int i = 0; i < count; i++) {
            float px = RandRange(-half + 2.0f, half - 2.0f);
            float pz = RandRange(-half + 2.0f, half - 2.0f);

            // Skip if inside a building block
            bool insideBuilding = false;
            for (auto& b : m_blocks) {
                if (std::abs(px - b.cx) < b.w * 0.4f && std::abs(pz - b.cz) < b.d * 0.4f) {
                    insideBuilding = true;
                    break;
                }
            }
            if (insideBuilding) continue;

            int coverType = RandInt(0, 4);
            std::string name = "Cover_" + std::to_string(i);
            int idx = scene.AddEntity(name, MeshType::Cube);
            auto& e = scene.GetEntity(idx);

            switch (coverType) {
                case 0: // Concrete barrier  
                {
                    float w = RandRange(2.0f, 4.5f);
                    SetEntity(e, name.c_str(), px, 0.5f, pz, w, 1.0f, 0.4f, RandRange(-30, 30));
                    SetColor(e, 0.48f, 0.46f, 0.42f);
                    e.textureName = "Walls/texture";
                    SetDestructible(e, 200, MaterialType::Concrete, 6, 0.25f);
                    break;
                }
                case 1: // Crate
                {
                    float s = RandRange(0.7f, 1.3f);
                    SetEntity(e, name.c_str(), px, s * 0.5f, pz, s, s, s, RandRange(-20, 20));
                    SetColor(e, 0.5f, 0.4f, 0.22f);
                    SetDestructible(e, 60, MaterialType::Wood, 4, 0.2f);
                    break;
                }
                case 2: // Barrel
                {
                    SetEntity(e, name.c_str(), px, 0.6f, pz, 0.5f, 1.2f, 0.5f);
                    SetColor(e, 0.35f, 0.38f, 0.32f);
                    SetDestructible(e, 80, MaterialType::Metal, 4, 0.15f);
                    break;
                }
                case 3: // Low wall / sandbag
                {
                    float w = RandRange(2.5f, 5.0f);
                    SetEntity(e, name.c_str(), px, 0.5f, pz, w, 1.0f, 0.8f, RandRange(-15, 15));
                    SetColor(e, 0.45f, 0.40f, 0.30f);
                    SetDestructible(e, 150, MaterialType::Concrete, 5, 0.3f);
                    break;
                }
                case 4: // Metal plate / cover
                {
                    SetEntity(e, name.c_str(), px, 0.75f, pz, 2.0f, 1.5f, 0.15f, RandRange(-25, 25));
                    SetColor(e, 0.4f, 0.42f, 0.45f);
                    SetDestructible(e, 200, MaterialType::Metal, 6, 0.25f);
                    break;
                }
            }
        }
    }

    // ---- 6. Detail props ----
    void AddDetails(Scene& scene) {
        float half = settings.arenaSize * 0.5f;
        int count = settings.detailDensity;

        for (int i = 0; i < count; i++) {
            float px = RandRange(-half + 2.0f, half - 2.0f);
            float pz = RandRange(-half + 2.0f, half - 2.0f);

            // Skip if deep inside a building
            bool insideBuilding = false;
            for (auto& b : m_blocks) {
                if (std::abs(px - b.cx) < b.w * 0.3f && std::abs(pz - b.cz) < b.d * 0.3f) {
                    insideBuilding = true;
                    break;
                }
            }
            if (insideBuilding) continue;

            int detailType = RandInt(0, 4);
            std::string name = "Detail_" + std::to_string(i);
            int idx = scene.AddEntity(name, MeshType::Cube);
            auto& e = scene.GetEntity(idx);

            switch (detailType) {
                case 0: // Small rubble
                {
                    float s = RandRange(0.2f, 0.5f);
                    SetEntity(e, name.c_str(), px, s * 0.5f, pz, s, s * 0.6f, s, RandRange(0, 360));
                    SetColor(e, 0.42f, 0.40f, 0.38f);
                    SetDestructible(e, 30, MaterialType::Concrete, 2, 0.1f);
                    break;
                }
                case 1: // Wooden plank
                {
                    float w = RandRange(1.5f, 3.0f);
                    SetEntity(e, name.c_str(), px, 0.1f, pz, w, 0.1f, 0.3f, RandRange(0, 180));
                    SetColor(e, 0.48f, 0.35f, 0.18f);
                    SetDestructible(e, 20, MaterialType::Wood, 2, 0.1f);
                    e.castShadow = false;
                    break;
                }
                case 2: // Barrel cluster (single barrel)
                {
                    SetEntity(e, name.c_str(), px, 0.5f, pz, 0.5f, 1.0f, 0.5f);
                    SetColor(e, 0.3f + RandFloat() * 0.15f, 0.25f + RandFloat() * 0.1f, 0.15f);
                    SetDestructible(e, 50, MaterialType::Metal, 3, 0.15f);
                    break;
                }
                case 3: // Fence segment
                {
                    if (!settings.addFences) { detailType = 0; break; }
                    float w = RandRange(2.0f, 4.0f);
                    SetEntity(e, name.c_str(), px, 0.5f, pz, w, 1.0f, 0.1f, RandRange(-10, 10));
                    SetColor(e, 0.5f, 0.38f, 0.18f);
                    SetDestructible(e, 40, MaterialType::Wood, 4, 0.18f);
                    break;
                }
                case 4: // Small concrete block
                {
                    float s = RandRange(0.5f, 1.5f);
                    SetEntity(e, name.c_str(), px, s * 0.3f, pz, s, s * 0.6f, s * 0.8f, RandRange(0, 30));
                    SetColor(e, 0.5f, 0.48f, 0.44f);
                    SetDestructible(e, 100, MaterialType::Concrete, 3, 0.2f);
                    break;
                }
            }
        }
    }
};

} // namespace WT
