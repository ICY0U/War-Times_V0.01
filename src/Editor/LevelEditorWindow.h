#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <Windows.h>
#include <string>
#include <vector>
#include "Graphics/Shader.h"
#include "Graphics/Mesh.h"
#include "Graphics/ConstantBuffer.h"
#include "Graphics/DebugRenderer.h"
#include "Graphics/Texture.h"
#include "Util/MathHelpers.h"
#include "Core/ResourceManager.h"
#include "PCG/LevelGenerator.h"
#include "PCG/WarfieldGenerator.h"

struct ImGuiContext;   // forward

namespace WT {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct EditorState; // forward

// ============================================================
// Edit tool mode for the level editor viewport
// ============================================================
enum class LevelEditTool : int {
    Select = 0,
    Move,
    Rotate,
    Scale,
    Place,
    Count
};

inline const char* LevelEditToolName(LevelEditTool t) {
    switch (t) {
        case LevelEditTool::Select: return "Select";
        case LevelEditTool::Move:   return "Move";
        case LevelEditTool::Rotate: return "Rotate";
        case LevelEditTool::Scale:  return "Scale";
        case LevelEditTool::Place:  return "Place";
        default: return "Unknown";
    }
}

// ============================================================
// Axis constraint for transform tools
// ============================================================
enum class AxisConstraint : int {
    None = 0,   // Free / all axes
    X    = 1,
    Y    = 2,
    Z    = 3,
    XZ   = 4    // Horizontal plane (default for Move)
};

inline const char* AxisConstraintName(AxisConstraint a) {
    switch (a) {
        case AxisConstraint::X:  return "X";
        case AxisConstraint::Y:  return "Y";
        case AxisConstraint::Z:  return "Z";
        case AxisConstraint::XZ: return "XZ";
        default: return "Free";
    }
}

// ============================================================
// Shared rendering resources (set by Application before use)
// ============================================================
struct LevelEditorResources {
    Shader*     voxelShader    = nullptr;
    Shader*     groundShader   = nullptr;
    Mesh*       cubeMesh       = nullptr;
    Mesh*       groundMesh     = nullptr;
    ConstantBuffer<CBPerFrame>*  cbPerFrame  = nullptr;
    ConstantBuffer<CBPerObject>* cbPerObject = nullptr;
    ConstantBuffer<CBLighting>*  cbLighting  = nullptr;
};

/// Separate OS-level window for runtime level editing.
/// Full 3D viewport with ImGui outliner panel, entity editing, and placement tools.
/// Shares the D3D11 device from the main renderer but has its own swap chain + ImGui context.
class LevelEditorWindow {
public:
    bool Init(ID3D11Device* sharedDevice, HINSTANCE hInstance, int width = 1200, int height = 800);
    void Shutdown();

    void SetResources(const LevelEditorResources& res) { m_res = res; }

    void Update(float dt, EditorState& state);
    void Render(ID3D11DeviceContext* ctx, EditorState& state);

    bool IsOpen() const { return m_open; }
    void SetOpen(bool open);
    HWND GetHWND() const { return m_hwnd; }

    // Forward Win32 messages (called from its own WndProc)
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Level file operations
    void SaveCurrentLevel(EditorState& state);
    void LoadLevel(const std::string& path, EditorState& state);
    void NewLevel(EditorState& state);
    void DeleteCurrentLevel(EditorState& state);
    const std::string& GetLevelsDirectory() const { return m_levelsDirectory; }
    const std::string& GetCurrentLevelPath() const { return m_currentLevelPath; }
    void SetCurrentLevelPath(const std::string& path) { m_currentLevelPath = path; }
    const std::string& GetStatusMessage() const { return m_statusMessage; }
    float GetStatusTimer() const { return m_statusTimer; }

    // Hot-swap: signal that the current editor scene should replace the game scene
    bool HasPendingHotSwap() const { return m_hotSwapPending; }
    void ClearHotSwap() { m_hotSwapPending = false; }

    // Camera access for debug
    XMFLOAT3 GetCameraPosition() const { return { m_camX, m_camY, m_camZ }; }
    float GetCameraYaw() const { return m_camYaw; }
    float GetCameraPitch() const { return m_camPitch; }

private:
    bool CreateEditorWindow(HINSTANCE hInstance, int width, int height);
    bool CreateSwapChain();
    bool CreateRenderTargets();

    // ImGui for this window
    bool InitImGui();
    void ShutdownImGui();

    // Input helpers
    void HandleCameraInput(float dt);
    void HandleToolInput(EditorState& state);
    int  PickEntity(const EditorState& state, int mx, int my);
    int  PickGizmoAxis(const EditorState& state, int mx, int my);
    XMFLOAT3 ScreenToWorldPlane(int mx, int my, float planeY = 0.0f);
    XMFLOAT3 ScreenToWorldAxis(int mx, int my, XMFLOAT3 origin, AxisConstraint axis);

    // Rendering sub-passes
    void RenderGrid(ID3D11DeviceContext* ctx);
    void RenderEntities(ID3D11DeviceContext* ctx, const EditorState& state);
    void RenderSelectionHighlight(ID3D11DeviceContext* ctx, const EditorState& state);
    void RenderGizmo(ID3D11DeviceContext* ctx, const EditorState& state);
    void RenderToolbar(ID3D11DeviceContext* ctx, EditorState& state);

    // ImGui outliner panel
    void DrawOutlinerPanel(EditorState& state);
    void DrawToolSection(EditorState& state);
    void DrawLevelSection(EditorState& state);
    void DrawEntitySection(EditorState& state);
    void DrawGridSection();
    void DrawPrefabSection(EditorState& state);
    void DrawPlacementSection();
    void DrawSceneSection(EditorState& state);
    void DrawLightingSection(EditorState& state);
    void DrawSkySection(EditorState& state);
    void DrawShadowsSection(EditorState& state);
    void DrawPostProcessSection(EditorState& state);
    void DrawArtStyleSection(EditorState& state);
    void DrawSSAOSection(EditorState& state);
    void DrawCharacterSection(EditorState& state);
    void DrawPCGSection(EditorState& state);

    // ImGui helpers
    bool BeginSection(const char* icon, const char* label, bool defaultOpen = true);
    void EndSection();
    void PropertyLabel(const char* label);
    void SectionSeparator();

    // Build view/proj matrices
    XMMATRIX GetViewMatrix() const;
    XMMATRIX GetProjectionMatrix() const;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND        m_hwnd     = nullptr;
    HINSTANCE   m_hInst    = nullptr;
    bool        m_open     = false;
    int         m_width    = 1200;
    int         m_height   = 800;

    // Shared from main renderer (we do NOT own these)
    ID3D11Device* m_device = nullptr;

    // Shared rendering resources
    LevelEditorResources m_res;

    // Our own swap chain and render targets
    ComPtr<IDXGISwapChain>          m_swapChain;
    ComPtr<ID3D11Texture2D>         m_backBuffer;
    ComPtr<ID3D11RenderTargetView>  m_rtv;
    ComPtr<ID3D11Texture2D>         m_depthBuffer;
    ComPtr<ID3D11DepthStencilView>  m_dsv;

    // Own ImGui context for this window
    ImGuiContext* m_imguiCtx   = nullptr;
    bool          m_imguiReady = false;
    float         m_panelWidth = 320.0f;   // Outliner panel width

    // Perspective camera (orbiting or free-fly)
    float m_camX     = 0.0f;      // Position
    float m_camY     = 15.0f;
    float m_camZ     = -20.0f;
    float m_camYaw   = 0.0f;      // Radians
    float m_camPitch = 0.7f;      // Radians (looking down)
    float m_camSpeed = 15.0f;
    float m_camFOV   = 60.0f;

    // Mouse state
    bool  m_orbiting       = false;   // Middle-mouse orbit
    bool  m_rightDragging  = false;   // Right-click fly
    bool  m_leftDragging   = false;   // Left-click tool action
    POINT m_lastMouse      = {};
    int   m_mouseX         = 0;
    int   m_mouseY         = 0;
    bool  m_imguiWantsMouse = false;  // Suppress 3D interaction when ImGui wants input

    // Keyboard state for camera
    bool m_keyW = false, m_keyA = false, m_keyS = false, m_keyD = false;
    bool m_keySpace = false, m_keyCtrl = false, m_keyShift = false;

    // Tool system
    LevelEditTool  m_currentTool    = LevelEditTool::Select;
    AxisConstraint m_axisConstraint = AxisConstraint::None;
    int            m_hoveredEntity  = -1;
    int            m_hoveredAxis    = -1;    // 0=X, 1=Y, 2=Z, -1=none
    int            m_activeAxis     = -1;    // Axis grabbed during drag
    XMFLOAT3       m_dragStart      = { 0, 0, 0 };
    XMFLOAT3       m_dragEntityOrigPos = { 0, 0, 0 };
    float          m_dragEntityOrigRot[3] = { 0, 0, 0 };
    float          m_dragEntityOrigScale[3] = { 1, 1, 1 };
    bool           m_isDragging     = false;
    float          m_gizmoLength    = 2.0f;
    float          m_gizmoHitRadius = 0.15f;

    // Rotation/scale snap
    float m_rotationSnap     = 15.0f;   // Degrees
    bool  m_rotationSnapOn   = false;
    float m_scaleSnap        = 0.25f;
    bool  m_scaleSnapOn      = false;
    bool  m_uniformScale     = true;    // Scale all axes equally

    // Grid
    float m_gridSize   = 1.0f;
    int   m_gridExtent = 50;
    bool  m_gridSnap   = true;
    float m_gridSnapSize = 1.0f;

    // Debug renderer (own instance for editor viewport)
    DebugRenderer m_debugRenderer;

    // Level file
    std::string m_currentLevelPath;
    std::string m_levelsDirectory;   // Set to exe_dir/levels/
    bool        m_hotSwapPending = false;
    bool        m_unsavedChanges = false;
    char        m_levelNameBuf[128] = {};  // For save-as input
    std::string m_statusMessage;           // Feedback shown in UI (e.g. "Saved!")
    float       m_statusTimer = 0.0f;      // Countdown for status message display

    // Placement
    MeshType    m_placeMeshType = MeshType::Cube;
    std::string m_placeMeshName;
    float       m_placeColor[4] = { 0.6f, 0.6f, 0.6f, 1.0f };

    // PCG Level Generator
    LevelGenSettings m_pcgSettings;
    int m_pcgMode = 0;  // 0 = Urban, 1 = Warfield (massive)

    // Warfield Generator
    WarfieldSettings m_warfieldSettings;
};

} // namespace WT

