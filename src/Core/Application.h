#pragma once

#include <Windows.h>
#include <string>
#include "Graphics/Renderer.h"
#include "Graphics/Camera.h"
#include "Graphics/Shader.h"
#include "Graphics/Mesh.h"
#include "Graphics/ConstantBuffer.h"
#include "Graphics/Texture.h"
#include "Graphics/DebugRenderer.h"
#include "Graphics/ShadowMap.h"
#include "Graphics/PostProcess.h"
#include "Graphics/SSAO.h"
#include "Core/Timer.h"
#include "Core/Input.h"
#include "Core/Entity.h"
#include "Core/Character.h"
#include "Core/ResourceManager.h"
#include "AI/NavGrid.h"
#include "AI/AIAgent.h"
#include "Physics/PhysicsWorld.h"
#include "Gameplay/WeaponSystem.h"
#include "Gameplay/HUD.h"
#include "FX/ParticleSystem.h"
#include "Util/MathHelpers.h"
#include "Editor/EditorUI.h"
#include "Editor/EditorPanels.h"
#include "Editor/LevelEditorWindow.h"
#include "Core/SceneCulling.h"
#include "Graphics/FSRUpscaler.h"

namespace WT {

class Application {
public:
    static Application& Get() { static Application app; return app; }

    bool Init(HINSTANCE hInstance, int width = 1920, int height = 1080);
    int  Run();
    void Shutdown();

    HWND GetHWND() const { return m_hwnd; }
    Input& GetInput() { return m_input; }
    Renderer& GetRenderer() { return m_renderer; }

    // Win32 message proc (forwarded from static WndProc)
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    Application() = default;
    ~Application() = default;

    void FixedUpdate(float dt);
    void Update(float dt);
    void Render();

    bool CreateAppWindow(HINSTANCE hInstance, int width, int height);
    bool InitGraphics();
    bool CreateCubeMesh();
    bool CreateGroundMesh();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    std::wstring GetExeDir() const;

    HWND        m_hwnd      = nullptr;
    HINSTANCE   m_hInstance = nullptr;
    bool        m_running       = false;
    bool        m_minimized     = false;
    bool        m_resizing      = false;
    bool        m_rendererReady = false;
    int         m_width     = 1920;
    int         m_height    = 1080;
    std::wstring m_title    = L"War Times V0.01";
    std::wstring m_exeDir;

    // Core systems
    Renderer    m_renderer;
    Camera      m_camera;
    Timer       m_timer;
    Input       m_input;

    // Graphics resources
    Shader      m_voxelShader;
    Shader      m_skyShader;
    Shader      m_groundShader;
    Mesh        m_cubeMesh;
    Mesh        m_groundMesh;
    ConstantBuffer<CBPerFrame>  m_cbPerFrame;
    ConstantBuffer<CBPerObject> m_cbPerObject;
    ConstantBuffer<CBLighting>  m_cbLighting;
    ConstantBuffer<CBSky>       m_cbSky;
    ConstantBuffer<CBShadow>    m_cbShadow;

    // Shadow mapping
    ShadowMap   m_shadowMap;
    Shader      m_shadowShader;

    // Post-processing
    PostProcess m_postProcess;
    PostProcessSettings m_postProcessSettings;

    // SSAO
    SSAO m_ssao;
    SSAOSettings m_ssaoSettings;

    // Debug rendering
    DebugRenderer m_debugRenderer;

    // Default textures
    Texture     m_defaultWhite;


    // Editor systems
    EditorUI     m_editorUI;
    EditorPanels m_editorPanels;
    EditorState  m_editorState;
    LevelEditorWindow m_levelEditor;
    bool m_editorVisible = false;

    // Test scene
    float m_cubeRotation = 0.0f;
    bool  m_showDebug    = true;

    // Character system
    Character         m_character;
    CharacterSettings m_charSettings;
    bool              m_characterMode = true;   // true = FPS character, false = fly cam

    // AI & Navigation
    NavGrid    m_navGrid;
    AISystem   m_aiSystem;

    // Physics / Collision
    PhysicsWorld m_physicsWorld;

    // Weapon system
    WeaponSystem m_weaponSystem;
    HUD          m_hud;

    // Particle / FX
    ParticleSystem m_particles;

    // Hot-reload timer
    float m_hotReloadTimer = 0.0f;

    // FSR upscaling
    FSRUpscaler m_fsrUpscaler;

    // Culling & streaming
    SceneCuller m_sceneCuller;
};

} // namespace WT
