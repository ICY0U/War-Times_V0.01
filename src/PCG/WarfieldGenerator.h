#pragma once
// ============================================================
// WarfieldGenerator — Massive open-world Battlefield/BattleBit-style
// level generator with terrain, towns, war zones, and cover
// ============================================================

#include "Core/Entity.h"
#include "Core/ResourceManager.h"
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>

namespace WT {

// ============================================================
// TerrainHeightmap — multi-octave noise with bilinear sampling
// ============================================================
class TerrainHeightmap {
public:
    int   resolution = 0;
    float worldSize  = 0;
    std::vector<float> heights;

    void Generate(int res, float size, float amplitude, float freq, uint32_t seed) {
        resolution = res;
        worldSize  = size;
        heights.resize(res * res, 0.0f);

        for (int z = 0; z < res; z++) {
            for (int x = 0; x < res; x++) {
                float wx = (float(x) / float(res - 1)) * size - size * 0.5f;
                float wz = (float(z) / float(res - 1)) * size - size * 0.5f;
                float h = 0.0f;
                // 5 octaves for richer terrain
                h += Noise2D(wx * freq * 0.25f, wz * freq * 0.25f, seed)     * amplitude * 1.5f;
                h += Noise2D(wx * freq * 0.5f,  wz * freq * 0.5f,  seed + 1) * amplitude;
                h += Noise2D(wx * freq * 1.0f,  wz * freq * 1.0f,  seed + 2) * amplitude * 0.5f;
                h += Noise2D(wx * freq * 2.0f,  wz * freq * 2.0f,  seed + 3) * amplitude * 0.25f;
                h += Noise2D(wx * freq * 4.0f,  wz * freq * 4.0f,  seed + 4) * amplitude * 0.1f;
                heights[z * res + x] = h;
            }
        }
    }

    void Flatten(float cx, float cz, float radius, float targetH) {
        float halfSize = worldSize * 0.5f;
        for (int z = 0; z < resolution; z++) {
            for (int x = 0; x < resolution; x++) {
                float wx = (float(x) / float(resolution - 1)) * worldSize - halfSize;
                float wz = (float(z) / float(resolution - 1)) * worldSize - halfSize;
                float dist = sqrtf((wx - cx) * (wx - cx) + (wz - cz) * (wz - cz));
                if (dist < radius) {
                    float blend = dist / radius;
                    blend = blend * blend * blend; // cubic falloff for smoother edges
                    heights[z * resolution + x] = heights[z * resolution + x] * blend + targetH * (1.0f - blend);
                }
            }
        }
    }

    // Carve a valley/trench along the heightmap
    void Carve(float cx, float cz, float radius, float depth) {
        float halfSize = worldSize * 0.5f;
        for (int z = 0; z < resolution; z++) {
            for (int x = 0; x < resolution; x++) {
                float wx = (float(x) / float(resolution - 1)) * worldSize - halfSize;
                float wz = (float(z) / float(resolution - 1)) * worldSize - halfSize;
                float dist = sqrtf((wx - cx) * (wx - cx) + (wz - cz) * (wz - cz));
                if (dist < radius) {
                    float blend = 1.0f - (dist / radius);
                    blend = blend * blend;
                    heights[z * resolution + x] -= depth * blend;
                }
            }
        }
    }

    float Sample(float wx, float wz) const {
        if (resolution < 2) return 0.0f;
        float halfSize = worldSize * 0.5f;
        float u = (wx + halfSize) / worldSize * float(resolution - 1);
        float v = (wz + halfSize) / worldSize * float(resolution - 1);
        int x0 = (std::max)(0, (std::min)(int(u), resolution - 2));
        int z0 = (std::max)(0, (std::min)(int(v), resolution - 2));
        float fx = u - float(x0);
        float fz = v - float(z0);
        float h00 = heights[z0 * resolution + x0];
        float h10 = heights[z0 * resolution + x0 + 1];
        float h01 = heights[(z0 + 1) * resolution + x0];
        float h11 = heights[(z0 + 1) * resolution + x0 + 1];
        return (h00 * (1 - fx) + h10 * fx) * (1 - fz) + (h01 * (1 - fx) + h11 * fx) * fz;
    }

private:
    static float Noise2D(float x, float y, uint32_t seed) {
        int ix = (int)floorf(x); int iy = (int)floorf(y);
        float fx = x - floorf(x); float fy = y - floorf(y);
        fx = fx * fx * (3.0f - 2.0f * fx);
        fy = fy * fy * (3.0f - 2.0f * fy);
        float n00 = HashFloat(ix, iy, seed), n10 = HashFloat(ix+1, iy, seed);
        float n01 = HashFloat(ix, iy+1, seed), n11 = HashFloat(ix+1, iy+1, seed);
        return (n00*(1-fx)+n10*fx)*(1-fy) + (n01*(1-fx)+n11*fx)*fy;
    }
    static float HashFloat(int x, int y, uint32_t seed) {
        uint32_t h = seed ^ (uint32_t(x)*374761393u) ^ (uint32_t(y)*668265263u);
        h = (h ^ (h >> 13)) * 1103515245u; h ^= h >> 16;
        return (float(h & 0x7FFFFFFF) / float(0x7FFFFFFF)) * 2.0f - 1.0f;
    }
};

// ============================================================
// Settings
// ============================================================
struct WarfieldSettings {
    uint32_t seed          = 0;
    float    mapSize       = 300.0f;
    int      terrainRes    = 128;
    float    terrainHeight = 12.0f;
    float    terrainFreq   = 0.02f;
    int      townCount     = 4;
    int      townMinBlocks = 2;
    int      townMaxBlocks = 5;
    float    buildingMinH  = 2.5f;
    float    buildingMaxH  = 6.0f;
    float    streetWidth   = 4.0f;
    int      outpostCount  = 6;
    int      forestClusters = 8;
    int      treesPerCluster = 15;
    int      fieldCover    = 40;
    float    buildingChance = 0.75f;
    float    roofChance     = 0.7f;
    bool     addForests    = true;
    bool     addOutposts   = true;
};

// ============================================================
// WarfieldGenerator
// ============================================================
class WarfieldGenerator {
public:
    WarfieldSettings settings;
    TerrainHeightmap heightmap;

    void Generate(Scene& scene) {
        scene.Clear();
        InitRandom();

        heightmap.Generate(settings.terrainRes, settings.mapSize,
                          settings.terrainHeight, settings.terrainFreq, m_seed);

        PlaceTowns();

        // Flatten under towns
        for (auto& t : m_towns) {
            float flatR = t.radius * 1.4f;
            t.baseY = heightmap.Sample(t.cx, t.cz);
            t.baseY = (std::max)(-1.0f, (std::min)(t.baseY, 2.0f));
            heightmap.Flatten(t.cx, t.cz, flatR, t.baseY);
        }

        // Carve craters across the battlefield
        PlaceCraters();

        // --- Build world ---
        AddTerrainChunks(scene);

        for (auto& t : m_towns) AddTown(scene, t);

        if (settings.addOutposts) AddOutposts(scene);
        if (settings.addForests) AddForests(scene);

        AddCraterDebris(scene);
        AddFieldCover(scene);
        AddWatchtowers(scene);
        AddBoundary(scene);
    }

    uint32_t GetUsedSeed() const { return m_seed; }
    float SampleHeight(float wx, float wz) const { return heightmap.Sample(wx, wz); }

private:
    uint32_t m_seed = 0;
    void InitRandom() {
        m_seed = settings.seed ? settings.seed : (uint32_t(time(nullptr)) ^ 0xBEEFCAFE);
        srand(m_seed);
    }
    float RandFloat() { return float(rand()) / float(RAND_MAX); }
    float RandRange(float lo, float hi) { return lo + RandFloat() * (hi - lo); }
    int   RandInt(int lo, int hi) { return hi <= lo ? lo : lo + rand() % (hi - lo + 1); }

    // ---- Internal data ----
    struct Town { float cx, cz, radius, baseY; int blockCols, blockRows; };
    struct CraterInfo { float x, z, radius, depth; };

    std::vector<Town> m_towns;
    std::vector<CraterInfo> m_craters;

    // ========================================================
    // TOWNS — Poisson-distributed clusters
    // ========================================================
    void PlaceTowns() {
        m_towns.clear();
        float half = settings.mapSize * 0.5f;
        float margin = settings.mapSize * 0.12f;
        int count = (std::max)(2, (std::min)(settings.townCount, 8));

        for (int i = 0; i < count; i++) {
            for (int attempt = 0; attempt < 200; attempt++) {
                Town t;
                t.blockCols = RandInt(settings.townMinBlocks, settings.townMaxBlocks);
                t.blockRows = RandInt(settings.townMinBlocks, settings.townMaxBlocks);
                float blockSize = 12.0f + settings.streetWidth;
                t.radius = (std::max)(float(t.blockCols), float(t.blockRows)) * blockSize * 0.5f;
                t.cx = RandRange(-half + margin + t.radius, half - margin - t.radius);
                t.cz = RandRange(-half + margin + t.radius, half - margin - t.radius);
                t.baseY = 0;
                bool ok = true;
                for (auto& o : m_towns) {
                    float d = sqrtf((t.cx-o.cx)*(t.cx-o.cx)+(t.cz-o.cz)*(t.cz-o.cz));
                    if (d < (t.radius+o.radius)*1.6f) { ok = false; break; }
                }
                if (ok) { m_towns.push_back(t); break; }
            }
        }
    }

    // ========================================================
    // CRATERS — bomb craters across the battlefield
    // ========================================================
    void PlaceCraters() {
        m_craters.clear();
        int count = (int)(settings.mapSize * 0.1f); // ~30 for 300 map
        float half = settings.mapSize * 0.5f;
        for (int i = 0; i < count; i++) {
            CraterInfo c;
            c.x = RandRange(-half * 0.85f, half * 0.85f);
            c.z = RandRange(-half * 0.85f, half * 0.85f);
            c.radius = RandRange(2.0f, 6.0f);
            c.depth = RandRange(0.8f, 2.5f);
            // Skip if in a town center
            bool inTown = false;
            for (auto& t : m_towns) {
                float d = sqrtf((c.x-t.cx)*(c.x-t.cx)+(c.z-t.cz)*(c.z-t.cz));
                if (d < t.radius * 0.5f) { inTown = true; break; }
            }
            if (!inTown) {
                heightmap.Carve(c.x, c.z, c.radius, c.depth);
                m_craters.push_back(c);
            }
        }
    }

    bool IsInTown(float wx, float wz) const {
        for (auto& t : m_towns) {
            float d = sqrtf((wx-t.cx)*(wx-t.cx)+(wz-t.cz)*(wz-t.cz));
            if (d < t.radius) return true;
        }
        return false;
    }

    // ========================================================
    // TERRAIN — chunked cubes with thick visible height
    // ========================================================
    void AddTerrainChunks(Scene& scene) {
        float half = settings.mapSize * 0.5f;
        float chunkSize = 24.0f;  // Larger chunks = fewer entities (was 8.0)
        int chunkCount = (int)(settings.mapSize / chunkSize);

        for (int cz = 0; cz < chunkCount; cz++) {
            for (int cx = 0; cx < chunkCount; cx++) {
                float wx = -half + (float(cx) + 0.5f) * chunkSize;
                float wz = -half + (float(cz) + 0.5f) * chunkSize;
                float h = heightmap.Sample(wx, wz);

                // Thick terrain block — shows elevation from below
                float thickness = (std::max)(2.0f, h + settings.terrainHeight + 4.0f);

                std::string name = "T_" + std::to_string(cx) + "_" + std::to_string(cz);
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                e.position[0] = wx;
                e.position[1] = h - thickness * 0.5f;
                e.position[2] = wz;
                e.scale[0] = chunkSize + 0.05f; // Tiny overlap to hide seams
                e.scale[1] = thickness;
                e.scale[2] = chunkSize + 0.05f;

                // Color based on biome
                bool inTown = IsInTown(wx, wz);

                float normalizedH = (h + settings.terrainHeight) / (settings.terrainHeight * 2.5f);
                normalizedH = (std::max)(0.0f, (std::min)(normalizedH, 1.0f));

                // Per-chunk noise for micro-variation
                float nv = (HashVal(cx * 7 + cz * 13 + m_seed) & 0xFF) / 255.0f * 0.08f;

                if (inTown) {
                    // Town ground — dusty/paved
                    e.color[0] = 0.38f + nv; e.color[1] = 0.36f + nv; e.color[2] = 0.30f + nv; e.color[3] = 1;
                    e.textureName = "Floors/texture";
                } else if (normalizedH > 0.7f) {
                    // Rocky peaks — gray/brown
                    float g = 0.38f + nv;
                    e.color[0] = g + 0.02f; e.color[1] = g; e.color[2] = g - 0.04f; e.color[3] = 1;
                    e.textureName = "Walls/texture";
                } else if (normalizedH > 0.5f) {
                    // Hill grass — yellow-green
                    e.color[0] = 0.35f + nv; e.color[1] = 0.40f + nv; e.color[2] = 0.22f + nv; e.color[3] = 1;
                    e.textureName = "Floors/texture";
                } else {
                    // Lowland grass — green
                    e.color[0] = 0.22f + nv; e.color[1] = 0.42f + nv; e.color[2] = 0.18f + nv; e.color[3] = 1;
                    e.textureName = "Floors/texture";
                }

                e.destructible = false;
                e.castShadow = false;
            }
        }
    }

    static uint32_t HashVal(int v) {
        uint32_t h = uint32_t(v) * 2654435761u;
        h ^= h >> 16; return h;
    }

    // ========================================================
    // TOWN — compound buildings with interiors
    // ========================================================
    void AddTown(Scene& scene, const Town& town) {
        float sw = settings.streetWidth;
        float blockSize = 12.0f;
        float townW = float(town.blockCols) * (blockSize + sw);
        float townD = float(town.blockRows) * (blockSize + sw);

        static int townId = 0;
        townId++;
        std::string pre = "Town" + std::to_string(townId);

        for (int r = 0; r < town.blockRows; r++) {
            for (int c = 0; c < town.blockCols; c++) {
                float bx = town.cx - townW*0.5f + (float(c)+0.5f) * (blockSize+sw);
                float bz = town.cz - townD*0.5f + (float(r)+0.5f) * (blockSize+sw);
                float by = heightmap.Sample(bx, bz);

                if (RandFloat() > settings.buildingChance) {
                    // Empty lot — add some rubble instead
                    AddRubblePile(scene, pre, bx, by, bz, 4.0f);
                    continue;
                }

                int buildType = RandInt(0, 4);
                switch (buildType) {
                    case 0: AddHouse(scene, pre, bx, by, bz, blockSize); break;
                    case 1: AddWarehouse(scene, pre, bx, by, bz, blockSize); break;
                    case 2: AddRuinedBuilding(scene, pre, bx, by, bz, blockSize); break;
                    case 3: AddMultiStory(scene, pre, bx, by, bz, blockSize); break;
                    default: AddHouse(scene, pre, bx, by, bz, blockSize); break;
                }
            }
        }

        // Town center feature — flag pole or monument
        AddTownCenter(scene, pre, town.cx, town.baseY, town.cz);
    }

    // --- House: simple 4-wall + roof + door + windows ---
    void AddHouse(Scene& scene, const std::string& pre, float cx, float by, float cz, float lot) {
        static int bid = 0; bid++;
        std::string bname = pre + "_House" + std::to_string(bid);

        float bw = RandRange(lot*0.45f, lot*0.7f);
        float bd = RandRange(lot*0.45f, lot*0.7f);
        float bh = RandRange(settings.buildingMinH, settings.buildingMaxH);
        float wt = 0.3f;
        float hp = 150.0f + bh * 30.0f;

        float cr = RandRange(0.50f, 0.70f);
        float cg = cr - RandRange(-0.05f, 0.1f);
        float cb = cg - RandRange(0.0f, 0.1f);

        int doorSide = RandInt(0, 3);
        AddBoxWalls(scene, bname, cx, by, cz, bw, bd, bh, wt, doorSide, cr, cg, cb, hp);

        // Roof
        if (RandFloat() < settings.roofChance) {
            AddRoof(scene, bname + "_Roof", cx, by + bh, cz, bw + 0.6f, bd + 0.6f, cr * 0.7f, cg * 0.6f, cb * 0.5f);
        }

        // Floor
        AddFloor(scene, bname + "_Floor", cx, by, cz, bw - 0.1f, bd - 0.1f);

        // Interior wall (splits room in half)
        if (bw > 3.0f && bd > 3.0f) {
            std::string iname = bname + "_IWall";
            int idx = scene.AddEntity(iname, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            bool splitX = RandFloat() > 0.5f;
            float offset = RandRange(-0.5f, 0.5f);
            e.position[0] = cx + (splitX ? offset : 0);
            e.position[1] = by + bh * 0.5f;
            e.position[2] = cz + (splitX ? 0 : offset);
            e.scale[0] = splitX ? wt : bw * 0.85f;
            e.scale[1] = bh - 0.3f;
            e.scale[2] = splitX ? bd * 0.85f : wt;
            e.color[0] = cr + 0.05f; e.color[1] = cg + 0.05f; e.color[2] = cb + 0.05f; e.color[3] = 1;
            e.textureName = "Walls/texture";
            e.destructible = true; e.health = hp * 0.5f; e.maxHealth = hp * 0.5f;
            e.materialType = MaterialType::Concrete;
            e.debrisCount = 4; e.debrisScale = 0.25f;
        }

        // Windows (holes represented as small cubes on walls)
        AddWindowFrames(scene, bname, cx, by, cz, bw, bd, bh, cr * 0.5f, cg * 0.5f, cb * 0.5f, doorSide);
    }

    // --- Warehouse: large open interior with support columns ---
    void AddWarehouse(Scene& scene, const std::string& pre, float cx, float by, float cz, float lot) {
        static int wid = 0; wid++;
        std::string bname = pre + "_Warehouse" + std::to_string(wid);

        float bw = RandRange(lot*0.6f, lot*0.85f);
        float bd = RandRange(lot*0.6f, lot*0.85f);
        float bh = RandRange(3.5f, 5.5f);
        float wt = 0.35f;
        float hp = 200.0f;

        float cr = 0.45f + RandFloat() * 0.1f;
        float cg = cr - 0.02f;
        float cb = cg - 0.04f;

        // Two large openings (loading bays)
        int doorSide = RandInt(0, 3);
        AddBoxWalls(scene, bname, cx, by, cz, bw, bd, bh, wt, doorSide, cr, cg, cb, hp);

        // Large door on opposite side too
        int oppSide = (doorSide + 2) % 4;
        AddDoorOpening(scene, bname + "_Bay", cx, by, cz, bw, bd, bh, wt, oppSide, cr, cg, cb, hp, 3.0f);

        // Roof
        AddRoof(scene, bname + "_Roof", cx, by + bh, cz, bw + 0.3f, bd + 0.3f, cr * 0.6f, cg * 0.55f, cb * 0.5f);
        AddFloor(scene, bname + "_Floor", cx, by, cz, bw, bd);

        // Interior columns
        int cols = (std::max)(1, (int)(bw * bd / 20.0f));
        for (int ci = 0; ci < cols; ci++) {
            std::string cname = bname + "_Col" + std::to_string(ci);
            int idx = scene.AddEntity(cname, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            e.position[0] = cx + RandRange(-bw*0.3f, bw*0.3f);
            e.position[1] = by + bh * 0.5f;
            e.position[2] = cz + RandRange(-bd*0.3f, bd*0.3f);
            e.scale[0] = 0.4f; e.scale[1] = bh; e.scale[2] = 0.4f;
            e.color[0] = 0.5f; e.color[1] = 0.48f; e.color[2] = 0.44f; e.color[3] = 1.0f;
            e.textureName = "Walls/texture";
            e.destructible = true; e.health = 300; e.maxHealth = 300;
            e.materialType = MaterialType::Concrete;
        }

        // Crates inside
        int crates = RandInt(2, 5);
        for (int ci = 0; ci < crates; ci++) {
            std::string cname = bname + "_Crate" + std::to_string(ci);
            int idx = scene.AddEntity(cname, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            float cs = RandRange(0.5f, 1.2f);
            e.position[0] = cx + RandRange(-bw*0.35f, bw*0.35f);
            e.position[1] = by + cs * 0.5f;
            e.position[2] = cz + RandRange(-bd*0.35f, bd*0.35f);
            e.scale[0] = cs * RandRange(0.8f, 1.3f); e.scale[1] = cs; e.scale[2] = cs * RandRange(0.8f, 1.3f);
            e.rotation[1] = RandRange(0, 45);
            e.color[0] = 0.40f; e.color[1] = 0.32f; e.color[2] = 0.18f; e.color[3] = 1;
            e.destructible = true; e.health = 50; e.maxHealth = 50;
            e.materialType = MaterialType::Wood;
            e.debrisCount = 4; e.debrisScale = 0.2f;
        }
    }

    // --- Ruined building: partially destroyed walls, rubble inside ---
    void AddRuinedBuilding(Scene& scene, const std::string& pre, float cx, float by, float cz, float lot) {
        static int rid = 0; rid++;
        std::string bname = pre + "_Ruin" + std::to_string(rid);

        float bw = RandRange(lot*0.4f, lot*0.7f);
        float bd = RandRange(lot*0.4f, lot*0.7f);
        float bh = RandRange(2.0f, 5.0f);
        float wt = 0.3f;
        float hp = 80.0f;

        float cr = 0.42f + RandFloat()*0.05f;
        float cg = cr - 0.03f;
        float cb = cg - 0.05f;

        // Only 2-3 walls standing
        int wallsStanding = RandInt(2, 3);
        int skip1 = RandInt(0, 3);
        int skip2 = (skip1 + RandInt(1, 2)) % 4;

        struct WallInfo { float px, pz, sx, sz; };
        WallInfo walls[4] = {
            { cx,           cz + bd*0.5f, bw, wt },
            { cx,           cz - bd*0.5f, bw, wt },
            { cx + bw*0.5f, cz,           wt, bd },
            { cx - bw*0.5f, cz,           wt, bd },
        };

        for (int wi = 0; wi < 4; wi++) {
            if (wi == skip1 || (wallsStanding < 3 && wi == skip2)) continue;
            // Walls at varied heights (damaged)
            float wallH = bh * RandRange(0.4f, 1.0f);
            std::string name = bname + "_W" + std::to_string(wi);
            int idx = scene.AddEntity(name, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            e.position[0] = walls[wi].px; e.position[1] = by + wallH*0.5f; e.position[2] = walls[wi].pz;
            e.scale[0] = walls[wi].sx; e.scale[1] = wallH; e.scale[2] = walls[wi].sz;
            e.color[0] = cr; e.color[1] = cg; e.color[2] = cb; e.color[3] = 1;
            e.textureName = "Walls/texture";
            e.destructible = true; e.health = hp; e.maxHealth = hp;
            e.materialType = MaterialType::Concrete;
            e.debrisCount = 5; e.debrisScale = 0.3f;
            e.voxelDestruction = true; e.voxelRes = 4;
        }

        // Rubble pile inside
        AddRubblePile(scene, bname, cx, by, cz, bw * 0.6f);

        // Broken floor
        AddFloor(scene, bname + "_Floor", cx, by - 0.1f, cz, bw * 0.8f, bd * 0.8f);
    }

    // --- Multi-story: 2-3 floors with stairs ---
    void AddMultiStory(Scene& scene, const std::string& pre, float cx, float by, float cz, float lot) {
        static int mid = 0; mid++;
        std::string bname = pre + "_Multi" + std::to_string(mid);

        float bw = RandRange(lot*0.5f, lot*0.75f);
        float bd = RandRange(lot*0.5f, lot*0.75f);
        float floorH = RandRange(2.8f, 3.5f);
        int floors = RandInt(2, 3);
        float totalH = floorH * floors;
        float wt = 0.3f;
        float hp = 200.0f + floors * 50.0f;

        float cr = RandRange(0.50f, 0.65f);
        float cg = cr - RandRange(0.0f, 0.08f);
        float cb = cg - RandRange(0.0f, 0.08f);

        int doorSide = RandInt(0, 3);

        // Full-height exterior walls
        AddBoxWalls(scene, bname, cx, by, cz, bw, bd, totalH, wt, doorSide, cr, cg, cb, hp);

        // Floor slabs for each story
        for (int f = 0; f <= floors; f++) {
            float fy = by + float(f) * floorH;
            std::string fname = bname + "_F" + std::to_string(f);
            int idx = scene.AddEntity(fname, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            e.position[0] = cx; e.position[1] = fy; e.position[2] = cz;
            e.scale[0] = bw - 0.1f; e.scale[1] = 0.2f; e.scale[2] = bd - 0.1f;
            e.color[0] = 0.45f; e.color[1] = 0.43f; e.color[2] = 0.40f; e.color[3] = 1;
            e.textureName = "Walls/texture";
            e.destructible = true; e.health = hp * 0.4f; e.maxHealth = hp * 0.4f;
            e.materialType = MaterialType::Concrete;
        }

        // Stairwell — diagonal ramp in corner
        for (int f = 0; f < floors; f++) {
            float sy = by + float(f) * floorH;
            std::string sname = bname + "_Stair" + std::to_string(f);
            int idx = scene.AddEntity(sname, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            float cornerX = cx + bw * 0.3f * (f % 2 == 0 ? 1 : -1);
            float cornerZ = cz + bd * 0.3f;
            e.position[0] = cornerX;
            e.position[1] = sy + floorH * 0.5f;
            e.position[2] = cornerZ;
            e.scale[0] = 1.2f; e.scale[1] = floorH; e.scale[2] = 2.0f;
            e.rotation[0] = 35.0f; // Tilted as ramp
            e.color[0] = 0.5f; e.color[1] = 0.48f; e.color[2] = 0.42f; e.color[3] = 1;
            e.destructible = false;
        }

        // Roof
        if (RandFloat() < settings.roofChance) {
            AddRoof(scene, bname + "_Roof", cx, by + totalH, cz, bw + 0.4f, bd + 0.4f,
                    cr * 0.65f, cg * 0.6f, cb * 0.55f);
        }

        // Windows on each floor
        for (int f = 0; f < floors; f++) {
            float fy = by + float(f) * floorH;
            AddWindowFrames(scene, bname + "_FL" + std::to_string(f), cx, fy, cz,
                           bw, bd, floorH, cr * 0.5f, cg * 0.5f, cb * 0.5f, doorSide);
        }
    }

    // ========================================================
    // Building helpers
    // ========================================================

    // 4 walls with one door opening
    void AddBoxWalls(Scene& scene, const std::string& bname, float cx, float by, float cz,
                     float bw, float bd, float bh, float wt, int doorSide,
                     float cr, float cg, float cb, float hp) {
        struct WDef { float px, pz, sx, sz; const char* s; };
        WDef defs[4] = {
            { cx,           cz + bd*0.5f, bw, wt, "_N" },
            { cx,           cz - bd*0.5f, bw, wt, "_S" },
            { cx + bw*0.5f, cz,           wt, bd, "_E" },
            { cx - bw*0.5f, cz,           wt, bd, "_W" },
        };

        for (int wi = 0; wi < 4; wi++) {
            if (wi == doorSide) {
                AddDoorOpening(scene, bname + defs[wi].s, cx, by, cz, bw, bd, bh, wt,
                               wi, cr, cg, cb, hp, RandRange(1.2f, 2.0f));
            } else {
                std::string name = bname + defs[wi].s;
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                e.position[0] = defs[wi].px; e.position[1] = by + bh*0.5f; e.position[2] = defs[wi].pz;
                e.scale[0] = defs[wi].sx; e.scale[1] = bh; e.scale[2] = defs[wi].sz;
                e.color[0] = cr; e.color[1] = cg; e.color[2] = cb; e.color[3] = 1;
                e.textureName = "Walls/texture";
                e.destructible = true; e.health = hp; e.maxHealth = hp;
                e.materialType = MaterialType::Concrete;
                e.debrisCount = 6; e.debrisScale = 0.3f;
                e.voxelDestruction = (bh > 3.0f); e.voxelRes = 4;
            }
        }
    }

    void AddDoorOpening(Scene& scene, const std::string& bname, float cx, float by, float cz,
                        float bw, float bd, float bh, float wt, int side,
                        float cr, float cg, float cb, float hp, float doorGap) {
        bool isNS = (side < 2);
        float wallLen = isNS ? bw : bd;
        float halfLen = (wallLen - doorGap) * 0.5f;
        if (halfLen < 0.3f) return;

        float wpx = (side == 0) ? cx : (side == 1) ? cx : (side == 2) ? cx + bw*0.5f : cx - bw*0.5f;
        float wpz = (side == 0) ? cz + bd*0.5f : (side == 1) ? cz - bd*0.5f : cz;

        for (int s = 0; s < 2; s++) {
            std::string name = bname + (s ? "R" : "L");
            float offset = (halfLen + doorGap) * 0.5f * (s ? 1.0f : -1.0f);
            int idx = scene.AddEntity(name, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            e.position[0] = wpx + (isNS ? offset : 0);
            e.position[1] = by + bh * 0.5f;
            e.position[2] = wpz + (isNS ? 0 : offset);
            e.scale[0] = isNS ? halfLen : wt;
            e.scale[1] = bh;
            e.scale[2] = isNS ? wt : halfLen;
            e.color[0] = cr; e.color[1] = cg; e.color[2] = cb; e.color[3] = 1;
            e.textureName = "Walls/texture";
            e.destructible = true; e.health = hp; e.maxHealth = hp;
            e.materialType = MaterialType::Concrete;
            e.debrisCount = 5; e.debrisScale = 0.25f;
        }

        // Door lintel above
        {
            std::string name = bname + "_Lintel";
            int idx = scene.AddEntity(name, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            float doorH = (std::min)(bh * 0.65f, 2.2f);
            float lintelH = bh - doorH;
            e.position[0] = wpx; e.position[1] = by + doorH + lintelH*0.5f; e.position[2] = wpz;
            e.scale[0] = isNS ? doorGap + 0.1f : wt;
            e.scale[1] = lintelH;
            e.scale[2] = isNS ? wt : doorGap + 0.1f;
            e.color[0] = cr; e.color[1] = cg; e.color[2] = cb; e.color[3] = 1;
            e.textureName = "Walls/texture";
            e.destructible = true; e.health = hp * 0.3f; e.maxHealth = hp * 0.3f;
            e.materialType = MaterialType::Concrete;
        }
    }

    void AddRoof(Scene& scene, const std::string& name, float cx, float y, float cz,
                 float w, float d, float cr, float cg, float cb) {
        int idx = scene.AddEntity(name, MeshType::Cube);
        auto& e = scene.GetEntity(idx);
        e.position[0] = cx; e.position[1] = y + 0.15f; e.position[2] = cz;
        e.scale[0] = w; e.scale[1] = 0.3f; e.scale[2] = d;
        e.color[0] = cr; e.color[1] = cg; e.color[2] = cb; e.color[3] = 1;
        e.textureName = "Walls/texture";
        e.destructible = true; e.health = 100; e.maxHealth = 100;
        e.materialType = MaterialType::Concrete;
    }

    void AddFloor(Scene& scene, const std::string& name, float cx, float y, float cz, float w, float d) {
        int idx = scene.AddEntity(name, MeshType::Cube);
        auto& e = scene.GetEntity(idx);
        e.position[0] = cx; e.position[1] = y - 0.05f; e.position[2] = cz;
        e.scale[0] = w; e.scale[1] = 0.1f; e.scale[2] = d;
        e.color[0] = 0.42f; e.color[1] = 0.40f; e.color[2] = 0.36f; e.color[3] = 1;
        e.destructible = false;
        e.castShadow = false;
    }

    // Window frames (small cubes on wall faces to suggest window openings)
    void AddWindowFrames(Scene& scene, const std::string& bname, float cx, float by, float cz,
                         float bw, float bd, float bh, float cr, float cg, float cb, int skipSide) {
        if (bh < 2.0f) return;
        float windowY = by + bh * 0.55f;
        float windowS = 0.8f;
        float frameT = 0.12f;

        struct WinDef { float px, pz, dx, dz; float wallLen; };
        WinDef defs[4] = {
            { cx, cz + bd*0.5f + 0.01f, 1, 0, bw },
            { cx, cz - bd*0.5f - 0.01f, 1, 0, bw },
            { cx + bw*0.5f + 0.01f, cz, 0, 1, bd },
            { cx - bw*0.5f - 0.01f, cz, 0, 1, bd },
        };

        int wid = 0;
        for (int wi = 0; wi < 4; wi++) {
            if (wi == skipSide) continue;
            int windowCount = (int)(defs[wi].wallLen / 2.5f);
            if (windowCount < 1) continue;

            for (int wn = 0; wn < windowCount; wn++) {
                float t = (float(wn) + 0.5f) / float(windowCount) - 0.5f;
                float wpx = defs[wi].px + t * defs[wi].wallLen * defs[wi].dx;
                float wpz = defs[wi].pz + t * defs[wi].wallLen * defs[wi].dz;

                // Window sill
                std::string name = bname + "_Win" + std::to_string(wid++);
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                e.position[0] = wpx; e.position[1] = windowY - windowS*0.5f; e.position[2] = wpz;
                e.scale[0] = (defs[wi].dx != 0) ? windowS : frameT;
                e.scale[1] = frameT;
                e.scale[2] = (defs[wi].dz != 0) ? windowS : frameT;
                e.color[0] = cr; e.color[1] = cg; e.color[2] = cb; e.color[3] = 1;
                e.destructible = true; e.health = 20; e.maxHealth = 20;
                e.materialType = MaterialType::Wood;
                e.debrisCount = 2; e.debrisScale = 0.1f;
            }
        }
    }

    void AddRubblePile(Scene& scene, const std::string& pre, float cx, float by, float cz, float spread) {
        int pieces = RandInt(4, 10);
        for (int i = 0; i < pieces; i++) {
            std::string name = pre + "_Rubble" + std::to_string(i);
            int idx = scene.AddEntity(name, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            float s = RandRange(0.3f, 1.5f);
            e.position[0] = cx + RandRange(-spread, spread);
            e.position[1] = by + s * 0.3f;
            e.position[2] = cz + RandRange(-spread, spread);
            e.scale[0] = s * RandRange(0.6f, 1.5f);
            e.scale[1] = s * RandRange(0.3f, 0.8f);
            e.scale[2] = s * RandRange(0.6f, 1.5f);
            e.rotation[0] = RandRange(-15, 15);
            e.rotation[1] = RandRange(0, 360);
            e.rotation[2] = RandRange(-15, 15);
            float g = RandRange(0.35f, 0.5f);
            e.color[0] = g; e.color[1] = g-0.02f; e.color[2] = g-0.04f; e.color[3] = 1;
            e.destructible = false;
        }
    }

    void AddTownCenter(Scene& scene, const std::string& pre, float cx, float by, float cz) {
        // Flagpole
        {
            std::string name = pre + "_Flagpole";
            int idx = scene.AddEntity(name, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            e.position[0] = cx; e.position[1] = by + 4.0f; e.position[2] = cz;
            e.scale[0] = 0.12f; e.scale[1] = 8.0f; e.scale[2] = 0.12f;
            e.color[0] = 0.5f; e.color[1] = 0.48f; e.color[2] = 0.42f; e.color[3] = 1;
            e.destructible = true; e.health = 100; e.maxHealth = 100;
            e.materialType = MaterialType::Metal;
        }
        // Flag
        {
            std::string name = pre + "_Flag";
            int idx = scene.AddEntity(name, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            e.position[0] = cx + 0.65f; e.position[1] = by + 7.5f; e.position[2] = cz;
            e.scale[0] = 1.2f; e.scale[1] = 0.8f; e.scale[2] = 0.05f;
            e.color[0] = 0.7f; e.color[1] = 0.15f; e.color[2] = 0.1f; e.color[3] = 1;
            e.destructible = true; e.health = 20; e.maxHealth = 20;
            e.materialType = MaterialType::Wood;
        }
        // Base platform
        {
            std::string name = pre + "_Platform";
            int idx = scene.AddEntity(name, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            e.position[0] = cx; e.position[1] = by + 0.15f; e.position[2] = cz;
            e.scale[0] = 3.0f; e.scale[1] = 0.3f; e.scale[2] = 3.0f;
            e.color[0] = 0.45f; e.color[1] = 0.43f; e.color[2] = 0.40f; e.color[3] = 1;
            e.destructible = false;
        }
    }

    // ========================================================
    // OUTPOSTS — bunkers with sandbag walls, MG positions
    // ========================================================
    void AddOutposts(Scene& scene) {
        float half = settings.mapSize * 0.5f;
        float margin = settings.mapSize * 0.08f;
        int count = (std::max)(0, (std::min)(settings.outpostCount, 12));

        for (int i = 0; i < count; i++) {
            float ox, oz;
            bool valid = false;
            for (int attempt = 0; attempt < 100; attempt++) {
                ox = RandRange(-half+margin, half-margin);
                oz = RandRange(-half+margin, half-margin);
                bool ok = true;
                for (auto& t : m_towns) {
                    if (sqrtf((ox-t.cx)*(ox-t.cx)+(oz-t.cz)*(oz-t.cz)) < t.radius*1.8f)
                    { ok = false; break; }
                }
                if (ok) { valid = true; break; }
            }
            if (!valid) continue;

            float baseH = heightmap.Sample(ox, oz);
            heightmap.Flatten(ox, oz, 10.0f, baseH);
            std::string pre = "OP" + std::to_string(i);

            // Bunker — half-buried reinforced box
            {
                std::string name = pre + "_Bunker";
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                e.position[0] = ox; e.position[1] = baseH + 0.3f; e.position[2] = oz;
                e.scale[0] = 4.0f; e.scale[1] = 1.8f; e.scale[2] = 3.0f;
                e.color[0] = 0.35f; e.color[1] = 0.33f; e.color[2] = 0.28f; e.color[3] = 1;
                e.textureName = "Walls/texture";
                e.destructible = true; e.health = 400; e.maxHealth = 400;
                e.materialType = MaterialType::Concrete;
                e.debrisCount = 8; e.debrisScale = 0.35f;
                e.voxelDestruction = true; e.voxelRes = 4;
            }

            // Firing slit
            {
                std::string name = pre + "_Slit";
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                e.position[0] = ox; e.position[1] = baseH + 1.0f; e.position[2] = oz + 1.6f;
                e.scale[0] = 1.5f; e.scale[1] = 0.3f; e.scale[2] = 0.2f;
                e.color[0] = 0.1f; e.color[1] = 0.1f; e.color[2] = 0.1f; e.color[3] = 1;
                e.destructible = false; e.noCollision = true; e.castShadow = false;
            }

            // Sandbag ring
            int wallCount = RandInt(4, 8);
            float ringR = RandRange(6.0f, 9.0f);
            for (int w = 0; w < wallCount; w++) {
                float angle = (float(w)/float(wallCount)) * 6.2832f + RandRange(-0.15f, 0.15f);
                float wx = ox + cosf(angle)*ringR;
                float wz = oz + sinf(angle)*ringR;
                float wh = heightmap.Sample(wx, wz);

                std::string name = pre + "_SB" + std::to_string(w);
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                float wallW = RandRange(3.5f, 6.0f);
                e.position[0] = wx; e.position[1] = wh + 0.55f; e.position[2] = wz;
                e.scale[0] = wallW; e.scale[1] = 1.1f; e.scale[2] = 0.7f;
                e.rotation[1] = angle * 57.2958f + 90.0f;
                e.color[0] = 0.48f; e.color[1] = 0.44f; e.color[2] = 0.32f; e.color[3] = 1;
                e.textureName = "Floors/texture";
                e.destructible = true; e.health = 200; e.maxHealth = 200;
                e.materialType = MaterialType::Concrete;
                e.debrisCount = 5; e.debrisScale = 0.25f;
            }

            // Ammo crates
            for (int ac = 0; ac < 3; ac++) {
                float angle = RandFloat() * 6.2832f;
                float dist = RandRange(1.0f, 4.0f);
                std::string name = pre + "_Ammo" + std::to_string(ac);
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                float cs = RandRange(0.5f, 0.9f);
                e.position[0] = ox + cosf(angle)*dist;
                e.position[1] = baseH + cs*0.5f;
                e.position[2] = oz + sinf(angle)*dist;
                e.scale[0] = cs * 1.3f; e.scale[1] = cs; e.scale[2] = cs;
                e.rotation[1] = RandRange(0, 45);
                e.color[0] = 0.28f; e.color[1] = 0.33f; e.color[2] = 0.22f; e.color[3] = 1;
                e.destructible = true; e.health = 60; e.maxHealth = 60;
                e.materialType = MaterialType::Wood;
            }
        }
    }

    // ========================================================
    // FORESTS — varied trees with undergrowth
    // ========================================================
    void AddForests(Scene& scene) {
        float half = settings.mapSize * 0.5f;
        float margin = settings.mapSize * 0.05f;
        int clusters = (std::max)(0, settings.forestClusters);

        for (int f = 0; f < clusters; f++) {
            float fcx, fcz;
            bool valid = false;
            for (int attempt = 0; attempt < 80; attempt++) {
                fcx = RandRange(-half+margin, half-margin);
                fcz = RandRange(-half+margin, half-margin);
                bool ok = true;
                for (auto& t : m_towns) {
                    if (sqrtf((fcx-t.cx)*(fcx-t.cx)+(fcz-t.cz)*(fcz-t.cz)) < t.radius*1.3f)
                    { ok = false; break; }
                }
                if (ok)
                { valid = true; break; }
            }
            if (!valid) continue;

            float clusterR = RandRange(10.0f, 25.0f);
            int treeCount = settings.treesPerCluster;

            for (int t = 0; t < treeCount; t++) {
                float angle = RandFloat() * 6.2832f;
                float dist = RandFloat() * clusterR;
                float tx = fcx + cosf(angle)*dist;
                float tz = fcz + sinf(angle)*dist;
                float th = heightmap.Sample(tx, tz);

                // Varied tree types
                int treeType = RandInt(0, 2);
                float trunkH, trunkR, crownS;

                switch (treeType) {
                    case 0: // Tall pine
                        trunkH = RandRange(5.0f, 8.0f);
                        trunkR = RandRange(0.15f, 0.25f);
                        crownS = RandRange(1.0f, 2.0f);
                        break;
                    case 1: // Broad oak
                        trunkH = RandRange(3.0f, 5.0f);
                        trunkR = RandRange(0.2f, 0.35f);
                        crownS = RandRange(2.0f, 3.5f);
                        break;
                    default: // Small bush/shrub
                        trunkH = RandRange(0.8f, 2.0f);
                        trunkR = RandRange(0.08f, 0.15f);
                        crownS = RandRange(0.8f, 1.5f);
                        break;
                }

                // Trunk
                std::string tname = "Tree_" + std::to_string(f) + "_" + std::to_string(t);
                int idx = scene.AddEntity(tname, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                e.position[0] = tx; e.position[1] = th + trunkH*0.5f; e.position[2] = tz;
                e.scale[0] = trunkR*2; e.scale[1] = trunkH; e.scale[2] = trunkR*2;
                float barkR = 0.30f + RandFloat()*0.1f;
                e.color[0] = barkR; e.color[1] = barkR*0.75f; e.color[2] = barkR*0.45f; e.color[3] = 1;
                e.destructible = true; e.health = 80; e.maxHealth = 80;
                e.materialType = MaterialType::Wood;
                e.debrisCount = 3; e.debrisScale = 0.15f;

                // Crown
                std::string cname = tname + "_C";
                idx = scene.AddEntity(cname, MeshType::Cube);
                auto& crown = scene.GetEntity(idx);
                float crownY;
                if (treeType == 0) {
                    // Pine — conical (tall narrow crown)
                    crown.scale[0] = crownS; crown.scale[1] = crownS * 2.0f; crown.scale[2] = crownS;
                    crownY = th + trunkH - crownS * 0.3f;
                } else if (treeType == 1) {
                    // Oak — wide round crown
                    crown.scale[0] = crownS * 1.3f; crown.scale[1] = crownS; crown.scale[2] = crownS * 1.3f;
                    crownY = th + trunkH - crownS * 0.2f;
                } else {
                    // Bush — low wide
                    crown.scale[0] = crownS * 1.5f; crown.scale[1] = crownS * 0.7f; crown.scale[2] = crownS * 1.5f;
                    crownY = th + trunkH * 0.3f;
                }
                crown.position[0] = tx; crown.position[1] = crownY; crown.position[2] = tz;
                crown.rotation[1] = RandRange(0, 45);
                // Green with variation
                float greenBase = 0.20f + RandFloat() * 0.15f;
                crown.color[0] = greenBase * 0.6f + RandFloat()*0.05f;
                crown.color[1] = greenBase + RandFloat()*0.1f;
                crown.color[2] = greenBase * 0.4f + RandFloat()*0.05f;
                crown.color[3] = 1;
                crown.destructible = true; crown.health = 40; crown.maxHealth = 40;
                crown.materialType = MaterialType::Wood;
                crown.debrisCount = 2; crown.debrisScale = 0.15f;
                crown.castShadow = true;
            }

            // Undergrowth / fallen logs
            int undergrowth = RandInt(3, 8);
            for (int ug = 0; ug < undergrowth; ug++) {
                float angle = RandFloat() * 6.2832f;
                float dist = RandFloat() * clusterR;
                float ugx = fcx + cosf(angle)*dist;
                float ugz = fcz + sinf(angle)*dist;
                float ugh = heightmap.Sample(ugx, ugz);

                std::string name = "Bush_" + std::to_string(f) + "_" + std::to_string(ug);
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                float bs = RandRange(0.5f, 1.2f);
                e.position[0] = ugx; e.position[1] = ugh + bs*0.3f; e.position[2] = ugz;

                if (RandFloat() > 0.5f) {
                    // Low bush
                    e.scale[0] = bs*1.8f; e.scale[1] = bs*0.6f; e.scale[2] = bs*1.8f;
                    float g = 0.18f + RandFloat()*0.1f;
                    e.color[0] = g*0.7f; e.color[1] = g; e.color[2] = g*0.5f; e.color[3] = 1;
                } else {
                    // Fallen log
                    e.scale[0] = RandRange(2.0f, 4.0f); e.scale[1] = bs*0.4f; e.scale[2] = bs*0.4f;
                    e.rotation[1] = RandRange(0, 180);
                    e.color[0] = 0.32f; e.color[1] = 0.24f; e.color[2] = 0.14f; e.color[3] = 1;
                }
                e.destructible = true; e.health = 25; e.maxHealth = 25;
                e.materialType = MaterialType::Wood;
            }
        }
    }

    // ========================================================
    // CRATER DEBRIS — rubble in bomb craters
    // ========================================================
    void AddCraterDebris(Scene& scene) {
        int did = 0;
        for (auto& c : m_craters) {
            // Skip some craters for perf
            if (RandFloat() > 0.6f) continue;

            float h = heightmap.Sample(c.x, c.z);
            int pieces = RandInt(2, 5);
            for (int p = 0; p < pieces; p++) {
                float angle = RandFloat() * 6.2832f;
                float dist = RandFloat() * c.radius * 0.8f;
                float px = c.x + cosf(angle)*dist;
                float pz = c.z + sinf(angle)*dist;
                float ph = heightmap.Sample(px, pz);

                std::string name = "Debris_" + std::to_string(did++);
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                float s = RandRange(0.2f, 0.8f);
                e.position[0] = px; e.position[1] = ph + s*0.3f; e.position[2] = pz;
                e.scale[0] = s*RandRange(0.5f, 1.5f);
                e.scale[1] = s*RandRange(0.3f, 0.7f);
                e.scale[2] = s*RandRange(0.5f, 1.5f);
                e.rotation[0] = RandRange(-20, 20);
                e.rotation[1] = RandRange(0, 360);
                e.rotation[2] = RandRange(-20, 20);
                float g = RandRange(0.25f, 0.40f);
                e.color[0] = g; e.color[1] = g-0.02f; e.color[2] = g-0.04f; e.color[3] = 1;
                e.destructible = false;
            }

            // Scorch mark — dark flat disc
            {
                std::string name = "Scorch_" + std::to_string(did++);
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                e.position[0] = c.x; e.position[1] = h + 0.02f; e.position[2] = c.z;
                e.scale[0] = c.radius * 1.3f; e.scale[1] = 0.02f; e.scale[2] = c.radius * 1.3f;
                e.color[0] = 0.10f; e.color[1] = 0.09f; e.color[2] = 0.08f; e.color[3] = 1;
                e.destructible = false; e.castShadow = false; e.noCollision = true;
            }
        }
    }

    // ========================================================
    // FIELD COVER — varied objects across open landscape
    // ========================================================
    void AddFieldCover(Scene& scene) {
        float half = settings.mapSize * 0.5f;
        float margin = 8.0f;
        int count = settings.fieldCover;

        for (int i = 0; i < count; i++) {
            float px = RandRange(-half+margin, half-margin);
            float pz = RandRange(-half+margin, half-margin);
            float ph = heightmap.Sample(px, pz);

            if (IsInTown(px, pz)) continue;

            int type = RandInt(0, 7);
            std::string name = "Field_" + std::to_string(i);
            int idx = scene.AddEntity(name, MeshType::Cube);
            auto& e = scene.GetEntity(idx);

            switch (type) {
                case 0: { // Large rock
                    float s = RandRange(1.0f, 3.0f);
                    e.position[0]=px; e.position[1]=ph+s*0.3f; e.position[2]=pz;
                    e.scale[0]=s*RandRange(0.8f,1.5f); e.scale[1]=s*RandRange(0.4f,0.8f); e.scale[2]=s*RandRange(0.8f,1.3f);
                    e.rotation[1]=RandRange(0,360);
                    float g=RandRange(0.35f,0.5f);
                    e.color[0]=g; e.color[1]=g-0.03f; e.color[2]=g-0.06f; e.color[3]=1;
                    e.destructible=false;
                    break;
                }
                case 1: { // Hay bale
                    e.position[0]=px; e.position[1]=ph+0.5f; e.position[2]=pz;
                    e.scale[0]=1.2f; e.scale[1]=1.0f; e.scale[2]=1.2f;
                    e.rotation[1]=RandRange(0,360);
                    e.color[0]=0.62f; e.color[1]=0.55f; e.color[2]=0.28f; e.color[3]=1;
                    e.destructible=true; e.health=40; e.maxHealth=40;
                    e.materialType=MaterialType::Wood;
                    break;
                }
                case 2: { // Concrete barrier (Jersey barrier)
                    float w=RandRange(3.0f,6.0f);
                    e.position[0]=px; e.position[1]=ph+0.55f; e.position[2]=pz;
                    e.scale[0]=w; e.scale[1]=1.1f; e.scale[2]=0.5f;
                    e.rotation[1]=RandRange(0,180);
                    e.color[0]=0.50f; e.color[1]=0.48f; e.color[2]=0.44f; e.color[3]=1;
                    e.textureName="Walls/texture";
                    e.destructible=true; e.health=300; e.maxHealth=300;
                    e.materialType=MaterialType::Concrete;
                    e.voxelDestruction=true; e.voxelRes=4;
                    break;
                }
                case 3: { // Burnt vehicle hull
                    e.position[0]=px; e.position[1]=ph+0.7f; e.position[2]=pz;
                    e.scale[0]=RandRange(3.0f,5.0f); e.scale[1]=RandRange(1.3f,2.0f); e.scale[2]=RandRange(1.5f,2.5f);
                    e.rotation[1]=RandRange(0,360);
                    float g=RandRange(0.18f,0.28f);
                    e.color[0]=g; e.color[1]=g-0.02f; e.color[2]=g-0.03f; e.color[3]=1;
                    e.destructible=true; e.health=500; e.maxHealth=500;
                    e.materialType=MaterialType::Metal;
                    e.debrisCount=8; e.debrisScale=0.4f;
                    e.voxelDestruction=true; e.voxelRes=4;
                    break;
                }
                case 4: { // Shipping container
                    e.position[0]=px; e.position[1]=ph+1.3f; e.position[2]=pz;
                    e.scale[0]=6.0f; e.scale[1]=2.6f; e.scale[2]=2.4f;
                    e.rotation[1]=RandRange(0,180);
                    e.color[0]=RandRange(0.25f,0.55f); e.color[1]=RandRange(0.2f,0.4f); e.color[2]=RandRange(0.15f,0.35f); e.color[3]=1;
                    e.destructible=true; e.health=600; e.maxHealth=600;
                    e.materialType=MaterialType::Metal;
                    e.debrisCount=10; e.debrisScale=0.35f;
                    break;
                }
                case 5: { // Wooden fence section
                    float w=RandRange(4.0f,10.0f);
                    e.position[0]=px; e.position[1]=ph+0.6f; e.position[2]=pz;
                    e.scale[0]=w; e.scale[1]=1.2f; e.scale[2]=0.1f;
                    e.rotation[1]=RandRange(0,180);
                    e.color[0]=0.45f; e.color[1]=0.35f; e.color[2]=0.18f; e.color[3]=1;
                    e.destructible=true; e.health=40; e.maxHealth=40;
                    e.materialType=MaterialType::Wood;
                    e.debrisCount=3; e.debrisScale=0.15f;
                    break;
                }
                case 6: { // Trench section (dug-in sandbag line)
                    float w=RandRange(5.0f,12.0f);
                    e.position[0]=px; e.position[1]=ph+0.35f; e.position[2]=pz;
                    e.scale[0]=w; e.scale[1]=0.7f; e.scale[2]=1.0f;
                    e.rotation[1]=RandRange(0,180);
                    e.color[0]=0.42f; e.color[1]=0.38f; e.color[2]=0.28f; e.color[3]=1;
                    e.destructible=true; e.health=150; e.maxHealth=150;
                    e.materialType=MaterialType::Concrete;
                    break;
                }
                case 7: { // Utility pole
                    e.position[0]=px; e.position[1]=ph+3.5f; e.position[2]=pz;
                    e.scale[0]=0.2f; e.scale[1]=7.0f; e.scale[2]=0.2f;
                    e.color[0]=0.38f; e.color[1]=0.30f; e.color[2]=0.18f; e.color[3]=1;
                    e.destructible=true; e.health=60; e.maxHealth=60;
                    e.materialType=MaterialType::Wood;
                    // Crossbar
                    std::string xname = name + "_X";
                    int xi = scene.AddEntity(xname, MeshType::Cube);
                    auto& xb = scene.GetEntity(xi);
                    xb.position[0]=px; xb.position[1]=ph+6.5f; xb.position[2]=pz;
                    xb.scale[0]=2.5f; xb.scale[1]=0.15f; xb.scale[2]=0.15f;
                    xb.color[0]=0.38f; xb.color[1]=0.30f; xb.color[2]=0.18f; xb.color[3]=1;
                    xb.destructible=true; xb.health=30; xb.maxHealth=30;
                    xb.materialType=MaterialType::Wood;
                    break;
                }
            }
        }
    }

    // ========================================================
    // WATCHTOWERS — tall lookout structures
    // ========================================================
    void AddWatchtowers(Scene& scene) {
        // Place 2-4 watchtowers at strategic high points
        int count = RandInt(2, 4);
        float half = settings.mapSize * 0.5f;

        for (int i = 0; i < count; i++) {
            float best = -1e9f;
            float bx = 0, bz = 0;
            // Find a high point not in a town
            for (int attempt = 0; attempt < 50; attempt++) {
                float tx = RandRange(-half*0.7f, half*0.7f);
                float tz = RandRange(-half*0.7f, half*0.7f);
                float h = heightmap.Sample(tx, tz);
                if (h > best && !IsInTown(tx, tz)) {
                    best = h; bx = tx; bz = tz;
                }
            }

            float by = heightmap.Sample(bx, bz);
            std::string pre = "Tower" + std::to_string(i);

            // 4 legs
            float towerH = RandRange(8.0f, 14.0f);
            float legSpread = 2.0f;
            float legR = 0.2f;
            for (int leg = 0; leg < 4; leg++) {
                float lx = (leg & 1) ? legSpread : -legSpread;
                float lz = (leg & 2) ? legSpread : -legSpread;
                std::string name = pre + "_Leg" + std::to_string(leg);
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                e.position[0] = bx+lx; e.position[1] = by+towerH*0.5f; e.position[2] = bz+lz;
                e.scale[0] = legR*2; e.scale[1] = towerH; e.scale[2] = legR*2;
                e.color[0]=0.38f; e.color[1]=0.30f; e.color[2]=0.20f; e.color[3]=1;
                e.destructible=true; e.health=150; e.maxHealth=150;
                e.materialType=MaterialType::Wood;
            }

            // Platform
            {
                std::string name = pre + "_Platform";
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                e.position[0]=bx; e.position[1]=by+towerH; e.position[2]=bz;
                e.scale[0]=legSpread*2+1.5f; e.scale[1]=0.25f; e.scale[2]=legSpread*2+1.5f;
                e.color[0]=0.40f; e.color[1]=0.32f; e.color[2]=0.20f; e.color[3]=1;
                e.destructible=true; e.health=100; e.maxHealth=100;
                e.materialType=MaterialType::Wood;
            }

            // Railing walls
            float railH = 1.2f;
            float railT = 0.08f;
            float platW = legSpread*2+1.5f;
            for (int s = 0; s < 4; s++) {
                float rpx = bx + ((s==2)?platW*0.5f:(s==3)?-platW*0.5f:0);
                float rpz = bz + ((s==0)?platW*0.5f:(s==1)?-platW*0.5f:0);
                float rsx = (s<2) ? platW : railT;
                float rsz = (s<2) ? railT : platW;

                std::string name = pre + "_Rail" + std::to_string(s);
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                e.position[0]=rpx; e.position[1]=by+towerH+railH*0.5f+0.12f; e.position[2]=rpz;
                e.scale[0]=rsx; e.scale[1]=railH; e.scale[2]=rsz;
                e.color[0]=0.38f; e.color[1]=0.30f; e.color[2]=0.20f; e.color[3]=1;
                e.destructible=true; e.health=40; e.maxHealth=40;
                e.materialType=MaterialType::Wood;
            }

            // Ladder
            {
                std::string name = pre + "_Ladder";
                int idx = scene.AddEntity(name, MeshType::Cube);
                auto& e = scene.GetEntity(idx);
                e.position[0]=bx+legSpread+0.3f; e.position[1]=by+towerH*0.5f; e.position[2]=bz;
                e.scale[0]=0.15f; e.scale[1]=towerH; e.scale[2]=0.6f;
                e.color[0]=0.40f; e.color[1]=0.32f; e.color[2]=0.22f; e.color[3]=1;
                e.destructible=false;
            }
        }
    }

    // ========================================================
    // MAP BOUNDARY
    // ========================================================
    void AddBoundary(Scene& scene) {
        float half = settings.mapSize * 0.5f;
        float h = 25.0f;
        float t = 1.0f;
        struct WDef { const char* n; float px,pz,sx,sz; };
        WDef walls[] = {
            {"Bound_N", 0, half, settings.mapSize, t},
            {"Bound_S", 0, -half, settings.mapSize, t},
            {"Bound_E", half, 0, t, settings.mapSize},
            {"Bound_W", -half, 0, t, settings.mapSize},
        };
        for (auto& w : walls) {
            int idx = scene.AddEntity(w.n, MeshType::Cube);
            auto& e = scene.GetEntity(idx);
            e.position[0]=w.px; e.position[1]=h*0.5f; e.position[2]=w.pz;
            e.scale[0]=w.sx; e.scale[1]=h; e.scale[2]=w.sz;
            e.color[0]=0.3f; e.color[1]=0.32f; e.color[2]=0.28f; e.color[3]=1;
            e.destructible=false; e.visible=false; e.castShadow=false;
        }
    }
};

} // namespace WT
