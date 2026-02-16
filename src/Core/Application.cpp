#include "Application.h"
#include "Util/Log.h"
#include "Editor/LevelFile.h"
#include "PCG/LevelGenerator.h"
#include <cstdio>
#include <algorithm>

namespace WT {

// ==================== Win32 Window ====================

LRESULT CALLBACK Application::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return Application::Get().HandleMessage(hwnd, msg, wParam, lParam);
}

LRESULT Application::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Forward to ImGui first — if it consumes the event, don't pass to game input
    if (m_editorVisible && m_editorUI.HandleMessage(hwnd, msg, wParam, lParam))
        return 0;

    m_input.ProcessMessage(msg, wParam, lParam);

    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            m_running = false;
            return 0;

        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            m_width = w;
            m_height = h;

            if (wParam == SIZE_MINIMIZED) {
                m_minimized = true;
            } else if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED) {
                m_minimized = false;
                if (!m_resizing && m_rendererReady && w > 0 && h > 0) {
                    m_renderer.OnResize(w, h);
                    m_postProcess.OnResize(m_renderer.GetDevice(), w, h);
                    m_ssao.OnResize(m_renderer.GetDevice(), w, h);
                    m_camera.UpdateProjection(m_renderer.GetAspectRatio());
                }
            }
            return 0;
        }

        case WM_ENTERSIZEMOVE:
            m_resizing = true;
            return 0;

        case WM_EXITSIZEMOVE:
            m_resizing = false;
            if (m_rendererReady && m_width > 0 && m_height > 0) {
                m_renderer.OnResize(m_width, m_height);
                m_postProcess.OnResize(m_renderer.GetDevice(), m_width, m_height);
                m_ssao.OnResize(m_renderer.GetDevice(), m_width, m_height);
                m_camera.UpdateProjection(m_renderer.GetAspectRatio());
            }
            return 0;

        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 320;
            info->ptMinTrackSize.y = 240;
            return 0;
        }

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                // Window lost focus — release cursor
                if (m_input.IsCursorLocked()) {
                    m_input.SetCursorLocked(false);
                }
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;  // Prevent GDI background erase — DirectX handles rendering

        case WM_PAINT:
            if (!m_rendererReady) {
                // Before renderer is ready, just validate the window region
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
                return 0;
            }
            break;  // Fall through to DefWindowProc when renderer is active
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool Application::CreateAppWindow(HINSTANCE hInstance, int width, int height) {
    m_hInstance = hInstance;

    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // Don't paint GDI background — DirectX handles rendering
    wc.lpszClassName = L"WarTimesClass";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm       = LoadIcon(nullptr, IDI_APPLICATION);

    if (!RegisterClassEx(&wc)) {
        LOG_ERROR("Failed to register window class");
        return false;
    }

    // Adjust window rect so client area is exactly width x height
    RECT rect = { 0, 0, width, height };
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);

    m_hwnd = CreateWindowEx(
        0,
        L"WarTimesClass",
        m_title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!m_hwnd) {
        LOG_ERROR("Failed to create window");
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(m_hwnd);

    // Read the actual client size after maximize
    RECT clientRect;
    GetClientRect(m_hwnd, &clientRect);
    m_width  = clientRect.right - clientRect.left;
    m_height = clientRect.bottom - clientRect.top;

    LOG_INFO("Window created (maximized): %dx%d", m_width, m_height);
    return true;
}

// ==================== Init ====================

bool Application::Init(HINSTANCE hInstance, int width, int height) {
    if (!CreateAppWindow(hInstance, width, height)) return false;
    // Use actual client dimensions (may differ from requested if window was maximized)
    if (!m_renderer.Init(m_hwnd, m_width, m_height))    return false;
    m_rendererReady = true;

    m_input.Init(m_hwnd);
    m_camera.Init(70.0f, m_renderer.GetAspectRatio(), 0.1f, 500.0f);
    m_camera.SetPosition(0.0f, 1.4f, -5.0f);  // Eye height on ground

    // Init character controller (starts at origin, ground level)
    m_character.Init({ 0.0f, 0.0f, -5.0f }, 0.0f);

    if (!InitGraphics()) return false;

    // Initialize editor systems
    if (!m_editorUI.Init(m_hwnd, m_renderer.GetDevice(), m_renderer.GetContext())) {
        LOG_ERROR("Editor UI init failed");
        return false;
    }
    m_editorPanels.Init();

    // Initialize level editor (separate window, hidden by default)
    if (!m_levelEditor.Init(m_renderer.GetDevice(), m_hInstance, 1200, 800)) {
        LOG_WARN("Level Editor window init failed — continuing without it");
    }

    // Share rendering resources with level editor so it can draw entities
    {
        LevelEditorResources res;
        res.voxelShader  = &m_voxelShader;
        res.groundShader = &m_groundShader;
        res.cubeMesh     = &m_cubeMesh;
        res.groundMesh   = &m_groundMesh;
        res.cbPerFrame   = &m_cbPerFrame;
        res.cbPerObject  = &m_cbPerObject;
        res.cbLighting   = &m_cbLighting;
        m_levelEditor.SetResources(res);
    }

    m_timer.Reset();
    m_running = true;

    // Start in FPS mode with cursor locked (F6 to open editor)
    m_editorVisible = false;
    m_input.SetCursorLocked(true);

    // Auto-load default level or generate a random one
    if (m_editorState.pcgOnLaunch) {
        LevelGenerator gen;
        gen.Generate(m_editorState.scene);
        m_editorState.entityDirty = true;
        LOG_INFO("PCG: Generated random level on launch (seed %u, %d entities)",
                 gen.GetUsedSeed(), m_editorState.scene.GetEntityCount());
    } else {
        // Start with a blank level (no entities)
        m_editorState.scene.Clear();
        m_editorState.entityDirty = true;
        LOG_INFO("Started with blank level");
    }

    LOG_INFO("Application initialized successfully");
    return true;
}

std::wstring Application::GetExeDir() const {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        dir = dir.substr(0, pos + 1);
    return dir;
}

bool Application::InitGraphics() {
    auto* device = m_renderer.GetDevice();

    // Resolve shader paths relative to executable directory
    m_exeDir = GetExeDir();
    std::wstring shaderDir = m_exeDir + L"shaders/";

    LOG_INFO("Loading shaders from exe directory");

    // Load voxel shaders
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    if (!m_voxelShader.LoadVS(device, shaderDir + L"VoxelVS.hlsl", "VSMain", layout, _countof(layout)))
        return false;
    if (!m_voxelShader.LoadPS(device, shaderDir + L"VoxelPS.hlsl", "PSMain"))
        return false;

    // Sky shader — fullscreen triangle, no input layout needed (uses SV_VertexID)
    // Load with a dummy layout since our Shader class requires one for VS
    if (!m_skyShader.LoadVS(device, shaderDir + L"SkyVS.hlsl", "VSMain", layout, _countof(layout)))
        return false;
    if (!m_skyShader.LoadPS(device, shaderDir + L"SkyPS.hlsl", "PSMain"))
        return false;

    // Ground plane shader — procedural checkerboard
    if (!m_groundShader.LoadVS(device, shaderDir + L"GroundVS.hlsl", "VSMain", layout, _countof(layout)))
        return false;
    if (!m_groundShader.LoadPS(device, shaderDir + L"GroundPS.hlsl", "PSMain"))
        return false;

    // Shadow depth shader — depth-only pass from light’s perspective
    if (!m_shadowShader.LoadVS(device, shaderDir + L"ShadowVS.hlsl", "VSMain", layout, _countof(layout)))
        return false;
    // No pixel shader — depth-only rendering

    // Create constant buffers (PerFrame b0, PerObject b1, Lighting b2, Sky b3, Shadow b4)
    if (!m_cbPerFrame.Init(device))  return false;
    if (!m_cbPerObject.Init(device)) return false;
    if (!m_cbLighting.Init(device))  return false;
    if (!m_cbSky.Init(device))       return false;
    if (!m_cbShadow.Init(device))    return false;

    // Shadow map
    if (!m_shadowMap.Init(device, 2048)) return false;

    // Post-processing pipeline
    if (!m_postProcess.Init(device, m_width, m_height, shaderDir)) return false;

    // SSAO
    if (!m_ssao.Init(device, m_width, m_height, shaderDir)) return false;

    // Init debug renderer
    if (!m_debugRenderer.Init(device, shaderDir)) return false;

    // Create default textures
    if (!m_defaultWhite.CreateFromColor(device, 1.0f, 1.0f, 1.0f, 1.0f)) return false;

    // Create test cube
    if (!CreateCubeMesh()) return false;

    // Create ground plane (single large quad)
    if (!CreateGroundMesh()) return false;

    // AI Navigation Grid
    m_navGrid.Init(m_editorState.navGridWidth, m_editorState.navGridHeight,
                   m_editorState.navCellSize,
                   m_editorState.navOriginX, m_editorState.navOriginZ,
                   m_editorState.navGridY);

    // AI System
    m_aiSystem.Init();

    // Physics / Collision World
    m_physicsWorld.Init();

    // Weapon System
    m_weaponSystem.Init();
    m_hud.Init();
    m_editorState.pWeaponSystem = &m_weaponSystem;
    m_editorState.pLevelEditor  = &m_levelEditor;

    // Particle / FX System
    m_particles.Init(m_editorState.charGroundY);

    // Resource Manager
    ResourceManager::Get().Init(device, shaderDir);

    // Load models (.mesh binary files)
    // Use source directory (relative to exe: ../../models/) so hot-reload picks up
    // new exports from Blender without needing a rebuild
    std::wstring modelsDir = m_exeDir + L"../../models/";
    int meshCount = ResourceManager::Get().LoadMeshDirectory(modelsDir);
    LOG_INFO("Loaded %d models", meshCount);

    // Load textures
    int texCount = 0;

    // Create dev prototype grid textures FIRST (overrides any broken PNGs)
    texCount += ResourceManager::Get().CreateDevTextures();

    std::wstring texturesDir = m_exeDir + L"textures/";
    texCount += ResourceManager::Get().LoadTextureDirectory(texturesDir);
    LOG_INFO("Loaded %d textures", texCount);

    // Create default white texture for untextured meshes
    ResourceManager::Get().CreateColorTexture("_white", 1.0f, 1.0f, 1.0f, 1.0f);

    LOG_INFO("Graphics resources initialized");
    return true;
}

bool Application::CreateCubeMesh() {
    // Unit cube centered at origin with per-face normals and colors
    // Each face has 4 unique vertices (for correct normals)
    using V = VertexPosNormalColor;

    // Face colors (BattleBit low-poly style — earth tones)
    XMFLOAT4 green  = { 0.4f, 0.7f, 0.3f, 1.0f };
    XMFLOAT4 brown  = { 0.5f, 0.35f, 0.2f, 1.0f };
    XMFLOAT4 gray   = { 0.6f, 0.6f, 0.6f, 1.0f };

    std::vector<V> vertices = {
        // Front face (+Z) — green (grass-like top)
        {{ -0.5f, -0.5f,  0.5f }, { 0, 0, 1 }, green, {0,0} },
        {{  0.5f, -0.5f,  0.5f }, { 0, 0, 1 }, green, {1,0} },
        {{  0.5f,  0.5f,  0.5f }, { 0, 0, 1 }, green, {1,1} },
        {{ -0.5f,  0.5f,  0.5f }, { 0, 0, 1 }, green, {0,1} },

        // Back face (-Z)
        {{  0.5f, -0.5f, -0.5f }, { 0, 0, -1 }, brown, {0,0} },
        {{ -0.5f, -0.5f, -0.5f }, { 0, 0, -1 }, brown, {1,0} },
        {{ -0.5f,  0.5f, -0.5f }, { 0, 0, -1 }, brown, {1,1} },
        {{  0.5f,  0.5f, -0.5f }, { 0, 0, -1 }, brown, {0,1} },

        // Top face (+Y) — green
        {{ -0.5f,  0.5f,  0.5f }, { 0, 1, 0 }, green, {0,0} },
        {{  0.5f,  0.5f,  0.5f }, { 0, 1, 0 }, green, {1,0} },
        {{  0.5f,  0.5f, -0.5f }, { 0, 1, 0 }, green, {1,1} },
        {{ -0.5f,  0.5f, -0.5f }, { 0, 1, 0 }, green, {0,1} },

        // Bottom face (-Y)
        {{ -0.5f, -0.5f, -0.5f }, { 0, -1, 0 }, brown, {0,0} },
        {{  0.5f, -0.5f, -0.5f }, { 0, -1, 0 }, brown, {1,0} },
        {{  0.5f, -0.5f,  0.5f }, { 0, -1, 0 }, brown, {1,1} },
        {{ -0.5f, -0.5f,  0.5f }, { 0, -1, 0 }, brown, {0,1} },

        // Right face (+X)
        {{  0.5f, -0.5f,  0.5f }, { 1, 0, 0 }, gray, {0,0} },
        {{  0.5f, -0.5f, -0.5f }, { 1, 0, 0 }, gray, {1,0} },
        {{  0.5f,  0.5f, -0.5f }, { 1, 0, 0 }, gray, {1,1} },
        {{  0.5f,  0.5f,  0.5f }, { 1, 0, 0 }, gray, {0,1} },

        // Left face (-X)
        {{ -0.5f, -0.5f, -0.5f }, { -1, 0, 0 }, gray, {0,0} },
        {{ -0.5f, -0.5f,  0.5f }, { -1, 0, 0 }, gray, {1,0} },
        {{ -0.5f,  0.5f,  0.5f }, { -1, 0, 0 }, gray, {1,1} },
        {{ -0.5f,  0.5f, -0.5f }, { -1, 0, 0 }, gray, {0,1} },
    };

    std::vector<UINT> indices = {
        0,1,2,  0,2,3,       // Front
        4,5,6,  4,6,7,       // Back
        8,9,10, 8,10,11,     // Top
        12,13,14, 12,14,15,  // Bottom
        16,17,18, 16,18,19,  // Right
        20,21,22, 20,22,23,  // Left
    };

    return m_cubeMesh.Create(m_renderer.GetDevice(), vertices, indices);
}

bool Application::CreateGroundMesh() {
    // Large flat quad for the ground plane
    using V = VertexPosNormalColor;
    float s = 200.0f;  // Half-extent
    XMFLOAT4 groundCol = { 0.35f, 0.55f, 0.28f, 1.0f };  // Natural green

    std::vector<V> vertices = {
        {{ -s, 0.0f,  s }, { 0, 1, 0 }, groundCol, {0,0} },
        {{  s, 0.0f,  s }, { 0, 1, 0 }, groundCol, {1,0} },
        {{  s, 0.0f, -s }, { 0, 1, 0 }, groundCol, {1,1} },
        {{ -s, 0.0f, -s }, { 0, 1, 0 }, groundCol, {0,1} },
    };

    std::vector<UINT> indices = { 0, 1, 2, 0, 2, 3 };

    return m_groundMesh.Create(m_renderer.GetDevice(), vertices, indices);
}

// ==================== Main Loop ====================

int Application::Run() {
    MSG msg = {};

    // Frame rate limiter — 60 FPS cap
    LARGE_INTEGER qpcFreq, frameStart, frameEnd;
    QueryPerformanceFrequency(&qpcFreq);
    const double targetFrameTime = 1.0 / 60.0; // 16.667ms

    while (m_running) {
        QueryPerformanceCounter(&frameStart);

        // Process all pending Windows messages
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                m_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!m_running) break;
        if (m_minimized) {
            Sleep(16);
            continue;
        }

        // Tick timer
        m_timer.Tick();

        // Fixed timestep simulation
        while (m_timer.ShouldDoFixedUpdate()) {
            FixedUpdate(m_timer.FixedDeltaTime());
        }

        // Per-frame update
        Update(m_timer.DeltaTime());

        // Render
        Render();

        // Update input state (after everything else — so "pressed this frame" works next frame)
        m_input.Update();

        // Title bar — just the engine name (stats shown in editor menu bar)
        SetWindowText(m_hwnd, m_title.c_str());

        // --- Frame rate limiter: spin-wait to hit 60 FPS ---
        do {
            QueryPerformanceCounter(&frameEnd);
        } while (static_cast<double>(frameEnd.QuadPart - frameStart.QuadPart)
               / static_cast<double>(qpcFreq.QuadPart) < targetFrameTime);
    }

    return static_cast<int>(msg.wParam);
}

// ==================== Update ====================

void Application::FixedUpdate(float dt) {
    (void)dt;
}

void Application::Update(float dt) {
    (void)dt;

    // --- Despawn timer: remove debris entities after their timer expires ---
    {
        bool needsColliderRebuild = false;
        for (int i = m_editorState.scene.GetEntityCount() - 1; i >= 0; i--) {
            auto& e = m_editorState.scene.GetEntity(i);
            if (e.despawnTimer < 0.0f) continue; // no despawn
            e.despawnTimer -= dt;

            // Fade out during last 2 seconds
            if (e.despawnTimer < 2.0f && e.despawnTimer >= 0.0f) {
                e.color[3] = e.despawnTimer / 2.0f;
            }

            if (e.despawnTimer <= 0.0f) {
                m_editorState.scene.RemoveEntity(i);
                if (m_editorState.selectedEntity == i)
                    m_editorState.selectedEntity = -1;
                else if (m_editorState.selectedEntity > i)
                    m_editorState.selectedEntity--;
                needsColliderRebuild = true;
            }
        }
        if (needsColliderRebuild) {
            m_physicsWorld.RebuildStaticColliders(m_editorState.scene);
        }
    }

    // Suppress game input when editor UI wants it
    bool editorWantsKeyboard = m_editorVisible && m_editorUI.WantsKeyboard();
    bool editorWantsMouse    = m_editorVisible && m_editorUI.WantsMouse();

    // Toggle editor visibility with F6
    if (m_input.IsKeyPressed(VK_F6)) {
        m_editorVisible = !m_editorVisible;
        m_editorUI.SetVisible(m_editorVisible);
        // Free cursor when editor opens, lock when it hides
        m_input.SetCursorLocked(!m_editorVisible);
        LOG_INFO("Editor: %s", m_editorVisible ? "ON" : "OFF");
    }

    // F7: Toggle level editor window
    if (m_input.IsKeyPressed(VK_F7)) {
        m_levelEditor.SetOpen(!m_levelEditor.IsOpen());
        LOG_INFO("Level Editor: %s", m_levelEditor.IsOpen() ? "OPEN" : "CLOSED");
    }

    // Escape: unlock cursor / toggle cursor lock when editor hidden
    if (m_input.IsKeyPressed(VK_ESCAPE)) {
        if (m_editorVisible) {
            // Always unlock when editor is showing
            m_input.SetCursorLocked(false);
        } else {
            m_input.ToggleCursorLock();
        }
    }

    // F1: Toggle wireframe
    if (!editorWantsKeyboard && m_input.IsKeyPressed(VK_F1)) {
        m_editorState.wireframe = !m_editorState.wireframe;
        m_renderer.SetWireframe(m_editorState.wireframe);
        LOG_INFO("Wireframe: %s", m_editorState.wireframe ? "ON" : "OFF");
    }

    // F2: Toggle VSync
    if (!editorWantsKeyboard && m_input.IsKeyPressed(VK_F2)) {
        m_editorState.vsync = !m_editorState.vsync;
        m_renderer.SetVSync(m_editorState.vsync);
        LOG_INFO("VSync: %s", m_editorState.vsync ? "ON" : "OFF");
    }

    // F3: Cycle MSAA (1 -> 2 -> 4 -> 8 -> 1)
    if (!editorWantsKeyboard && m_input.IsKeyPressed(VK_F3)) {
        UINT msaa = m_renderer.GetMSAASamples();
        msaa = (msaa >= 8) ? 1 : msaa * 2;
        m_editorState.msaaSamples = msaa;
        m_renderer.SetMSAA(msaa);
    }

    // F4: Toggle debug rendering
    if (!editorWantsKeyboard && m_input.IsKeyPressed(VK_F4)) {
        m_editorState.showDebug = !m_editorState.showDebug;
        m_debugRenderer.SetEnabled(m_editorState.showDebug);
        LOG_INFO("Debug rendering: %s", m_editorState.showDebug ? "ON" : "OFF");
    }

    // F8: Toggle character mode (FPS ground walk vs fly cam)
    if (!editorWantsKeyboard && m_input.IsKeyPressed(VK_F8)) {
        m_characterMode = !m_characterMode;
        m_editorState.characterMode = m_characterMode;
        if (m_characterMode) {
            // Entering character mode: snap character to camera XZ, ground Y
            XMFLOAT3 camPos = m_camera.GetPosition();
            m_character.SetPosition({ camPos.x, m_charSettings.groundY, camPos.z });
        }
        LOG_INFO("Character mode: %s", m_characterMode ? "ON (FPS)" : "OFF (Fly Cam)");
    }
    // Sync from editor state
    m_characterMode = m_editorState.characterMode;

    // M: Toggle cursor lock (hide/show mouse)
    if (!editorWantsKeyboard && m_input.IsKeyPressed('M')) {
        m_input.ToggleCursorLock();
        LOG_INFO("Cursor: %s", m_input.IsCursorLocked() ? "LOCKED (hidden)" : "FREE (visible)");
    }

    // F5: Force shader hot-reload
    if (!editorWantsKeyboard && m_input.IsKeyPressed(VK_F5)) {
        m_voxelShader.Reload(m_renderer.GetDevice());
    }

    // F9: Force model hot-reload (rescan models directory)
    if (!editorWantsKeyboard && m_input.IsKeyPressed(VK_F9)) {
        ResourceManager::Get().ReloadMeshDirectory();
    }

    // Auto hot-reload check every 2 seconds
    m_hotReloadTimer += dt;
    if (m_hotReloadTimer >= 2.0f) {
        m_hotReloadTimer = 0.0f;
        if (m_voxelShader.HasFileChanged()) {
            m_voxelShader.Reload(m_renderer.GetDevice());
        }
        ResourceManager::Get().ReloadMeshDirectory();
    }

    // Camera rotation from mouse:
    // - When cursor is locked (editor hidden): always rotate
    // - When cursor is free (editor visible): right-click-drag to rotate
    // - In character mode with editor visible: left-click-drag to rotate
    bool doRotate = false;
    if (m_input.IsCursorLocked() && !editorWantsMouse) {
        doRotate = true;
    } else if (!m_input.IsCursorLocked() && !editorWantsMouse) {
        if (m_input.IsRightMouseDown()) {
            doRotate = true;
        } else if (m_characterMode && m_input.IsLeftMouseDown()) {
            doRotate = true;
        }
    }
    if (doRotate) {
        XMFLOAT2 delta = m_input.GetMouseDelta();
        m_camera.Update(delta.x, delta.y);
    }

    // ---- Physics World (must run BEFORE character update) ----
    m_physicsWorld.showDebug = m_editorState.physicsShowDebug;

    // Rebuild static colliders every frame (handles spawns, moves, scale changes)
    m_physicsWorld.RebuildStaticColliders(m_editorState.scene);


    // Sync physics settings
    {
        PhysicsSettings& physSettings = m_physicsWorld.GetSettings();
        physSettings.gravity       = m_editorState.charGravity;
        physSettings.groundY       = m_editorState.charGroundY;
        physSettings.groundEnabled = true;
    }

    if (m_characterMode) {
        // ---- FPS Character Controller ----
        // Sync editor settings to character settings
        m_charSettings.moveSpeed      = m_editorState.charMoveSpeed;
        m_charSettings.sprintMult     = m_editorState.charSprintMult;
        m_charSettings.jumpForce      = m_editorState.charJumpForce;
        m_charSettings.gravity        = m_editorState.charGravity;
        m_charSettings.groundY        = m_editorState.charGroundY;
        m_charSettings.eyeHeight      = m_editorState.charEyeHeight;
        m_charSettings.crouchEyeHeight  = m_editorState.charCrouchEyeHeight;
        m_charSettings.crouchSpeedMult  = m_editorState.charCrouchSpeedMult;
        m_charSettings.crouchTransSpeed = m_editorState.charCrouchTransSpeed;
        m_charSettings.cameraTiltEnabled = m_editorState.charCameraTiltEnabled;
        m_charSettings.cameraTiltAmount  = m_editorState.charCameraTiltAmount;
        m_charSettings.cameraTiltSpeed   = m_editorState.charCameraTiltSpeed;
        m_charSettings.headBobEnabled = m_editorState.charHeadBobEnabled;
        m_charSettings.headBobSpeed   = m_editorState.charHeadBobSpeed;
        m_charSettings.headBobAmount  = m_editorState.charHeadBobAmount;
        m_charSettings.headBobSway    = m_editorState.charHeadBobSway;
        memcpy(m_charSettings.headColor,  m_editorState.charHeadColor,  sizeof(float) * 4);
        memcpy(m_charSettings.torsoColor, m_editorState.charTorsoColor, sizeof(float) * 4);
        memcpy(m_charSettings.armsColor,  m_editorState.charArmsColor,  sizeof(float) * 4);
        memcpy(m_charSettings.legsColor,  m_editorState.charLegsColor,  sizeof(float) * 4);
        m_charSettings.collisionEnabled = m_editorState.physicsCollisionEnabled;

        m_character.Update(dt, m_input, m_camera, m_charSettings,
                           editorWantsMouse, editorWantsKeyboard,
                           m_editorState.physicsCollisionEnabled ? &m_physicsWorld : nullptr);

        // ---- Weapon System Update (only in character mode) ----
        m_weaponSystem.Update(dt, m_input, m_camera, m_character, editorWantsMouse,
                               m_editorState.physicsCollisionEnabled ? &m_physicsWorld : nullptr,
                               &m_aiSystem);

        // Apply weapon recoil to camera
        float recoilPitch = m_weaponSystem.GetRecoilPitch();
        if (recoilPitch > 0.01f) {
            // Nudge camera pitch up by recoil amount (scaled down for smooth feel)
            m_camera.Update(m_weaponSystem.GetRecoilYaw() * dt * 2.0f,
                            -recoilPitch * dt * 2.0f);
        }

        // ---- Destruction: process bullet hits against entities ----
        if (m_weaponSystem.JustFired()) {
            const auto& hit = m_weaponSystem.GetLastHit();

            if (hit.hit) {
                // If we hit an entity, use material-aware impact FX
                if (hit.entityIndex >= 0 && hit.entityIndex < m_editorState.scene.GetEntityCount()) {
                    auto& entity = m_editorState.scene.GetEntity(hit.entityIndex);

                    // Material-aware impact FX (sparks + dust tuned per material)
                    m_particles.SpawnMaterialImpact(hit.hitPosition, hit.hitNormal,
                                                    entity.color, entity.materialType);

                    // Add hit decal (bullet scar) at world-space hit position
                    entity.AddHitDecal(hit.hitPosition.x, hit.hitPosition.y, hit.hitPosition.z);

                    if (entity.destructible) {
                        // Auto-enable voxel destruction on cubes that don't have it yet
                        if (!entity.voxelDestruction && entity.meshType == MeshType::Cube) {
                            entity.voxelDestruction = true;
                            // Pick resolution: thin/fragile materials get smaller grids
                            float minScale = (std::min)({entity.scale[0], entity.scale[1], entity.scale[2]});
                            float avgScale = (entity.scale[0] + entity.scale[1] + entity.scale[2]) / 3.0f;

                            if (entity.materialType == MaterialType::Glass ||
                                entity.materialType == MaterialType::Wood ||
                                minScale < 0.5f) {
                                entity.voxelRes = 5;
                            } else if (avgScale >= 4.0f) {
                                entity.voxelRes = 8;
                            } else if (avgScale >= 2.0f) {
                                entity.voxelRes = 6;
                            } else {
                                entity.voxelRes = 5;
                            }
                            entity.ResetVoxelMask();
                        }

                        // Voxel chunk destruction: remove the hit cell only
                        if (entity.voxelDestruction && entity.meshType == MeshType::Cube) {
                            // Prefer direct cell index from physics (works from any direction)
                            bool removed = false;
                            if (hit.voxelCellIndex >= 0) {
                                removed = entity.RemoveVoxelCell(hit.voxelCellIndex);
                            } else {
                                // Fallback for first hit before per-cell colliders exist
                                removed = entity.RemoveVoxelAt(hit.hitPosition.x, hit.hitPosition.y, hit.hitPosition.z);
                            }
                            if (removed) {
                                // Spawn small debris for the removed chunk
                                XMFLOAT3 chunkScale = {
                                    entity.scale[0] / entity.voxelRes,
                                    entity.scale[1] / entity.voxelRes,
                                    entity.scale[2] / entity.voxelRes
                                };
                                m_particles.SpawnMaterialImpact(hit.hitPosition, hit.hitNormal,
                                                                entity.color, entity.materialType);
                                m_particles.SpawnDebris(hit.hitPosition, chunkScale, entity.color, 4, 0.5f);

                                // Rebuild colliders so player can walk/shoot through the hole
                                m_physicsWorld.RebuildStaticColliders(m_editorState.scene);
                            }

                            // Destroy entity only when ALL voxel cells are gone
                            if (entity.GetActiveVoxelCount() == 0) {
                                entity.health = 0.0f;
                            }
                        } else {
                            // Non-voxel entities: normal HP damage
                            float damage = m_weaponSystem.GetCurrentDef().damage
                                         * m_weaponSystem.GetCurrentDef().pelletsPerShot;
                            entity.TakeDamage(damage);
                        }

                        bool destroyed = entity.IsDestroyed();

                        // Spawn smoke if entity is below 50% health (still alive, non-voxel only)
                        if (!destroyed && !entity.voxelDestruction &&
                            entity.GetHealthFraction() < 0.5f && entity.smokeOnDamage) {
                            XMFLOAT3 smokeCenter = { entity.position[0], entity.position[1], entity.position[2] };
                            XMFLOAT3 smokeScale = { entity.scale[0], entity.scale[1], entity.scale[2] };
                            m_particles.SpawnSmoke(smokeCenter, smokeScale, 2);
                        }

                        // Fire embers on critical damage (below 25%, non-voxel only)
                        if (!destroyed && !entity.voxelDestruction && entity.GetHealthFraction() < 0.25f) {
                            XMFLOAT3 fireCenter = { entity.position[0], entity.position[1], entity.position[2] };
                            XMFLOAT3 fireScale = { entity.scale[0], entity.scale[1], entity.scale[2] };
                            m_particles.SpawnFireEmbers(fireCenter, fireScale, 3);
                        }

                        if (destroyed) {
                            // IMPORTANT: Copy entity data BEFORE modifying the scene,
                            // because AddEntity/RemoveEntity can reallocate the vector
                            // and invalidate the 'entity' reference.
                            Entity destroyedCopy = entity;

                            // Full material-aware explosion
                            XMFLOAT3 center = { destroyedCopy.position[0], destroyedCopy.position[1], destroyedCopy.position[2] };
                            XMFLOAT3 entScale = { destroyedCopy.scale[0], destroyedCopy.scale[1], destroyedCopy.scale[2] };
                            m_particles.SpawnMaterialExplosion(center, entScale, destroyedCopy.color,
                                                              destroyedCopy.debrisCount, destroyedCopy.debrisScale,
                                                              destroyedCopy.materialType);

                            // Screen shake proportional to entity size (reduced 90%)
                            float avgScale = (destroyedCopy.scale[0] + destroyedCopy.scale[1] + destroyedCopy.scale[2]) / 3.0f;
                            m_camera.AddScreenShake(0.008f * avgScale, 0.15f);

                            // --- Breakable sub-pieces: spawn smaller non-destructible entities ---
                            if (destroyedCopy.breakPieceCount > 0) {
                                for (int bp = 0; bp < destroyedCopy.breakPieceCount; bp++) {
                                    float angle = (bp / (float)destroyedCopy.breakPieceCount) * 6.283f;
                                    float spread = avgScale * 0.6f + 0.5f;
                                    float offX = cosf(angle) * spread;
                                    float offZ = sinf(angle) * spread;

                                    int idx = m_editorState.scene.AddEntity(destroyedCopy.name + "_debris", destroyedCopy.meshType);
                                    auto& piece = m_editorState.scene.GetEntity(idx);
                                    piece.meshName    = destroyedCopy.meshName;
                                    piece.textureName = destroyedCopy.textureName;
                                    // Random scale for each piece — small rubble
                                    float pScale = 0.08f + (rand() % 100) / 800.0f; // 0.08 - 0.205
                                    piece.scale[0] = destroyedCopy.scale[0] * pScale;
                                    piece.scale[1] = destroyedCopy.scale[1] * pScale;
                                    piece.scale[2] = destroyedCopy.scale[2] * pScale;
                                    // Fall to ground: position at ground level
                                    piece.position[0] = destroyedCopy.position[0] + offX;
                                    piece.position[1] = piece.scale[1] * 0.5f; // sit on ground (y=0)
                                    piece.position[2] = destroyedCopy.position[2] + offZ;
                                    // Dramatic tilt — fallen rubble look
                                    piece.rotation[0] = (float)(rand() % 60 - 30);
                                    piece.rotation[1] = (float)(rand() % 360);
                                    piece.rotation[2] = (float)(rand() % 60 - 30);
                                    // Darken color for debris look
                                    piece.color[0] = destroyedCopy.color[0] * 0.6f;
                                    piece.color[1] = destroyedCopy.color[1] * 0.6f;
                                    piece.color[2] = destroyedCopy.color[2] * 0.6f;
                                    piece.color[3] = destroyedCopy.color[3];
                                    piece.materialType  = destroyedCopy.materialType;
                                    piece.destructible  = false;  // Sub-pieces are NOT destructible
                                    piece.noCollision   = true;   // No collision on debris
                                    piece.despawnTimer  = 8.0f;   // Despawn after 8 seconds
                                    piece.castShadow    = true;
                                    piece.visible       = true;
                                }
                            }

                            // --- Structural support: auto-collapse entities resting on top ---
                            // Destroyed entity bounding box top
                            float dTop    = destroyedCopy.position[1] + destroyedCopy.scale[1] * 0.5f;
                            float dBottom = destroyedCopy.position[1] - destroyedCopy.scale[1] * 0.5f;
                            float dMinX   = destroyedCopy.position[0] - destroyedCopy.scale[0] * 0.5f;
                            float dMaxX   = destroyedCopy.position[0] + destroyedCopy.scale[0] * 0.5f;
                            float dMinZ   = destroyedCopy.position[2] - destroyedCopy.scale[2] * 0.5f;
                            float dMaxZ   = destroyedCopy.position[2] + destroyedCopy.scale[2] * 0.5f;

                            // Remove the destroyed entity
                            m_editorState.scene.RemoveEntity(hit.entityIndex);

                            // Deselect if it was selected
                            if (m_editorState.selectedEntity == hit.entityIndex)
                                m_editorState.selectedEntity = -1;
                            else if (m_editorState.selectedEntity > hit.entityIndex)
                                m_editorState.selectedEntity--;

                            // Auto structural support: any entity whose bottom rests near
                            // the top of the destroyed entity (or overlaps vertically and horizontally)
                            // will collapse. This handles roofs on walls, stacked objects, etc.
                            for (int si = m_editorState.scene.GetEntityCount() - 1; si >= 0; si--) {
                                auto& supported = m_editorState.scene.GetEntity(si);

                                // Check explicit name-based support OR automatic proximity
                                bool shouldCollapse = false;

                                // Name-based: supportedBy field matches destroyed entity name
                                if (!supported.supportedBy.empty() && supported.supportedBy == destroyedCopy.name) {
                                    shouldCollapse = true;
                                }

                                // Proximity-based: entity bottom is near destroyed entity top,
                                // and they overlap horizontally (cube entities only —
                                // custom meshes have complex shapes, skip auto-collapse)
                                if (!shouldCollapse &&
                                    destroyedCopy.meshType == MeshType::Cube &&
                                    supported.meshType == MeshType::Cube) {
                                    float sBottom = supported.position[1] - supported.scale[1] * 0.5f;
                                    float sMinX   = supported.position[0] - supported.scale[0] * 0.5f;
                                    float sMaxX   = supported.position[0] + supported.scale[0] * 0.5f;
                                    float sMinZ   = supported.position[2] - supported.scale[2] * 0.5f;
                                    float sMaxZ   = supported.position[2] + supported.scale[2] * 0.5f;

                                    // Bottom of supported entity is within 1.5 units of top of destroyed
                                    float tolerance = 1.5f;
                                    bool restingOnTop = (sBottom >= dTop - tolerance) && (sBottom <= dTop + tolerance);
                                    // Horizontal overlap check (XZ bounding boxes intersect)
                                    bool overlapX = (sMinX < dMaxX) && (sMaxX > dMinX);
                                    bool overlapZ = (sMinZ < dMaxZ) && (sMaxZ > dMinZ);

                                    if (restingOnTop && overlapX && overlapZ) {
                                        shouldCollapse = true;
                                    }
                                }

                                if (shouldCollapse) {
                                    // Cascade: explode the supported entity
                                    XMFLOAT3 sc = { supported.position[0], supported.position[1], supported.position[2] };
                                    XMFLOAT3 ss = { supported.scale[0], supported.scale[1], supported.scale[2] };
                                    m_particles.SpawnMaterialExplosion(sc, ss, supported.color,
                                                                      supported.debrisCount, supported.debrisScale,
                                                                      supported.materialType);
                                    float suppAvg = (supported.scale[0] + supported.scale[1] + supported.scale[2]) / 3.0f;
                                    m_camera.AddScreenShake(0.006f * suppAvg, 0.12f);

                                    m_editorState.scene.RemoveEntity(si);
                                    if (m_editorState.selectedEntity == si)
                                        m_editorState.selectedEntity = -1;
                                    else if (m_editorState.selectedEntity > si)
                                        m_editorState.selectedEntity--;

                                    LOG_INFO("Supported entity collapsed!");
                                }
                            }

                            // Rebuild physics colliders
                            m_physicsWorld.RebuildStaticColliders(m_editorState.scene);

                            LOG_INFO("Entity destroyed!");
                        }
                    }
                } else {
                    // Hit world geometry (no entity) — default sparks
                    m_particles.SpawnImpactSparks(hit.hitPosition, hit.hitNormal, 6);
                }
            }
        }

        // Update AI agent damage flash timers
        for (int i = 0; i < m_aiSystem.GetAgentCount(); i++) {
            auto& agent = m_aiSystem.GetAgent(i);
            if (agent.damageFlashTimer > 0.0f) {
                agent.damageFlashTimer -= dt;
            }
        }
    } else {
        // ---- Fly Camera ----
        if (!editorWantsKeyboard) {
            float speed = m_editorState.cameraMoveSpeed * dt;
            if (m_input.IsKeyDown(VK_SHIFT)) speed *= m_editorState.cameraSprintMult;

            XMFLOAT3 forward = m_camera.GetForward();
            XMFLOAT3 right   = m_camera.GetRight();
            XMFLOAT3 pos     = m_camera.GetPosition();

            if (m_input.IsKeyDown('W')) {
                pos.x += forward.x * speed;
                pos.y += forward.y * speed;
                pos.z += forward.z * speed;
            }
            if (m_input.IsKeyDown('S')) {
                pos.x -= forward.x * speed;
                pos.y -= forward.y * speed;
                pos.z -= forward.z * speed;
            }
            if (m_input.IsKeyDown('A')) {
                pos.x -= right.x * speed;
                pos.z -= right.z * speed;
            }
            if (m_input.IsKeyDown('D')) {
                pos.x += right.x * speed;
                pos.z += right.z * speed;
            }
            if (m_input.IsKeyDown(VK_SPACE)) {
                pos.y += speed;
            }
            if (m_input.IsKeyDown(VK_CONTROL)) {
                pos.y -= speed;
            }

            m_camera.SetPosition(pos);
        } // end !editorWantsKeyboard

        // Reset camera roll in fly cam mode
        m_camera.SetRoll(0.0f);
    }

    // ---- Particle System Update ----
    m_particles.SetGroundY(m_editorState.charGroundY);
    m_particles.Update(dt);

    // ---- Entity Damage Flash Timers ----
    for (int i = 0; i < m_editorState.scene.GetEntityCount(); i++) {
        auto& e = m_editorState.scene.GetEntity(i);
        if (e.damageFlashTimer > 0.0f) {
            e.damageFlashTimer -= dt;
            if (e.damageFlashTimer < 0.0f) e.damageFlashTimer = 0.0f;
        }
    }

    // ---- Camera Screen Shake ----
    m_camera.UpdateShake(dt);

    // ---- AI Navigation & Agents ----
    // Sync nav grid settings from editor
    m_navGrid.showDebug = m_editorState.navShowDebug;
    m_aiSystem.showDebug = m_editorState.aiShowDebug;

    // Handle nav grid rebuild request
    if (m_editorState.navRebuildRequested) {
        m_editorState.navRebuildRequested = false;
        // Re-init grid if size changed
        if (m_navGrid.GetWidth() != m_editorState.navGridWidth ||
            m_navGrid.GetHeight() != m_editorState.navGridHeight ||
            m_navGrid.GetCellSize() != m_editorState.navCellSize) {
            m_navGrid.Init(m_editorState.navGridWidth, m_editorState.navGridHeight,
                           m_editorState.navCellSize,
                           m_editorState.navOriginX, m_editorState.navOriginZ,
                           m_editorState.navGridY);
        }
        m_navGrid.SetOrigin(m_editorState.navOriginX, m_editorState.navOriginZ);
        m_navGrid.SetGridY(m_editorState.navGridY);
        m_navGrid.RebuildFromEntities(m_editorState.scene);
        LOG_INFO("NavGrid rebuilt from %d entities", m_editorState.scene.GetEntityCount());

        // Also rebuild physics colliders when entities change
        m_physicsWorld.RebuildStaticColliders(m_editorState.scene);
    }

    // Handle agent spawn request (selectedAgent == -2 means "spawn new")
    if (m_editorState.aiSelectedAgent == -2) {
        XMFLOAT3 spawnPos = { m_editorState.aiSpawnPos[0],
                              m_editorState.aiSpawnPos[1],
                              m_editorState.aiSpawnPos[2] };
        int idx = m_aiSystem.AddAgent("", spawnPos);
        AIAgent& agent = m_aiSystem.GetAgent(idx);
        agent.settings.moveSpeed   = m_editorState.aiDefaultSpeed;
        agent.settings.chaseSpeed  = m_editorState.aiDefaultChaseSpeed;
        agent.settings.detectRange = m_editorState.aiDefaultDetectRange;
        agent.settings.loseRange   = m_editorState.aiDefaultLoseRange;
        memcpy(agent.settings.bodyColor, m_editorState.aiDefaultColor, sizeof(float) * 4);
        m_editorState.aiSelectedAgent = idx;
    }

    // Update AI agents — pass player position for chase detection
    XMFLOAT3 playerPos = m_camera.GetPosition();
    if (m_characterMode) {
        playerPos = m_character.GetPosition();
    }
    m_aiSystem.Update(dt, m_navGrid, playerPos,
                       m_editorState.physicsCollisionEnabled ? &m_physicsWorld : nullptr);

    // Apply editor state changes from panels
    if (m_editorState.rendererDirty) {
        m_editorState.rendererDirty = false;
        m_renderer.SetWireframe(m_editorState.wireframe);
        m_renderer.SetVSync(m_editorState.vsync);
        m_renderer.SetMSAA(m_editorState.msaaSamples);
    }

    if (m_editorState.cameraDirty) {
        m_editorState.cameraDirty = false;
        m_camera.Init(m_editorState.cameraFOV, m_renderer.GetAspectRatio(),
                      m_editorState.cameraNearZ, m_editorState.cameraFarZ);
    }

    m_debugRenderer.SetEnabled(m_editorState.showDebug);

    // Sync weapon system settings from editor
    m_weaponSystem.showDebug = m_editorState.weaponShowDebug;
    // Switch weapon if editor changed selection
    if (static_cast<int>(m_weaponSystem.GetCurrentWeapon()) != m_editorState.weaponType) {
        m_weaponSystem.SwitchWeapon(static_cast<WeaponType>(m_editorState.weaponType));
    }
    // Sync HUD visibility
    m_hud.GetSettings().showCrosshair = m_editorState.weaponShowHUD;
    m_hud.GetSettings().showAmmo      = m_editorState.weaponShowHUD;
    m_hud.GetSettings().showHitMarker = m_editorState.weaponShowHUD;
    m_hud.GetSettings().showReloadBar = m_editorState.weaponShowHUD;

    // Sync post-processing settings from editor state
    m_postProcessSettings.bloomEnabled       = m_editorState.ppBloomEnabled;
    m_postProcessSettings.bloomThreshold     = m_editorState.ppBloomThreshold;
    m_postProcessSettings.bloomIntensity     = m_editorState.ppBloomIntensity;
    m_postProcessSettings.vignetteEnabled    = m_editorState.ppVignetteEnabled;
    m_postProcessSettings.vignetteIntensity  = m_editorState.ppVignetteIntensity;
    m_postProcessSettings.vignetteSmoothness = m_editorState.ppVignetteSmoothness;
    m_postProcessSettings.brightness         = m_editorState.ppBrightness;
    m_postProcessSettings.contrast           = m_editorState.ppContrast;
    m_postProcessSettings.saturation         = m_editorState.ppSaturation;
    m_postProcessSettings.gamma              = m_editorState.ppGamma;
    m_postProcessSettings.tint[0]            = m_editorState.ppTint[0];
    m_postProcessSettings.tint[1]            = m_editorState.ppTint[1];
    m_postProcessSettings.tint[2]            = m_editorState.ppTint[2];
    m_postProcessSettings.ssaoEnabled        = m_editorState.ssaoEnabled;

    // Sync art style settings
    m_postProcessSettings.outlineEnabled       = m_editorState.outlineEnabled;
    m_postProcessSettings.outlineThickness     = m_editorState.outlineThickness;
    m_postProcessSettings.outlineColor[0]      = m_editorState.outlineColor[0];
    m_postProcessSettings.outlineColor[1]      = m_editorState.outlineColor[1];
    m_postProcessSettings.outlineColor[2]      = m_editorState.outlineColor[2];
    m_postProcessSettings.paperGrainIntensity  = m_editorState.paperGrainIntensity;
    m_postProcessSettings.hatchingIntensity    = m_editorState.hatchingIntensity;
    m_postProcessSettings.hatchingScale        = m_editorState.hatchingScale;

    // Sync SSAO settings
    m_ssaoSettings.enabled    = m_editorState.ssaoEnabled;
    m_ssaoSettings.radius     = m_editorState.ssaoRadius;
    m_ssaoSettings.bias       = m_editorState.ssaoBias;
    m_ssaoSettings.intensity  = m_editorState.ssaoIntensity;
    m_ssaoSettings.kernelSize = m_editorState.ssaoKernelSize;

    // Update level editor
    m_levelEditor.Update(dt, m_editorState);

    // Hot-swap: level editor pushed a new scene into the game
    if (m_levelEditor.HasPendingHotSwap()) {
        m_levelEditor.ClearHotSwap();
        m_editorState.physicsRebuildRequested = true;
        LOG_INFO("Hot-swap: level editor scene applied to game");
    }
}

// ==================== Render ====================

void Application::Render() {
    // ---- Level Editor Window (separate swap chain) ----
    if (m_levelEditor.IsOpen()) {
        m_levelEditor.Render(m_renderer.GetContext(), m_editorState);
    }

    auto* ctx = m_renderer.GetContext();

    // ============================================================
    // PASS 1: Shadow Map (depth-only from light's POV)
    // ============================================================
    XMFLOAT3 sunDir = { m_editorState.sunDirection[0], m_editorState.sunDirection[1], m_editorState.sunDirection[2] };
    XMFLOAT3 sceneCenter = { 0.0f, 0.0f, 0.0f };
    float sceneRadius = m_editorState.shadowDistance;

    XMMATRIX lightVP = m_shadowMap.CalcLightViewProjection(sunDir, sceneCenter, sceneRadius);

    if (m_editorState.shadowsEnabled) {
        m_shadowMap.BeginShadowPass(ctx);

        // Bind shadow VS (no pixel shader — depth only)
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(m_shadowShader.GetVS(), nullptr, 0);
        ctx->PSSetShader(nullptr, nullptr, 0);
        ctx->IASetInputLayout(m_voxelShader.GetInputLayout());

        // Update shadow CB with light VP
        CBShadow shadowData = {};
        XMStoreFloat4x4(&shadowData.LightViewProjection, XMMatrixTranspose(lightVP));
        shadowData.ShadowBias       = m_editorState.shadowBias;
        shadowData.ShadowNormalBias = m_editorState.shadowNormalBias;
        shadowData.ShadowIntensity  = m_editorState.shadowIntensity;
        shadowData.ShadowMapSize    = static_cast<float>(m_shadowMap.GetResolution());
        m_cbShadow.Update(ctx, shadowData);
        m_cbShadow.BindBoth(ctx, 4);

        CBPerObject objData = {};

        // Shadow pass: Ground
        XMMATRIX groundWorld = XMMatrixTranslation(0.0f, -0.01f, 0.0f);
        XMStoreFloat4x4(&objData.World, XMMatrixTranspose(groundWorld));
        XMStoreFloat4x4(&objData.WorldInvTranspose, XMMatrixInverse(nullptr, groundWorld));
        m_cbPerObject.Update(ctx, objData);
        m_groundMesh.Draw(ctx);

        // Shadow pass: Entities
        for (int i = 0; i < m_editorState.scene.GetEntityCount(); i++) {
            const auto& e = m_editorState.scene.GetEntity(i);
            if (!e.visible || !e.castShadow) continue;

            // Voxel chunk shadow: draw each active cell
            if (e.voxelDestruction && e.meshType == MeshType::Cube) {
                int res = e.voxelRes;
                for (int vz = 0; vz < res; vz++)
                for (int vy = 0; vy < res; vy++)
                for (int vx = 0; vx < res; vx++) {
                    int idx = vx + vy * res + vz * res * res;
                    if (!e.IsVoxelCellActive(idx)) continue;
                    XMMATRIX cellWorld = e.GetVoxelCellWorldMatrix(vx, vy, vz);
                    XMStoreFloat4x4(&objData.World, XMMatrixTranspose(cellWorld));
                    XMStoreFloat4x4(&objData.WorldInvTranspose, XMMatrixInverse(nullptr, cellWorld));
                    m_cbPerObject.Update(ctx, objData);
                    m_cbPerObject.BindVS(ctx, 1);
                    m_cubeMesh.Draw(ctx);
                }
            } else {
                XMMATRIX entWorld = e.GetWorldMatrix();
                XMStoreFloat4x4(&objData.World, XMMatrixTranspose(entWorld));
                XMStoreFloat4x4(&objData.WorldInvTranspose, XMMatrixInverse(nullptr, entWorld));
                m_cbPerObject.Update(ctx, objData);
                m_cbPerObject.BindVS(ctx, 1);
                if (e.meshType == MeshType::Cube) {
                    m_cubeMesh.Draw(ctx);
                } else if (e.meshType == MeshType::Custom) {
                    Mesh* customMesh = ResourceManager::Get().GetMesh(e.meshName);
                    if (customMesh) customMesh->Draw(ctx);
                }
            }
        }

        // Shadow pass: Character body parts — disabled (player doesn't cast shadow)
        // if (m_characterMode && m_editorState.charShowBody) { ... }

        // Shadow pass: AI Agents
        for (int i = 0; i < m_aiSystem.GetAgentCount(); i++) {
            const auto& agent = m_aiSystem.GetAgent(i);
            if (!agent.visible || !agent.active) continue;
            float halfScale = agent.settings.bodyScale * 0.5f;
            XMMATRIX agentScale = XMMatrixScaling(agent.settings.bodyScale,
                                                   agent.settings.bodyScale,
                                                   agent.settings.bodyScale);
            XMMATRIX agentRot   = XMMatrixRotationY(XMConvertToRadians(agent.yaw));
            XMMATRIX agentTrans = XMMatrixTranslation(agent.position.x,
                                                       agent.position.y + halfScale,
                                                       agent.position.z);
            XMMATRIX agentWorld = agentScale * agentRot * agentTrans;
            XMStoreFloat4x4(&objData.World, XMMatrixTranspose(agentWorld));
            XMStoreFloat4x4(&objData.WorldInvTranspose, XMMatrixInverse(nullptr, agentWorld));
            m_cbPerObject.Update(ctx, objData);
            m_cbPerObject.BindVS(ctx, 1);
            m_cubeMesh.Draw(ctx);
        }

        m_shadowMap.EndShadowPass(ctx);
    }

    // ============================================================
    // PASS 2: Main Scene
    // ============================================================
    m_renderer.BeginFrame(m_editorState.clearColor[0], m_editorState.clearColor[1],
                          m_editorState.clearColor[2], m_editorState.clearColor[3]);

    // ---- Per-Frame Setup (b0) ----
    XMMATRIX view     = m_camera.GetViewMatrix();

    // Apply screen shake offset to the view
    if (m_camera.IsShaking()) {
        XMFLOAT3 shake = m_camera.GetShakeOffset();
        view = view * XMMatrixTranslation(shake.x, shake.y, shake.z);
    }

    XMMATRIX proj     = m_camera.GetProjectionMatrix();
    XMMATRIX viewProj = view * proj;

    CBPerFrame frameData = {};
    XMStoreFloat4x4(&frameData.View,           XMMatrixTranspose(view));
    XMStoreFloat4x4(&frameData.Projection,     XMMatrixTranspose(proj));
    XMStoreFloat4x4(&frameData.ViewProjection, XMMatrixTranspose(viewProj));

    XMVECTOR det = XMMatrixDeterminant(viewProj);
    XMMATRIX invVP = XMMatrixInverse(&det, viewProj);
    XMStoreFloat4x4(&frameData.InvViewProjection, XMMatrixTranspose(invVP));

    frameData.CameraPosition = m_camera.GetPosition();
    frameData.Time       = m_timer.TotalTime();
    frameData.ScreenSize = { static_cast<float>(m_width), static_cast<float>(m_height) };
    frameData.NearZ      = 0.1f;
    frameData.FarZ       = 500.0f;
    m_cbPerFrame.Update(ctx, frameData);
    m_cbPerFrame.BindBoth(ctx, 0);

    // ---- Lighting Setup (b2) ----
    CBLighting lightData = {};
    lightData.SunDirection     = sunDir;
    lightData.SunIntensity     = m_editorState.sunIntensity;
    lightData.SunColor         = { m_editorState.sunColor[0], m_editorState.sunColor[1], m_editorState.sunColor[2] };
    lightData.AmbientColor     = { m_editorState.ambientColor[0], m_editorState.ambientColor[1], m_editorState.ambientColor[2] };
    lightData.AmbientIntensity = m_editorState.ambientIntensity;
    lightData.FogColor         = { m_editorState.fogColor[0], m_editorState.fogColor[1], m_editorState.fogColor[2] };
    lightData.FogDensity       = m_editorState.fogDensity;
    lightData.FogStart         = m_editorState.fogStart;
    lightData.FogEnd           = m_editorState.fogEnd;
    lightData.CelEnabled       = m_editorState.celEnabled ? 1.0f : 0.0f;
    lightData.CelRimIntensity  = m_editorState.celRimIntensity;
    lightData.CelBands         = m_editorState.celBands;
    m_cbLighting.Update(ctx, lightData);
    m_cbLighting.BindPS(ctx, 2);

    // ---- Sky Setup (b3) ----
    CBSky skyData = {};
    skyData.ZenithColor      = { m_editorState.skyZenithColor[0], m_editorState.skyZenithColor[1], m_editorState.skyZenithColor[2] };
    skyData.Brightness       = m_editorState.skyBrightness;
    skyData.HorizonColor     = { m_editorState.skyHorizonColor[0], m_editorState.skyHorizonColor[1], m_editorState.skyHorizonColor[2] };
    skyData.HorizonFalloff   = m_editorState.skyHorizonFalloff;
    skyData.GroundColor      = { m_editorState.skyGroundColor[0], m_editorState.skyGroundColor[1], m_editorState.skyGroundColor[2] };
    skyData.SunDiscSize      = m_editorState.sunDiscSize;
    skyData.SunGlowIntensity = m_editorState.sunGlowIntensity;
    skyData.SunGlowFalloff   = m_editorState.sunGlowFalloff;
    skyData.CloudCoverage    = m_editorState.cloudCoverage;
    skyData.CloudSpeed       = m_editorState.cloudSpeed;
    skyData.CloudDensity     = m_editorState.cloudDensity;
    skyData.CloudHeight      = m_editorState.cloudHeight;
    skyData.CloudColor       = { m_editorState.cloudColor[0], m_editorState.cloudColor[1], m_editorState.cloudColor[2] };
    skyData.CloudSunInfluence = m_editorState.cloudSunInfluence;
    m_cbSky.Update(ctx, skyData);
    m_cbSky.BindBoth(ctx, 3);

    // ---- Shadow Setup (b4) — for main pass sampling ----
    CBShadow shadowData = {};
    XMStoreFloat4x4(&shadowData.LightViewProjection, XMMatrixTranspose(lightVP));
    shadowData.ShadowBias       = m_editorState.shadowBias;
    shadowData.ShadowNormalBias = m_editorState.shadowNormalBias;
    shadowData.ShadowIntensity  = m_editorState.shadowsEnabled ? m_editorState.shadowIntensity : 0.0f;
    shadowData.ShadowMapSize    = static_cast<float>(m_shadowMap.GetResolution());
    m_cbShadow.Update(ctx, shadowData);
    m_cbShadow.BindBoth(ctx, 4);

    // Bind shadow map SRV for scene shaders to sample
    m_shadowMap.BindSRV(ctx, 0);

    // ---- Post-Processing: Redirect rendering to HDR buffer ----
    bool postProcessOn = m_postProcessSettings.bloomEnabled || m_postProcessSettings.vignetteEnabled
                         || m_postProcessSettings.outlineEnabled
                         || m_postProcessSettings.paperGrainIntensity > 0.001f
                         || m_postProcessSettings.hatchingIntensity > 0.001f;
    if (postProcessOn) {
        // When post-processing is on, we render to a non-MSAA HDR buffer
        // Clear the non-MSAA depth buffer (BeginFrame may have only cleared MSAA DSV)
        ctx->ClearDepthStencilView(m_renderer.GetNonMSAADSV(),
            D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        // Redirect scene rendering to the HDR buffer
        m_postProcess.BeginSceneCapture(ctx, m_renderer.GetNonMSAADSV());
    }

    // ---- Draw Sky (fullscreen triangle, no depth write) ----
    m_renderer.SetDepthEnabled(false);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetInputLayout(nullptr);
    ctx->VSSetShader(m_skyShader.GetVS(), nullptr, 0);
    ctx->PSSetShader(m_skyShader.GetPS(), nullptr, 0);
    ctx->Draw(3, 0);
    m_renderer.TrackDrawCall(3);
    m_renderer.SetDepthEnabled(true);

    // ---- Draw Objects ----
    m_voxelShader.Bind(ctx);

    CBPerObject objData = {};

    // Ground plane
    m_groundShader.Bind(ctx);
    // Bind white texture for ground (uses procedural shader pattern, not texture)
    Texture* defaultWhiteTex = ResourceManager::Get().GetTexture("_white");
    if (defaultWhiteTex) defaultWhiteTex->Bind(ctx, 1);
    XMMATRIX groundWorld = XMMatrixTranslation(0.0f, -0.01f, 0.0f);
    XMStoreFloat4x4(&objData.World, XMMatrixTranspose(groundWorld));
    XMStoreFloat4x4(&objData.WorldInvTranspose, XMMatrixInverse(nullptr, groundWorld));
    m_cbPerObject.Update(ctx, objData);
    m_cbPerObject.BindBoth(ctx, 1);
    m_groundMesh.Draw(ctx);
    m_renderer.TrackDrawCall(m_groundMesh.GetIndexCount());

    // ---- Draw Entities ----
    m_voxelShader.Bind(ctx);
    Texture* whiteTex = ResourceManager::Get().GetTexture("_white");
    for (int i = 0; i < m_editorState.scene.GetEntityCount(); i++) {
        const auto& e = m_editorState.scene.GetEntity(i);
        if (!e.visible) continue;

        // Use damage-tinted color (darkens + flash on hit)
        float damagedColor[4];
        e.GetDamagedColor(damagedColor);
        objData.ObjectColor = { damagedColor[0], damagedColor[1], damagedColor[2], damagedColor[3] };

        // Fill hit decal data for the shader
        for (int di = 0; di < Entity::MAX_HIT_DECALS; di++) {
            if (di < e.hitDecalCount) {
                objData.HitDecals[di] = { e.hitDecalPos[di].x, e.hitDecalPos[di].y,
                                           e.hitDecalPos[di].z, e.hitDecalIntensity[di] };
            } else {
                objData.HitDecals[di] = { 0, 0, 0, 0 };
            }
        }
        objData.HitDecalCount = static_cast<float>(e.hitDecalCount);

        // Bind texture once for this entity
        if (e.meshType == MeshType::Cube) {
            Texture* ct = nullptr;
            if (!e.textureName.empty())
                ct = ResourceManager::Get().GetTexture(e.textureName);
            if (ct) ct->Bind(ctx, 1);
            else if (whiteTex) whiteTex->Bind(ctx, 1);
        } else if (e.meshType == MeshType::Custom) {
            Texture* modelTex = nullptr;
            if (!e.textureName.empty())
                modelTex = ResourceManager::Get().GetTexture(e.textureName);
            if (!modelTex)
                modelTex = ResourceManager::Get().GetTexture(e.meshName);
            if (modelTex) modelTex->Bind(ctx, 1);
            else if (whiteTex) whiteTex->Bind(ctx, 1);
        }

        // Voxel chunk rendering: draw each active cell as a sub-cube
        if (e.voxelDestruction && e.meshType == MeshType::Cube) {
            int res = e.voxelRes;
            for (int vz = 0; vz < res; vz++)
            for (int vy = 0; vy < res; vy++)
            for (int vx = 0; vx < res; vx++) {
                int idx = vx + vy * res + vz * res * res;
                if (!e.IsVoxelCellActive(idx)) continue;

                XMMATRIX cellWorld = e.GetVoxelCellWorldMatrix(vx, vy, vz);
                XMStoreFloat4x4(&objData.World, XMMatrixTranspose(cellWorld));
                XMStoreFloat4x4(&objData.WorldInvTranspose, XMMatrixInverse(nullptr, cellWorld));
                m_cbPerObject.Update(ctx, objData);
                m_cbPerObject.BindBoth(ctx, 1);
                m_cubeMesh.Draw(ctx);
                m_renderer.TrackDrawCall(m_cubeMesh.GetIndexCount());
            }
        } else {
            // Normal full-entity rendering
            XMMATRIX entWorld = e.GetWorldMatrix();
            XMStoreFloat4x4(&objData.World, XMMatrixTranspose(entWorld));
            XMStoreFloat4x4(&objData.WorldInvTranspose, XMMatrixInverse(nullptr, entWorld));
            m_cbPerObject.Update(ctx, objData);
            m_cbPerObject.BindBoth(ctx, 1);

            if (e.meshType == MeshType::Cube) {
                m_cubeMesh.Draw(ctx);
                m_renderer.TrackDrawCall(m_cubeMesh.GetIndexCount());
            } else if (e.meshType == MeshType::Custom) {
                Mesh* customMesh = ResourceManager::Get().GetMesh(e.meshName);
                if (customMesh) {
                    customMesh->Draw(ctx);
                    m_renderer.TrackDrawCall(customMesh->GetIndexCount());
                }
            }
        }
    }

    // Reset ObjectColor + hit decals after entities
    objData.ObjectColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    objData.HitDecalCount = 0.0f;
    for (int di = 0; di < 4; di++) objData.HitDecals[di] = { 0, 0, 0, 0 };
    m_cbPerObject.Update(ctx, objData);
    m_cbPerObject.BindBoth(ctx, 1);

    // ---- Draw Particles (debris, sparks, dust) ----
    if (m_particles.GetParticleCount() > 0) {
        m_voxelShader.Bind(ctx);
        if (whiteTex) whiteTex->Bind(ctx, 1);

        for (const auto& p : m_particles.GetParticles()) {
            if (!p.alive) continue;

            XMMATRIX pScale = XMMatrixScaling(p.scale.x, p.scale.y, p.scale.z);
            XMMATRIX pRot = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(p.rotation.x),
                XMConvertToRadians(p.rotation.y),
                XMConvertToRadians(p.rotation.z));
            XMMATRIX pTrans = XMMatrixTranslation(p.position.x, p.position.y, p.position.z);
            XMMATRIX pWorld = pScale * pRot * pTrans;

            XMStoreFloat4x4(&objData.World, XMMatrixTranspose(pWorld));
            XMStoreFloat4x4(&objData.WorldInvTranspose, XMMatrixInverse(nullptr, pWorld));
            objData.ObjectColor = { p.color[0], p.color[1], p.color[2], p.color[3] };
            m_cbPerObject.Update(ctx, objData);
            m_cbPerObject.BindBoth(ctx, 1);

            m_cubeMesh.Draw(ctx);
            m_renderer.TrackDrawCall(m_cubeMesh.GetIndexCount());
        }

        // Reset ObjectColor
        objData.ObjectColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_cbPerObject.Update(ctx, objData);
        m_cbPerObject.BindBoth(ctx, 1);
    }

    // ---- Draw Character Body (cube body parts) ----
    if (m_characterMode && m_editorState.charShowBody) {
        m_voxelShader.Bind(ctx);

        auto drawBodyPart = [&](const Character::BodyPart& bp, const float color[4]) {
            XMMATRIX S = XMMatrixScaling(bp.scale.x, bp.scale.y, bp.scale.z);
            XMMATRIX R = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(bp.rotation.x),
                XMConvertToRadians(bp.rotation.y),
                XMConvertToRadians(bp.rotation.z));
            XMMATRIX T = XMMatrixTranslation(bp.position.x, bp.position.y, bp.position.z);
            XMMATRIX bpWorld = S * R * T;

            CBPerObject bpObj = {};
            XMStoreFloat4x4(&bpObj.World, XMMatrixTranspose(bpWorld));
            XMStoreFloat4x4(&bpObj.WorldInvTranspose, XMMatrixInverse(nullptr, bpWorld));
            bpObj.ObjectColor = { color[0], color[1], color[2], color[3] };
            m_cbPerObject.Update(ctx, bpObj);
            m_cbPerObject.BindBoth(ctx, 1);
            m_cubeMesh.Draw(ctx);
            m_renderer.TrackDrawCall(m_cubeMesh.GetIndexCount());
        };

        drawBodyPart(m_character.GetHeadTransform(),     m_charSettings.headColor);
        drawBodyPart(m_character.GetTorsoTransform(),     m_charSettings.torsoColor);
        drawBodyPart(m_character.GetLeftArmTransform(),   m_charSettings.armsColor);
        drawBodyPart(m_character.GetRightArmTransform(),  m_charSettings.armsColor);
        drawBodyPart(m_character.GetLeftLegTransform(),    m_charSettings.legsColor);
        drawBodyPart(m_character.GetRightLegTransform(),   m_charSettings.legsColor);

        objData.ObjectColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_cbPerObject.Update(ctx, objData);
        m_cbPerObject.BindBoth(ctx, 1);
    }

    // ---- Draw Weapon Viewmodel (first-person gun) ----
    if (m_characterMode) {
        m_voxelShader.Bind(ctx);

        // Clear depth so viewmodel always renders on top
        ID3D11DepthStencilView* activeDSV = postProcessOn
            ? m_renderer.GetNonMSAADSV()
            : m_renderer.GetCurrentDSV();
        ctx->ClearDepthStencilView(activeDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

        // Draw gun model mesh (if using a loaded model)
        if (m_weaponSystem.HasGunModel()) {
            const auto& vm = m_weaponSystem.GetViewmodelMesh();
            if (!vm.meshName.empty()) {
                Mesh* gunMesh = ResourceManager::Get().GetMesh(vm.meshName);
                if (gunMesh) {
                    XMMATRIX S = XMMatrixScaling(vm.scale.x, vm.scale.y, vm.scale.z);
                    XMMATRIX R = XMMatrixRotationRollPitchYaw(
                        XMConvertToRadians(vm.rotation.x),
                        XMConvertToRadians(vm.rotation.y),
                        XMConvertToRadians(vm.rotation.z));
                    XMMATRIX T = XMMatrixTranslation(vm.position.x, vm.position.y, vm.position.z);
                    XMMATRIX gunWorld = S * R * T;

                    CBPerObject vmObj = {};
                    XMStoreFloat4x4(&vmObj.World, XMMatrixTranspose(gunWorld));
                    XMStoreFloat4x4(&vmObj.WorldInvTranspose, XMMatrixTranspose(InverseTranspose(gunWorld)));
                    vmObj.ObjectColor = { 0, 0, 0, 0 }; // alpha=0 → use vertex colors
                    m_cbPerObject.Update(ctx, vmObj);
                    m_cbPerObject.BindBoth(ctx, 1);

                    // Bind gun texture if specified, otherwise white fallback
                    Texture* gunTex = nullptr;
                    if (!vm.textureName.empty())
                        gunTex = ResourceManager::Get().GetTexture(vm.textureName);
                    if (!gunTex)
                        gunTex = ResourceManager::Get().GetTexture(vm.meshName);
                    if (gunTex) gunTex->Bind(ctx, 1);
                    else if (whiteTex) whiteTex->Bind(ctx, 1);

                    // Use no-cull rasterizer for gun mesh (safety net for mixed winding)
                    ctx->RSSetState(m_renderer.GetNoCullState());
                    gunMesh->Draw(ctx);
                    m_renderer.TrackDrawCall(gunMesh->GetIndexCount());
                    // Restore normal solid rasterizer
                    ctx->RSSetState(m_renderer.GetSolidState());
                }
            }
        }

        // Draw cube-based viewmodel parts (muzzle flash, etc.)
        if (whiteTex) whiteTex->Bind(ctx, 1);
        const auto& viewmodelParts = m_weaponSystem.GetViewmodelParts();
        for (const auto& vp : viewmodelParts) {
            const auto& bp = vp.transform;
            XMMATRIX S = XMMatrixScaling(bp.scale.x, bp.scale.y, bp.scale.z);
            XMMATRIX R = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(bp.rotation.x),
                XMConvertToRadians(bp.rotation.y),
                XMConvertToRadians(bp.rotation.z));
            XMMATRIX T = XMMatrixTranslation(bp.position.x, bp.position.y, bp.position.z);
            XMMATRIX bpWorld = S * R * T;

            CBPerObject vmObj = {};
            XMStoreFloat4x4(&vmObj.World, XMMatrixTranspose(bpWorld));
            XMStoreFloat4x4(&vmObj.WorldInvTranspose, XMMatrixInverse(nullptr, bpWorld));
            vmObj.ObjectColor = { vp.color[0], vp.color[1], vp.color[2], vp.color[3] };
            m_cbPerObject.Update(ctx, vmObj);
            m_cbPerObject.BindBoth(ctx, 1);
            m_cubeMesh.Draw(ctx);
            m_renderer.TrackDrawCall(m_cubeMesh.GetIndexCount());
        }

        // Reset ObjectColor
        objData.ObjectColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_cbPerObject.Update(ctx, objData);
        m_cbPerObject.BindBoth(ctx, 1);
    }

    // Unbind shadow map before next frame's shadow pass
    m_shadowMap.UnbindSRV(ctx, 0);

    // ---- SSAO: Compute ambient occlusion from depth ----
    if (m_ssaoSettings.enabled) {
        // Unbind depth stencil as render target before reading it
        ctx->OMSetRenderTargets(0, nullptr, nullptr);

        XMMATRIX ssaoView = m_camera.GetViewMatrix();
        XMMATRIX ssaoProj = m_camera.GetProjectionMatrix();
        m_ssao.Compute(ctx, m_renderer.GetDepthSRV(), ssaoProj, ssaoView, 0.1f, 500.0f, m_ssaoSettings);

        // Unbind SSAO render targets before binding the AO result as SRV
        // (prevents D3D11 read-write conflict that would force-unbind the SRV)
        ctx->OMSetRenderTargets(0, nullptr, nullptr);

        // Bind AO texture at t4 for the composite shader to use
        ID3D11ShaderResourceView* aoSRV = m_ssao.GetAOTexture();
        ctx->PSSetShaderResources(4, 1, &aoSRV);
    } else {
        // Unbind AO texture if SSAO is off
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx->PSSetShaderResources(4, 1, &nullSRV);
    }

    // ---- Post-Processing: Apply bloom, vignette, color grading ----
    if (postProcessOn) {
        // Composite post-processing to the back buffer
        m_postProcess.Apply(ctx, m_renderer.GetBackBufferRTV(), m_postProcessSettings,
                            m_renderer.GetDepthSRV());

        // Restore the renderer's render target for debug + ImGui
        ID3D11RenderTargetView* rtv = m_renderer.GetBackBufferRTV();
        ctx->OMSetRenderTargets(1, &rtv, m_renderer.GetNonMSAADSV());

        // Restore viewport
        D3D11_VIEWPORT vp = {};
        vp.Width  = static_cast<float>(m_width);
        vp.Height = static_cast<float>(m_height);
        vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);

        // Skip MSAA resolve since post-process already output to back buffer
        m_renderer.SetSkipMSAAResolve(true);
    } else {
        m_renderer.SetSkipMSAAResolve(false);
    }

    // Unbind AO texture
    if (m_ssaoSettings.enabled) {
        m_ssao.Unbind(ctx);
    }

    // ---- Debug Rendering ----
    if (m_editorState.showDebug) {
        m_debugRenderer.DrawGrid(20.0f, 20, { 0.4f, 0.4f, 0.4f, 0.5f });
        m_debugRenderer.DrawAxis({ 0.0f, 0.01f, 0.0f }, 3.0f);
        m_debugRenderer.DrawBox({ m_editorState.cubePosition[0], m_editorState.cubePosition[1], m_editorState.cubePosition[2] },
                                 { 0.5f * m_editorState.cubeScale[0], 0.5f * m_editorState.cubeScale[1], 0.5f * m_editorState.cubeScale[2] },
                                 { 1.0f, 1.0f, 0.0f, 0.6f });

        // Draw selection box for selected entity
        int sel = m_editorState.selectedEntity;
        if (sel >= 0 && sel < m_editorState.scene.GetEntityCount()) {
            const auto& e = m_editorState.scene.GetEntity(sel);
            m_debugRenderer.DrawBox(
                { e.position[0], e.position[1], e.position[2] },
                { 0.5f * e.scale[0], 0.5f * e.scale[1], 0.5f * e.scale[2] },
                { 0.2f, 0.8f, 1.0f, 0.8f });
        }

        // Health bars disabled — entities still have health/damage but no visual bar

        // Nav grid debug visualization
        m_navGrid.DebugDraw(m_debugRenderer);

        // AI agent debug visualization
        m_aiSystem.DebugDraw(m_debugRenderer, m_navGrid);

        // Physics collision debug visualization
        m_physicsWorld.DebugDraw(m_debugRenderer);

        // Weapon debug visualization
        m_weaponSystem.DebugDraw(m_debugRenderer);
    }

    // ---- Draw AI Agents as colored cubes ----
    {
        m_voxelShader.Bind(ctx);
        for (int i = 0; i < m_aiSystem.GetAgentCount(); i++) {
            const auto& agent = m_aiSystem.GetAgent(i);
            if (!agent.visible || !agent.active) continue;

            float halfScale = agent.settings.bodyScale * 0.5f;
            XMMATRIX agentScale = XMMatrixScaling(agent.settings.bodyScale,
                                                   agent.settings.bodyScale,
                                                   agent.settings.bodyScale);
            XMMATRIX agentRot   = XMMatrixRotationY(XMConvertToRadians(agent.yaw));
            XMMATRIX agentTrans = XMMatrixTranslation(agent.position.x,
                                                       agent.position.y + halfScale,
                                                       agent.position.z);
            XMMATRIX agentWorld = agentScale * agentRot * agentTrans;

            CBPerObject agentObj = {};
            XMStoreFloat4x4(&agentObj.World, XMMatrixTranspose(agentWorld));
            XMStoreFloat4x4(&agentObj.WorldInvTranspose, XMMatrixInverse(nullptr, agentWorld));

            // Flash white when taking damage
            if (agent.damageFlashTimer > 0.0f) {
                agentObj.ObjectColor = { 1.0f, 1.0f, 1.0f, 1.0f };
            } else {
                agentObj.ObjectColor = { agent.settings.bodyColor[0],
                                         agent.settings.bodyColor[1],
                                         agent.settings.bodyColor[2],
                                         agent.settings.bodyColor[3] };
            }
            m_cbPerObject.Update(ctx, agentObj);
            m_cbPerObject.BindBoth(ctx, 1);
            m_cubeMesh.Draw(ctx);
            m_renderer.TrackDrawCall(m_cubeMesh.GetIndexCount());
        }

        // Reset ObjectColor
        objData.ObjectColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_cbPerObject.Update(ctx, objData);
        m_cbPerObject.BindBoth(ctx, 1);
    }

    m_debugRenderer.Flush(ctx);

    // ---- Editor ImGui ----
    if (m_editorVisible) {
        m_editorUI.BeginFrame();
        m_editorPanels.Draw(m_editorState, m_renderer, m_camera,
                            m_timer.DeltaTime(), m_timer.FPS(), m_timer.TotalTime());

        // Draw HUD overlay (inside ImGui frame) when in character mode
        if (m_characterMode) {
            m_hud.Draw(m_weaponSystem, m_width, m_height);
        }

        m_editorUI.EndFrame();
    } else if (m_characterMode) {
        // Editor hidden but we still need ImGui for HUD
        m_editorUI.BeginFrame();
        m_hud.Draw(m_weaponSystem, m_width, m_height);
        m_editorUI.EndFrame();
    }

    m_renderer.EndFrame();
}

// ==================== Shutdown ====================

void Application::Shutdown() {
    m_input.SetCursorLocked(false);
    m_hud.Shutdown();
    m_weaponSystem.Shutdown();
    m_physicsWorld.Shutdown();
    m_aiSystem.Shutdown();
    m_navGrid.Shutdown();
    ResourceManager::Get().Shutdown();
    m_levelEditor.Shutdown();
    m_editorUI.Shutdown();
    m_debugRenderer.Shutdown();
    m_postProcess.Shutdown();
    m_ssao.Shutdown();
    m_shadowMap.Shutdown();
    m_defaultWhite.Release();
    m_groundMesh.Release();
    m_cubeMesh.Release();
    m_renderer.Shutdown();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    LOG_INFO("Application shutdown");
}

} // namespace WT
