#pragma once

#include <DirectXMath.h>
#include <string>
#include <vector>
#include <deque>
#include "Core/Entity.h"

namespace WT {

class Renderer;
class Camera;
class WeaponSystem;
class LevelEditorWindow;

using namespace DirectX;

// ---- Editing state shared between editor and game ----
struct EditorState {
    // Scene
    float cubeRotationSpeed = 0.5f;
    float cubeRotation      = 0.0f;
    float cubeScale[3]      = { 1.0f, 1.0f, 1.0f };
    float cubePosition[3]   = { 0.0f, 0.0f, 0.0f };
    float cubeColor[4]      = { 0.4f, 0.7f, 0.3f, 1.0f };
    int   groundExtent      = 5;

    // Lighting
    float sunDirection[3]     = { 0.577f, -0.577f, 0.577f };
    float sunIntensity        = 1.5f;
    float sunColor[3]         = { 1.0f, 0.95f, 0.9f };
    float ambientColor[3]     = { 0.15f, 0.2f, 0.25f };
    float ambientIntensity    = 1.0f;
    float fogColor[3]         = { 0.6f, 0.75f, 0.9f };  // Match sky horizon
    float fogDensity          = 1.0f;
    float fogStart            = 100.0f;
    float fogEnd              = 300.0f;

    // Cel-Shading (stored with lighting, sent via CBLighting)
    bool  celEnabled          = true;
    float celBands            = 3.0f;    // Number of shading bands
    float celRimIntensity     = 0.5f;    // Rim/edge highlight

    // Renderer
    bool  wireframe   = false;
    bool  vsync       = true;
    int   msaaSamples = 4;
    float clearColor[4] = { 0.05f, 0.05f, 0.08f, 1.0f }; // Dark fallback (sky covers it)
    bool  showDebug   = true;

    // Sky
    float skyZenithColor[3]   = { 0.15f, 0.3f, 0.65f };  // Deep blue
    float skyHorizonColor[3]  = { 0.6f, 0.75f, 0.9f };    // Light blue-white
    float skyGroundColor[3]   = { 0.25f, 0.22f, 0.18f };  // Earthy brown
    float skyBrightness       = 1.2f;
    float skyHorizonFalloff   = 0.6f;    // Power curve for horizon->zenith blend
    float sunDiscSize         = 0.9995f; // Cosine angle (smaller = bigger disc)
    float sunGlowIntensity    = 0.35f;
    float sunGlowFalloff      = 64.0f;   // Higher = tighter glow
    // Clouds
    float cloudCoverage       = 0.5f;    // 0 = clear, 1 = overcast
    float cloudSpeed          = 0.03f;   // Animation speed
    float cloudDensity        = 3.0f;    // Edge sharpness
    float cloudHeight         = 0.3f;    // Vertical position in sky
    float cloudColor[3]       = { 1.0f, 1.0f, 1.0f };
    float cloudSunInfluence   = 0.5f;    // Sun-lit edge highlight
    bool  skyDirty            = false;

    // Shadows
    bool  shadowsEnabled      = true;
    float shadowBias          = 0.001f;
    float shadowNormalBias    = 0.02f;
    float shadowIntensity     = 0.85f;
    int   shadowMapResolution = 2048;
    float shadowDistance       = 30.0f;   // Scene radius for shadow ortho
    bool  shadowDirty         = false;

    // Post-Processing
    bool  ppBloomEnabled       = true;
    float ppBloomThreshold     = 0.8f;
    float ppBloomIntensity     = 0.5f;
    bool  ppVignetteEnabled    = true;
    float ppVignetteIntensity  = 0.4f;
    float ppVignetteSmoothness = 0.8f;
    float ppBrightness         = 0.0f;
    float ppContrast           = 1.0f;
    float ppSaturation         = 1.0f;
    float ppGamma              = 1.0f;
    float ppTint[3]            = { 1.0f, 1.0f, 1.0f };
    bool  ppDirty              = false;

    // Art Style: Outlines
    bool  outlineEnabled       = false;
    float outlineThickness     = 1.0f;
    float outlineColor[3]      = { 0.05f, 0.03f, 0.02f };

    // Art Style: Paper grain & hatching
    float paperGrainIntensity  = 0.0f;
    float hatchingIntensity    = 0.0f;
    float hatchingScale        = 4.0f;

    // Entity system
    Scene scene;
    int   selectedEntity       = -1;
    bool  entityDirty          = false;  // Request to rebuild scene rendering

    // SSAO
    bool  ssaoEnabled          = false;
    float ssaoRadius           = 0.3f;
    float ssaoBias             = 0.025f;
    float ssaoIntensity        = 0.5f;
    int   ssaoKernelSize       = 16;
    bool  ssaoDirty            = false;

    // Camera
    float cameraMoveSpeed   = 5.0f;
    float cameraSprintMult  = 2.5f;
    float cameraSensitivity = 0.15f;
    float cameraFOV         = 79.0f;
    float cameraNearZ       = 0.1f;
    float cameraFarZ        = 500.0f;

    // Character System
    bool  characterMode       = true;     // FPS ground mode vs fly cam
    bool  charShowBody        = false;    // Show body (off for FPS)
    float charMoveSpeed       = 5.0f;
    float charSprintMult      = 2.0f;
    float charJumpForce       = 6.0f;
    float charGravity         = 18.0f;
    float charGroundY         = 0.0f;
    float charEyeHeight       = 1.4f;
    float charCrouchEyeHeight = 0.9f;
    float charCrouchSpeedMult = 0.5f;
    float charCrouchTransSpeed = 8.0f;
    bool  charCameraTiltEnabled = true;
    float charCameraTiltAmount  = 0.4f;
    float charCameraTiltSpeed   = 6.0f;
    bool  charHeadBobEnabled  = true;
    float charHeadBobSpeed    = 10.0f;
    float charHeadBobAmount   = 0.04f;
    float charHeadBobSway     = 0.02f;
    float charHeadColor[4]    = { 0.85f, 0.70f, 0.55f, 1.0f };
    float charTorsoColor[4]   = { 0.25f, 0.35f, 0.20f, 1.0f };
    float charArmsColor[4]    = { 0.25f, 0.35f, 0.20f, 1.0f };
    float charLegsColor[4]    = { 0.30f, 0.25f, 0.18f, 1.0f };

    // AI Navigation
    bool  navGridEnabled      = true;
    int   navGridWidth        = 40;
    int   navGridHeight       = 40;
    float navCellSize         = 1.0f;
    float navOriginX          = -20.0f;
    float navOriginZ          = -20.0f;
    float navGridY            = 0.0f;
    bool  navShowDebug        = false;
    bool  navRebuildRequested = false;   // Request to rebuild from entities

    // AI Agents
    bool  aiShowDebug         = false;
    int   aiSelectedAgent     = -1;
    float aiDefaultSpeed      = 3.0f;
    float aiDefaultChaseSpeed = 5.0f;
    float aiDefaultDetectRange = 10.0f;
    float aiDefaultLoseRange   = 15.0f;
    float aiDefaultColor[4]   = { 0.7f, 0.2f, 0.2f, 1.0f };
    float aiSpawnPos[3]       = { 5.0f, 0.0f, 5.0f };

    // Physics / Collision
    bool  physicsCollisionEnabled = true;
    bool  physicsShowDebug        = false;
    bool  physicsRebuildRequested = false;

    // FPS Arms
    // Character model
    float charModelScale         = 0.7f;     // Character model scale

    // Weapon System
    int   weaponType             = 0;        // WeaponType enum
    bool  weaponShowDebug        = false;
    bool  weaponShowHUD          = true;
    WeaponSystem* pWeaponSystem  = nullptr;  // Pointer for editor model tuning

    // Level Editor (separate window)
    LevelEditorWindow* pLevelEditor = nullptr;

    // Flags
    bool lightingDirty  = false;
    bool cameraDirty    = false;
    bool rendererDirty  = false;
};

// ---- Console log ----
struct LogEntry {
    enum Level { Info, Warn, Error };
    Level level;
    std::string text;
};

class EditorPanels {
public:
    void Init();
    void Draw(EditorState& state, Renderer& renderer, Camera& camera, float dt,
              int fps, float totalTime);
    void AddLog(LogEntry::Level level, const char* fmt, ...);

    bool showDemoWindow = false;

private:
    // Unified panel drawing
    void DrawMenuBar(EditorState& state);
    void DrawDockspace();
    void DrawOutliner(EditorState& state, Renderer& renderer, Camera& camera,
                      float dt, int fps, float totalTime);
    void DrawConsoleDrawer();

    // Outliner sections
    void SectionScene(EditorState& state);
    void SectionLevel(EditorState& state);
    void SectionEntities(EditorState& state);
    void SectionLighting(EditorState& state);
    void SectionSky(EditorState& state);
    void SectionShadows(EditorState& state);
    void SectionPostProcess(EditorState& state);
    void SectionArtStyle(EditorState& state);
    void SectionSSAO(EditorState& state);
    void SectionCharacter(EditorState& state);
    void SectionPhysics(EditorState& state);
    void SectionNavGrid(EditorState& state);
    void SectionAI(EditorState& state);
    void SectionWeapon(EditorState& state);
    void SectionCamera(EditorState& state, Camera& camera);
    void SectionRenderer(EditorState& state, Renderer& renderer);
    void SectionPerformance(Renderer& renderer, int fps, float dt);

    // Visual helpers
    bool BeginSection(const char* icon, const char* label, bool defaultOpen = true);
    void EndSection();
    void PropertyLabel(const char* label);
    void SectionSeparator();

    // Console
    std::deque<LogEntry> m_logEntries;
    static constexpr size_t MAX_LOG_ENTRIES = 512;
    bool m_autoScroll     = true;
    char m_consoleInput[256] = {};
    bool m_consoleOpen    = true;
    float m_consoleHeight = 60.0f;

    // Perf history
    float m_fpsHistory[120]       = {};
    float m_frameTimeHistory[120] = {};
    int   m_historyIdx = 0;

    // Selection
    int m_selectedObject = -1;

    // Outliner state
    bool m_firstFrame = true;
};

} // namespace WT
