#include "LevelEditorWindow.h"
#include "EditorPanels.h"      // for EditorState
#include "LevelFile.h"
#include "PCG/LevelGenerator.h"
#include "Core/ResourceManager.h"
#include "Util/Log.h"
#include "Util/MathHelpers.h"
#include <windowsx.h>   // GET_X_LPARAM, GET_Y_LPARAM
#include <algorithm>
#include <filesystem>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---- Style constants (match main editor theme) ----
static const ImVec4 kAccent       = ImVec4(0.40f, 0.62f, 1.00f, 1.00f);
static const ImVec4 kAccentDim    = ImVec4(0.30f, 0.48f, 0.78f, 1.00f);
static const ImVec4 kTextDim      = ImVec4(0.55f, 0.58f, 0.62f, 1.00f);
static const ImVec4 kSectionBar   = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
static const ImVec4 kSectionHover = ImVec4(0.20f, 0.23f, 0.30f, 1.00f);
static constexpr float kLabelWidth = 110.0f;

// Axis colors
static const ImVec4 kAxisX = ImVec4(0.90f, 0.20f, 0.20f, 1.00f);
static const ImVec4 kAxisY = ImVec4(0.20f, 0.85f, 0.20f, 1.00f);
static const ImVec4 kAxisZ = ImVec4(0.20f, 0.40f, 0.90f, 1.00f);

namespace WT {

// ==================== Static WndProc ====================
static LevelEditorWindow* g_levelEditor = nullptr;

LRESULT CALLBACK LevelEditorWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_levelEditor)
        return g_levelEditor->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT LevelEditorWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Forward to ImGui first
    if (m_imguiReady) {
        ImGuiContext* prevCtx = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(m_imguiCtx);
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
            ImGui::SetCurrentContext(prevCtx);
            return 0;
        }
        // Check if ImGui wants the mouse — suppress 3D viewport interaction
        m_imguiWantsMouse = ImGui::GetIO().WantCaptureMouse;
        ImGui::SetCurrentContext(prevCtx);
    }

    switch (msg) {
        case WM_CLOSE:
            ShowWindow(m_hwnd, SW_HIDE);
            m_open = false;
            return 0;

        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (w > 0 && h > 0 && m_swapChain) {
                m_width  = w;
                m_height = h;
                m_rtv.Reset();
                m_dsv.Reset();
                m_backBuffer.Reset();
                m_depthBuffer.Reset();
                m_swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTargets();
            }
            return 0;
        }

        // ---- Mouse ----
        case WM_LBUTTONDOWN:
            if (!m_imguiWantsMouse) {
                m_leftDragging = true;
                m_lastMouse.x = GET_X_LPARAM(lParam);
                m_lastMouse.y = GET_Y_LPARAM(lParam);
                SetCapture(hwnd);
            }
            return 0;

        case WM_LBUTTONUP:
            m_leftDragging = false;
            m_isDragging = false;
            m_activeAxis = -1;
            ReleaseCapture();
            return 0;

        case WM_MBUTTONDOWN:
            if (!m_imguiWantsMouse) {
                m_orbiting = true;
                m_lastMouse.x = GET_X_LPARAM(lParam);
                m_lastMouse.y = GET_Y_LPARAM(lParam);
                SetCapture(hwnd);
            }
            return 0;

        case WM_MBUTTONUP:
            m_orbiting = false;
            ReleaseCapture();
            return 0;

        case WM_RBUTTONDOWN:
            if (!m_imguiWantsMouse) {
                m_rightDragging = true;
                m_lastMouse.x = GET_X_LPARAM(lParam);
                m_lastMouse.y = GET_Y_LPARAM(lParam);
                SetCapture(hwnd);
            }
            return 0;

        case WM_RBUTTONUP:
            m_rightDragging = false;
            ReleaseCapture();
            return 0;

        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            float dx = static_cast<float>(mx - m_lastMouse.x);
            float dy = static_cast<float>(my - m_lastMouse.y);
            m_mouseX = mx;
            m_mouseY = my;

            if (m_orbiting || m_rightDragging) {
                m_camYaw   += dx * 0.005f;
                m_camPitch += dy * 0.005f;
                m_camPitch = Clamp(m_camPitch, -HALF_PI + 0.05f, HALF_PI - 0.05f);
            }
            m_lastMouse.x = mx;
            m_lastMouse.y = my;
            return 0;
        }

        case WM_MOUSEWHEEL: {
            if (!m_imguiWantsMouse) {
                short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                float amount = (delta > 0) ? 2.0f : -2.0f;
                if (m_keyShift) amount *= 3.0f;
                float cosP = cosf(m_camPitch);
                m_camX += cosP * sinf(m_camYaw) * amount;
                m_camY += -sinf(m_camPitch) * amount;
                m_camZ += cosP * cosf(m_camYaw) * amount;
            }
            return 0;
        }

        // ---- Keyboard ----
        case WM_KEYDOWN:
            switch (wParam) {
                case 'W': m_keyW = true; break;
                case 'A': m_keyA = true; break;
                case 'S': m_keyS = true; break;
                case 'D': m_keyD = true; break;
                case VK_SPACE:   m_keySpace = true; break;
                case VK_CONTROL: m_keyCtrl = true; break;
                case VK_SHIFT:   m_keyShift = true; break;
                // Tool hotkeys
                case 'Q': m_currentTool = LevelEditTool::Select; m_axisConstraint = AxisConstraint::None; break;
                case 'G': m_currentTool = LevelEditTool::Move;   m_axisConstraint = AxisConstraint::XZ; break;
                case 'R': m_currentTool = LevelEditTool::Rotate; m_axisConstraint = AxisConstraint::Y; break;
                case 'T': m_currentTool = LevelEditTool::Scale;  m_axisConstraint = AxisConstraint::None; break;
                case 'P': m_currentTool = LevelEditTool::Place;  m_axisConstraint = AxisConstraint::None; break;
                case VK_OEM_3: m_gridSnap = !m_gridSnap; break;
                // Axis constraints (while tool is active)
                case 'X': if (m_currentTool == LevelEditTool::Move || m_currentTool == LevelEditTool::Scale || m_currentTool == LevelEditTool::Rotate) {
                              m_axisConstraint = (m_axisConstraint == AxisConstraint::X) ? AxisConstraint::None : AxisConstraint::X;
                          } break;
                case 'Y': if (m_currentTool == LevelEditTool::Move || m_currentTool == LevelEditTool::Scale || m_currentTool == LevelEditTool::Rotate) {
                              m_axisConstraint = (m_axisConstraint == AxisConstraint::Y) ? AxisConstraint::None : AxisConstraint::Y;
                          } break;
                case 'Z': if (m_currentTool == LevelEditTool::Move || m_currentTool == LevelEditTool::Scale || m_currentTool == LevelEditTool::Rotate) {
                              m_axisConstraint = (m_axisConstraint == AxisConstraint::Z) ? AxisConstraint::None : AxisConstraint::Z;
                          } break;
                case VK_ESCAPE:
                    if (m_isDragging) { m_isDragging = false; m_leftDragging = false; m_activeAxis = -1; }
                    break;
            }
            return 0;

        case WM_KEYUP:
            switch (wParam) {
                case 'W': m_keyW = false; break;
                case 'A': m_keyA = false; break;
                case 'S': m_keyS = false; break;
                case 'D': m_keyD = false; break;
                case VK_SPACE:   m_keySpace = false; break;
                case VK_CONTROL: m_keyCtrl = false; break;
                case VK_SHIFT:   m_keyShift = false; break;
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ==================== Init ====================
bool LevelEditorWindow::Init(ID3D11Device* sharedDevice, HINSTANCE hInstance, int width, int height) {
    m_device = sharedDevice;
    m_hInst  = hInstance;
    g_levelEditor = this;

    if (!CreateEditorWindow(hInstance, width, height)) return false;
    if (!CreateSwapChain())  return false;
    if (!CreateRenderTargets()) return false;

    // Init debug renderer for grid/selection lines
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    exeDir = exeDir.substr(0, exeDir.find_last_of(L"\\/") + 1);
    m_debugRenderer.Init(m_device, exeDir + L"shaders/");

    // Levels directory — use source directory (../../levels/ relative to exe)
    // so saved levels persist across clean builds (same pattern as models)
    char exeDirBuf[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, exeDir.c_str(), -1, exeDirBuf, MAX_PATH, nullptr, nullptr);
    std::string exeDirStr(exeDirBuf);
    m_levelsDirectory = exeDirStr + "../../levels/";
    // Normalize the path to resolve the ../.. 
    try {
        auto canonical = std::filesystem::weakly_canonical(m_levelsDirectory);
        m_levelsDirectory = canonical.string() + "\\";
    } catch (...) {
        // Fallback to exe-relative if canonical fails
        m_levelsDirectory = exeDirStr + "levels/";
    }
    std::filesystem::create_directories(m_levelsDirectory);
    LOG_INFO("Levels directory: %s", m_levelsDirectory.c_str());

    // Init ImGui for this window
    if (!InitImGui()) {
        LOG_ERROR("Failed to init ImGui for Level Editor window");
    }

    m_open = false;
    ShowWindow(m_hwnd, SW_HIDE);

    LOG_INFO("Level Editor window created (%dx%d) with ImGui panel", width, height);
    return true;
}

bool LevelEditorWindow::CreateEditorWindow(HINSTANCE hInstance, int width, int height) {
    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor        = LoadCursor(nullptr, IDC_CROSS);
    wc.hbrBackground  = nullptr;
    wc.lpszClassName  = L"WT_LevelEditorClass";
    wc.hIcon          = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm        = LoadIcon(nullptr, IDI_APPLICATION);

    RegisterClassEx(&wc);

    RECT rect = { 0, 0, width, height };
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);

    m_hwnd = CreateWindowEx(
        0, L"WT_LevelEditorClass",
        L"War Times \u2014 Level Editor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!m_hwnd) {
        LOG_ERROR("Failed to create Level Editor window");
        return false;
    }

    m_width  = width;
    m_height = height;
    return true;
}

bool LevelEditorWindow::CreateSwapChain() {
    ComPtr<IDXGIDevice> dxgiDevice;
    m_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);
    ComPtr<IDXGIFactory> factory;
    adapter->GetParent(IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount  = 2;
    sd.BufferDesc.Width  = m_width;
    sd.BufferDesc.Height = m_height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate = { 60, 1 };
    sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_hwnd;
    sd.SampleDesc   = { 1, 0 };
    sd.Windowed     = TRUE;
    sd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    HRESULT hr = factory->CreateSwapChain(m_device, &sd, &m_swapChain);
    if (FAILED(hr)) {
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        hr = factory->CreateSwapChain(m_device, &sd, &m_swapChain);
        if (FAILED(hr)) {
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            sd.BufferCount = 1;
            hr = factory->CreateSwapChain(m_device, &sd, &m_swapChain);
        }
    }

    if (FAILED(hr)) {
        LOG_ERROR("Failed to create Level Editor swap chain");
        return false;
    }
    return true;
}

bool LevelEditorWindow::CreateRenderTargets() {
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&m_backBuffer));
    if (FAILED(hr)) return false;
    hr = m_device->CreateRenderTargetView(m_backBuffer.Get(), nullptr, &m_rtv);
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = m_width; dd.Height = m_height;
    dd.MipLevels = 1; dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc = { 1, 0 };
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = m_device->CreateTexture2D(&dd, nullptr, &m_depthBuffer);
    if (FAILED(hr)) return false;
    hr = m_device->CreateDepthStencilView(m_depthBuffer.Get(), nullptr, &m_dsv);
    return SUCCEEDED(hr);
}

// ==================== ImGui Context ====================
bool LevelEditorWindow::InitImGui() {
    // Save the main context
    ImGuiContext* mainCtx = ImGui::GetCurrentContext();

    // Create a new ImGui context for this window
    m_imguiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Style — match main editor
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 4.0f;
    style.GrabRounding      = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 0.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.WindowPadding     = ImVec2(8, 6);
    style.FramePadding      = ImVec2(6, 3);
    style.ItemSpacing       = ImVec2(6, 3);
    style.ItemInnerSpacing  = ImVec2(4, 4);
    style.IndentSpacing     = 14.0f;
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 8.0f;
    style.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
    style.SeparatorTextBorderSize = 2.0f;

    // Colors — match main editor dark theme
    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]           = ImVec4(0.09f, 0.09f, 0.11f, 0.97f);
    c[ImGuiCol_ChildBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_PopupBg]            = ImVec4(0.10f, 0.10f, 0.13f, 0.98f);
    c[ImGuiCol_Border]             = ImVec4(0.18f, 0.20f, 0.26f, 0.65f);
    c[ImGuiCol_FrameBg]            = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
    c[ImGuiCol_FrameBgHovered]     = ImVec4(0.18f, 0.20f, 0.26f, 1.00f);
    c[ImGuiCol_FrameBgActive]      = ImVec4(0.14f, 0.16f, 0.22f, 1.00f);
    c[ImGuiCol_TitleBg]            = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    c[ImGuiCol_TitleBgActive]      = ImVec4(0.10f, 0.11f, 0.15f, 1.00f);
    c[ImGuiCol_ScrollbarBg]        = ImVec4(0.06f, 0.06f, 0.08f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.22f, 0.24f, 0.30f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.30f, 0.33f, 0.40f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]= ImVec4(0.35f, 0.38f, 0.48f, 1.00f);
    c[ImGuiCol_CheckMark]          = ImVec4(0.40f, 0.62f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]         = ImVec4(0.35f, 0.50f, 0.80f, 1.00f);
    c[ImGuiCol_SliderGrabActive]   = ImVec4(0.45f, 0.62f, 0.95f, 1.00f);
    c[ImGuiCol_Button]             = ImVec4(0.16f, 0.18f, 0.24f, 1.00f);
    c[ImGuiCol_ButtonHovered]      = ImVec4(0.22f, 0.26f, 0.36f, 1.00f);
    c[ImGuiCol_ButtonActive]       = ImVec4(0.13f, 0.15f, 0.20f, 1.00f);
    c[ImGuiCol_Header]             = ImVec4(0.16f, 0.18f, 0.24f, 1.00f);
    c[ImGuiCol_HeaderHovered]      = ImVec4(0.22f, 0.26f, 0.36f, 1.00f);
    c[ImGuiCol_HeaderActive]       = ImVec4(0.18f, 0.22f, 0.32f, 1.00f);
    c[ImGuiCol_Separator]          = ImVec4(0.18f, 0.20f, 0.26f, 0.50f);
    c[ImGuiCol_Tab]                = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
    c[ImGuiCol_TabHovered]         = ImVec4(0.25f, 0.32f, 0.48f, 0.80f);
    c[ImGuiCol_TabSelected]        = ImVec4(0.18f, 0.24f, 0.38f, 1.00f);
    c[ImGuiCol_TextSelectedBg]     = ImVec4(0.25f, 0.40f, 0.65f, 0.35f);

    // Get the device context from the shared device
    ID3D11DeviceContext* ctx = nullptr;
    m_device->GetImmediateContext(&ctx);

    if (!ImGui_ImplWin32_Init(m_hwnd)) {
        LOG_ERROR("ImGui_ImplWin32_Init failed for Level Editor");
        ImGui::SetCurrentContext(mainCtx);
        return false;
    }
    ImGui_ImplDX11_Init(m_device, ctx);

    if (ctx) ctx->Release();

    m_imguiReady = true;

    // Restore main context
    ImGui::SetCurrentContext(mainCtx);

    LOG_INFO("Level Editor ImGui context initialized");
    return true;
}

void LevelEditorWindow::ShutdownImGui() {
    if (!m_imguiReady) return;

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();

    ImGui::SetCurrentContext(prevCtx);
    ImGui::DestroyContext(m_imguiCtx);
    m_imguiCtx = nullptr;
    m_imguiReady = false;

    LOG_INFO("Level Editor ImGui context shutdown");
}

// ==================== Open/Close ====================
void LevelEditorWindow::SetOpen(bool open) {
    m_open = open;
    ShowWindow(m_hwnd, open ? SW_SHOW : SW_HIDE);
    if (open) SetForegroundWindow(m_hwnd);
}

// ==================== Camera ====================
XMMATRIX LevelEditorWindow::GetViewMatrix() const {
    float cosP = cosf(m_camPitch);
    XMFLOAT3 fwd = { cosP * sinf(m_camYaw), -sinf(m_camPitch), cosP * cosf(m_camYaw) };
    XMVECTOR eye    = XMVectorSet(m_camX, m_camY, m_camZ, 0);
    XMVECTOR target = eye + XMLoadFloat3(&fwd);
    return XMMatrixLookAtLH(eye, target, XMVectorSet(0, 1, 0, 0));
}

XMMATRIX LevelEditorWindow::GetProjectionMatrix() const {
    float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    return XMMatrixPerspectiveFovLH(XMConvertToRadians(m_camFOV), aspect, 0.1f, 500.0f);
}

void LevelEditorWindow::HandleCameraInput(float dt) {
    if (!m_rightDragging && !m_orbiting) return;

    float speed = m_camSpeed * dt;
    if (m_keyShift) speed *= 3.0f;

    float cosP = cosf(m_camPitch);
    XMFLOAT3 fwd = { cosP * sinf(m_camYaw), 0, cosP * cosf(m_camYaw) };
    float len = sqrtf(fwd.x * fwd.x + fwd.z * fwd.z);
    if (len > 0.001f) { fwd.x /= len; fwd.z /= len; }
    XMFLOAT3 right = { cosf(m_camYaw), 0, -sinf(m_camYaw) };

    if (m_keyW) { m_camX += fwd.x * speed; m_camZ += fwd.z * speed; }
    if (m_keyS) { m_camX -= fwd.x * speed; m_camZ -= fwd.z * speed; }
    if (m_keyA) { m_camX -= right.x * speed; m_camZ -= right.z * speed; }
    if (m_keyD) { m_camX += right.x * speed; m_camZ += right.z * speed; }
    if (m_keySpace) m_camY += speed;
    if (m_keyCtrl)  m_camY -= speed;
}

// ==================== Screen-to-World ====================
XMFLOAT3 LevelEditorWindow::ScreenToWorldPlane(int mx, int my, float planeY) {
    XMMATRIX invVP = XMMatrixInverse(nullptr, GetViewMatrix() * GetProjectionMatrix());
    float ndcX = (2.0f * mx / m_width) - 1.0f;
    float ndcY = 1.0f - (2.0f * my / m_height);

    XMVECTOR nearPt = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0, 1), invVP);
    XMVECTOR farPt  = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1, 1), invVP);
    XMVECTOR rayDir = XMVector3Normalize(farPt - nearPt);

    XMFLOAT3 o, d;
    XMStoreFloat3(&o, nearPt);
    XMStoreFloat3(&d, rayDir);

    if (fabsf(d.y) < 0.0001f) return { o.x, planeY, o.z };
    float t = (planeY - o.y) / d.y;
    return { o.x + d.x * t, planeY, o.z + d.z * t };
}

// Project screen point onto an axis line through origin for constrained movement
XMFLOAT3 LevelEditorWindow::ScreenToWorldAxis(int mx, int my, XMFLOAT3 origin, AxisConstraint axis) {
    XMMATRIX invVP = XMMatrixInverse(nullptr, GetViewMatrix() * GetProjectionMatrix());
    float ndcX = (2.0f * mx / m_width) - 1.0f;
    float ndcY = 1.0f - (2.0f * my / m_height);

    XMVECTOR nearPt = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0, 1), invVP);
    XMVECTOR farPt  = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1, 1), invVP);
    XMVECTOR rayO   = nearPt;
    XMVECTOR rayD   = XMVector3Normalize(farPt - nearPt);

    // Axis direction
    XMVECTOR axisDir;
    switch (axis) {
        case AxisConstraint::X:  axisDir = XMVectorSet(1, 0, 0, 0); break;
        case AxisConstraint::Y:  axisDir = XMVectorSet(0, 1, 0, 0); break;
        case AxisConstraint::Z:  axisDir = XMVectorSet(0, 0, 1, 0); break;
        default:                 return ScreenToWorldPlane(mx, my, origin.y);
    }

    XMVECTOR lineO = XMLoadFloat3(&origin);

    // Closest point on axis line to the ray (parametric)
    XMVECTOR w0 = lineO - rayO;
    float a = XMVectorGetX(XMVector3Dot(axisDir, axisDir));
    float b = XMVectorGetX(XMVector3Dot(axisDir, rayD));
    float c2 = XMVectorGetX(XMVector3Dot(rayD, rayD));
    float d1 = XMVectorGetX(XMVector3Dot(axisDir, w0));
    float e1 = XMVectorGetX(XMVector3Dot(rayD, w0));

    float denom = a * c2 - b * b;
    float sc = (fabsf(denom) > 0.0001f) ? (b * e1 - c2 * d1) / denom : 0.0f;

    XMVECTOR pt = lineO + axisDir * sc;
    XMFLOAT3 result;
    XMStoreFloat3(&result, pt);
    return result;
}

// ==================== Picking ====================
int LevelEditorWindow::PickEntity(const EditorState& state, int mx, int my) {
    XMMATRIX invVP = XMMatrixInverse(nullptr, GetViewMatrix() * GetProjectionMatrix());
    float ndcX = (2.0f * mx / m_width) - 1.0f;
    float ndcY = 1.0f - (2.0f * my / m_height);

    XMVECTOR nearPt = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0, 1), invVP);
    XMVECTOR farPt  = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1, 1), invVP);
    XMFLOAT3 origin, dir;
    XMStoreFloat3(&origin, nearPt);
    XMStoreFloat3(&dir, XMVector3Normalize(farPt - nearPt));

    int closest = -1;
    float closestDist = 999999.0f;

    for (int i = 0; i < state.scene.GetEntityCount(); i++) {
        const auto& e = state.scene.GetEntity(i);
        if (!e.visible) continue;

        // Use mesh bounds for custom meshes, entity scale for cubes
        XMFLOAT3 half = { e.scale[0] * 0.5f, e.scale[1] * 0.5f, e.scale[2] * 0.5f };
        XMFLOAT3 center = { e.position[0], e.position[1], e.position[2] };
        if (e.meshType == MeshType::Custom && !e.meshName.empty()) {
            Mesh* mesh = ResourceManager::Get().GetMesh(e.meshName);
            if (mesh && mesh->HasBounds()) {
                auto bc = mesh->GetBoundsCenter();
                auto bh = mesh->GetBoundsHalfExtent();
                center = { e.position[0] + bc.x * e.scale[0],
                           e.position[1] + bc.y * e.scale[1],
                           e.position[2] + bc.z * e.scale[2] };
                half = { bh.x * e.scale[0], bh.y * e.scale[1], bh.z * e.scale[2] };
            }
        }
        XMFLOAT3 bmin = { center.x - half.x, center.y - half.y, center.z - half.z };
        XMFLOAT3 bmax = { center.x + half.x, center.y + half.y, center.z + half.z };

        float tmin = -999999.0f, tmax = 999999.0f;
        auto slab = [&](float rO, float rD, float mn, float mx2) -> bool {
            if (fabsf(rD) < 0.00001f) return (rO >= mn && rO <= mx2);
            float t1 = (mn - rO) / rD, t2 = (mx2 - rO) / rD;
            if (t1 > t2) std::swap(t1, t2);
            tmin = (std::max)(tmin, t1);
            tmax = (std::min)(tmax, t2);
            return tmin <= tmax && tmax >= 0;
        };
        if (!slab(origin.x, dir.x, bmin.x, bmax.x)) continue;
        if (!slab(origin.y, dir.y, bmin.y, bmax.y)) continue;
        if (!slab(origin.z, dir.z, bmin.z, bmax.z)) continue;

        float dist = (tmin >= 0) ? tmin : tmax;
        if (dist < closestDist) { closestDist = dist; closest = i; }
    }
    return closest;
}

// Pick which gizmo axis the mouse is near (returns 0=X, 1=Y, 2=Z, -1=none)
int LevelEditorWindow::PickGizmoAxis(const EditorState& state, int mx, int my) {
    if (state.selectedEntity < 0 || state.selectedEntity >= state.scene.GetEntityCount())
        return -1;

    const auto& e = state.scene.GetEntity(state.selectedEntity);
    XMFLOAT3 pos = { e.position[0], e.position[1], e.position[2] };

    // Offset by mesh bounds center for custom meshes
    if (e.meshType == MeshType::Custom) {
        auto mesh = ResourceManager::Get().GetMesh(e.meshName);
        if (mesh && mesh->HasBounds()) {
            auto bc = mesh->GetBoundsCenter();
            pos.x += bc.x * e.scale[0];
            pos.y += bc.y * e.scale[1];
            pos.z += bc.z * e.scale[2];
        }
    }

    XMMATRIX vp = GetViewMatrix() * GetProjectionMatrix();

    // Project entity origin and axis endpoints to screen space
    auto projectToScreen = [&](XMFLOAT3 worldPt) -> XMFLOAT2 {
        XMVECTOR p = XMVector3TransformCoord(XMLoadFloat3(&worldPt), vp);
        XMFLOAT3 ndc;
        XMStoreFloat3(&ndc, p);
        return { (ndc.x * 0.5f + 0.5f) * m_width, (0.5f - ndc.y * 0.5f) * m_height };
    };

    XMFLOAT2 screenOrigin = projectToScreen(pos);
    XMFLOAT3 axisEnds[3] = {
        { pos.x + m_gizmoLength, pos.y, pos.z },
        { pos.x, pos.y + m_gizmoLength, pos.z },
        { pos.x, pos.y, pos.z + m_gizmoLength }
    };

    float hitDist = 12.0f;  // pixels
    int bestAxis = -1;
    float bestDist = hitDist;

    for (int a = 0; a < 3; a++) {
        XMFLOAT2 screenEnd = projectToScreen(axisEnds[a]);

        // Distance from point to line segment
        float dx = screenEnd.x - screenOrigin.x;
        float dy = screenEnd.y - screenOrigin.y;
        float segLen2 = dx * dx + dy * dy;
        if (segLen2 < 1.0f) continue;

        float t = ((mx - screenOrigin.x) * dx + (my - screenOrigin.y) * dy) / segLen2;
        t = Clamp(t, 0.0f, 1.0f);

        float px = screenOrigin.x + t * dx - mx;
        float py = screenOrigin.y + t * dy - my;
        float d = sqrtf(px * px + py * py);

        if (d < bestDist) {
            bestDist = d;
            bestAxis = a;
        }
    }
    return bestAxis;
}

// ==================== Tool Input ====================
void LevelEditorWindow::HandleToolInput(EditorState& state) {
    if (m_imguiWantsMouse) return;
    if (!m_leftDragging) return;

    switch (m_currentTool) {
        case LevelEditTool::Select: {
            if (!m_isDragging) {
                state.selectedEntity = PickEntity(state, m_mouseX, m_mouseY);
                m_isDragging = true;
            }
            break;
        }
        case LevelEditTool::Move: {
            if (state.selectedEntity >= 0 && state.selectedEntity < state.scene.GetEntityCount()) {
                auto& e = state.scene.GetEntity(state.selectedEntity);

                if (!m_isDragging) {
                    // Check if clicking on a gizmo axis handle
                    int gizmoHit = PickGizmoAxis(state, m_mouseX, m_mouseY);
                    if (gizmoHit >= 0) {
                        m_activeAxis = gizmoHit;
                        static const AxisConstraint axisMap[3] = { AxisConstraint::X, AxisConstraint::Y, AxisConstraint::Z };
                        m_axisConstraint = axisMap[gizmoHit];
                    }

                    m_dragEntityOrigPos = { e.position[0], e.position[1], e.position[2] };

                    if (m_axisConstraint == AxisConstraint::X || m_axisConstraint == AxisConstraint::Y || m_axisConstraint == AxisConstraint::Z) {
                        m_dragStart = ScreenToWorldAxis(m_mouseX, m_mouseY, m_dragEntityOrigPos, m_axisConstraint);
                    } else {
                        m_dragStart = ScreenToWorldPlane(m_mouseX, m_mouseY, e.position[1]);
                    }
                    m_isDragging = true;
                }

                XMFLOAT3 wp;
                if (m_axisConstraint == AxisConstraint::X || m_axisConstraint == AxisConstraint::Y || m_axisConstraint == AxisConstraint::Z) {
                    wp = ScreenToWorldAxis(m_mouseX, m_mouseY, m_dragEntityOrigPos, m_axisConstraint);
                } else {
                    wp = ScreenToWorldPlane(m_mouseX, m_mouseY, e.position[1]);
                }

                float deltaX = wp.x - m_dragStart.x;
                float deltaY = wp.y - m_dragStart.y;
                float deltaZ = wp.z - m_dragStart.z;

                // Fine control
                if (m_keyCtrl) { deltaX *= 0.1f; deltaY *= 0.1f; deltaZ *= 0.1f; }

                float nx = m_dragEntityOrigPos.x;
                float ny = m_dragEntityOrigPos.y;
                float nz = m_dragEntityOrigPos.z;

                switch (m_axisConstraint) {
                    case AxisConstraint::X:  nx += deltaX; break;
                    case AxisConstraint::Y:  ny += deltaY; break;
                    case AxisConstraint::Z:  nz += deltaZ; break;
                    case AxisConstraint::XZ: nx += deltaX; nz += deltaZ; break;
                    default:                 nx += deltaX; ny += deltaY; nz += deltaZ; break;
                }

                if (m_gridSnap) {
                    if (m_axisConstraint != AxisConstraint::Y) {
                        nx = roundf(nx / m_gridSnapSize) * m_gridSnapSize;
                        nz = roundf(nz / m_gridSnapSize) * m_gridSnapSize;
                    }
                    if (m_axisConstraint == AxisConstraint::Y) {
                        ny = roundf(ny / m_gridSnapSize) * m_gridSnapSize;
                    }
                }

                e.position[0] = nx;
                e.position[1] = ny;
                e.position[2] = nz;
                m_unsavedChanges = true;
            }
            break;
        }
        case LevelEditTool::Rotate: {
            if (state.selectedEntity >= 0 && state.selectedEntity < state.scene.GetEntityCount()) {
                auto& e = state.scene.GetEntity(state.selectedEntity);
                if (!m_isDragging) {
                    m_dragStart = { (float)m_mouseX, (float)m_mouseY, 0 };
                    m_dragEntityOrigRot[0] = e.rotation[0];
                    m_dragEntityOrigRot[1] = e.rotation[1];
                    m_dragEntityOrigRot[2] = e.rotation[2];

                    int gizmoHit = PickGizmoAxis(state, m_mouseX, m_mouseY);
                    if (gizmoHit >= 0) {
                        m_activeAxis = gizmoHit;
                        static const AxisConstraint axisMap[3] = { AxisConstraint::X, AxisConstraint::Y, AxisConstraint::Z };
                        m_axisConstraint = axisMap[gizmoHit];
                    }
                    m_isDragging = true;
                }

                float mouseDelta = (float)m_mouseX - m_dragStart.x;
                float rotDelta = mouseDelta * 0.5f;

                if (m_rotationSnapOn) {
                    rotDelta = roundf(rotDelta / m_rotationSnap) * m_rotationSnap;
                }
                if (m_keyCtrl) rotDelta *= 0.1f;

                switch (m_axisConstraint) {
                    case AxisConstraint::X:
                        e.rotation[0] = m_dragEntityOrigRot[0] + rotDelta;
                        break;
                    case AxisConstraint::Z:
                        e.rotation[2] = m_dragEntityOrigRot[2] + rotDelta;
                        break;
                    case AxisConstraint::Y:
                    default:
                        e.rotation[1] = m_dragEntityOrigRot[1] + rotDelta;
                        break;
                }
                m_unsavedChanges = true;
            }
            break;
        }
        case LevelEditTool::Scale: {
            if (state.selectedEntity >= 0 && state.selectedEntity < state.scene.GetEntityCount()) {
                auto& e = state.scene.GetEntity(state.selectedEntity);
                if (!m_isDragging) {
                    m_dragStart = { (float)m_mouseX, (float)m_mouseY, 0 };
                    memcpy(m_dragEntityOrigScale, e.scale, sizeof(float) * 3);

                    int gizmoHit = PickGizmoAxis(state, m_mouseX, m_mouseY);
                    if (gizmoHit >= 0) {
                        m_activeAxis = gizmoHit;
                        static const AxisConstraint axisMap[3] = { AxisConstraint::X, AxisConstraint::Y, AxisConstraint::Z };
                        m_axisConstraint = axisMap[gizmoHit];
                    }
                    m_isDragging = true;
                }

                float rawFactor = 1.0f + (m_dragStart.y - (float)m_mouseY) * 0.005f;
                if (rawFactor < 0.05f) rawFactor = 0.05f;
                if (m_keyCtrl) rawFactor = 1.0f + (rawFactor - 1.0f) * 0.2f;

                float factor = rawFactor;
                if (m_scaleSnapOn) {
                    factor = roundf(rawFactor / m_scaleSnap) * m_scaleSnap;
                    if (factor < m_scaleSnap) factor = m_scaleSnap;
                }

                if (m_uniformScale && m_axisConstraint == AxisConstraint::None) {
                    e.scale[0] = m_dragEntityOrigScale[0] * factor;
                    e.scale[1] = m_dragEntityOrigScale[1] * factor;
                    e.scale[2] = m_dragEntityOrigScale[2] * factor;
                } else {
                    switch (m_axisConstraint) {
                        case AxisConstraint::X:
                            e.scale[0] = m_dragEntityOrigScale[0] * factor;
                            e.scale[1] = m_dragEntityOrigScale[1];
                            e.scale[2] = m_dragEntityOrigScale[2];
                            break;
                        case AxisConstraint::Y:
                            e.scale[0] = m_dragEntityOrigScale[0];
                            e.scale[1] = m_dragEntityOrigScale[1] * factor;
                            e.scale[2] = m_dragEntityOrigScale[2];
                            break;
                        case AxisConstraint::Z:
                            e.scale[0] = m_dragEntityOrigScale[0];
                            e.scale[1] = m_dragEntityOrigScale[1];
                            e.scale[2] = m_dragEntityOrigScale[2] * factor;
                            break;
                        default:
                            e.scale[0] = m_dragEntityOrigScale[0] * factor;
                            e.scale[1] = m_dragEntityOrigScale[1] * factor;
                            e.scale[2] = m_dragEntityOrigScale[2] * factor;
                            break;
                    }
                }
                m_unsavedChanges = true;
            }
            break;
        }
        case LevelEditTool::Place: {
            if (!m_isDragging) {
                XMFLOAT3 wp = ScreenToWorldPlane(m_mouseX, m_mouseY, 0.0f);
                if (m_gridSnap) {
                    wp.x = roundf(wp.x / m_gridSnapSize) * m_gridSnapSize;
                    wp.z = roundf(wp.z / m_gridSnapSize) * m_gridSnapSize;
                }
                int idx = state.scene.AddEntity("", m_placeMeshType);
                auto& e = state.scene.GetEntity(idx);
                if (m_placeMeshType == MeshType::Custom) {
                    e.meshName = m_placeMeshName;
                    e.name = m_placeMeshName + "_" + std::to_string(idx);
                }
                e.position[0] = wp.x;
                e.position[1] = 0.5f;
                e.position[2] = wp.z;
                memcpy(e.color, m_placeColor, sizeof(float) * 4);
                state.selectedEntity = idx;
                m_isDragging = true;
                m_unsavedChanges = true;
            }
            break;
        }
        default: break;
    }
}

// ==================== Update ====================
void LevelEditorWindow::Update(float dt, EditorState& state) {
    if (!m_open) return;
    HandleCameraInput(dt);
    HandleToolInput(state);

    if (!m_leftDragging && !m_imguiWantsMouse) {
        m_hoveredAxis = PickGizmoAxis(state, m_mouseX, m_mouseY);
        if (m_currentTool == LevelEditTool::Select || m_hoveredAxis < 0)
            m_hoveredEntity = PickEntity(state, m_mouseX, m_mouseY);
    }

    // Delete key
    if (GetAsyncKeyState(VK_DELETE) & 0x8000) {
        if (state.selectedEntity >= 0 && state.selectedEntity < state.scene.GetEntityCount()) {
            state.scene.RemoveEntity(state.selectedEntity);
            if (state.selectedEntity >= state.scene.GetEntityCount())
                state.selectedEntity = state.scene.GetEntityCount() - 1;
            m_unsavedChanges = true;
        }
    }
}

// ==================== Render ====================
void LevelEditorWindow::Render(ID3D11DeviceContext* ctx, EditorState& state) {
    if (!m_open || !m_swapChain) return;

    // ---- Bind our render target ----
    float cc[4] = { 0.12f, 0.13f, 0.16f, 1.0f };
    ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), m_dsv.Get());
    ctx->ClearRenderTargetView(m_rtv.Get(), cc);
    ctx->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)m_width; vp.Height = (float)m_height; vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);

    // ---- Per-frame CB ----
    if (m_res.cbPerFrame) {
        XMMATRIX v = GetViewMatrix(), p = GetProjectionMatrix(), vpp = v * p;
        CBPerFrame f = {};
        XMStoreFloat4x4(&f.View, XMMatrixTranspose(v));
        XMStoreFloat4x4(&f.Projection, XMMatrixTranspose(p));
        XMStoreFloat4x4(&f.ViewProjection, XMMatrixTranspose(vpp));
        XMStoreFloat4x4(&f.InvViewProjection, XMMatrixTranspose(XMMatrixInverse(nullptr, vpp)));
        f.CameraPosition = { m_camX, m_camY, m_camZ };
        f.ScreenSize = { (float)m_width, (float)m_height };
        f.NearZ = 0.1f; f.FarZ = 500.0f;
        m_res.cbPerFrame->Update(ctx, f);
        m_res.cbPerFrame->BindBoth(ctx, 0);
    }

    // ---- Lighting CB (simple editor light) ----
    if (m_res.cbLighting) {
        CBLighting L = {};
        L.SunDirection = { 0.4f, -0.7f, 0.5f };
        L.SunIntensity = 1.2f;
        L.SunColor = { 1, 0.95f, 0.9f };
        L.AmbientColor = { 0.25f, 0.28f, 0.32f };
        L.AmbientIntensity = 1.0f;
        L.FogDensity = 0; L.FogStart = 500; L.FogEnd = 500;
        m_res.cbLighting->Update(ctx, L);
        m_res.cbLighting->BindBoth(ctx, 2);
    }

    // ---- Grid ----
    RenderGrid(ctx);

    // ---- Entities ----
    RenderEntities(ctx, state);

    // ---- Selection + Gizmo ----
    RenderSelectionHighlight(ctx, state);
    RenderGizmo(ctx, state);

    // ---- Debug flush ----
    m_debugRenderer.Flush(ctx);

    // ---- ImGui Outliner Panel ----
    if (m_imguiReady) {
        ImGuiContext* prevCtx = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(m_imguiCtx);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);

        DrawOutlinerPanel(state);

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        ImGui::SetCurrentContext(prevCtx);
    }

    // ---- Title bar info ----
    RenderToolbar(ctx, state);

    m_swapChain->Present(1, 0);
}

// ==================== Render Sub-passes ====================

void LevelEditorWindow::RenderGrid(ID3D11DeviceContext* ctx) {
    (void)ctx;
    float ext = (float)m_gridExtent;
    XMFLOAT4 gc = { 0.3f, 0.3f, 0.35f, 1.0f };
    XMFLOAT4 oc = { 0.5f, 0.5f, 0.55f, 1.0f };

    for (float i = -ext; i <= ext; i += m_gridSize) {
        XMFLOAT4 c = (fabsf(i) < 0.001f) ? oc : gc;
        m_debugRenderer.DrawLine({ i, 0, -ext }, { i, 0, ext }, c);
        m_debugRenderer.DrawLine({ -ext, 0, i }, { ext, 0, i }, c);
    }
    // Axis
    m_debugRenderer.DrawLine({ 0, 0.01f, 0 }, { ext * 0.3f, 0.01f, 0 }, { 0.8f, 0.2f, 0.2f, 1 });
    m_debugRenderer.DrawLine({ 0, 0.01f, 0 }, { 0, 0.01f, ext * 0.3f }, { 0.2f, 0.2f, 0.8f, 1 });
    m_debugRenderer.DrawLine({ 0, 0, 0 }, { 0, ext * 0.15f, 0 }, { 0.2f, 0.8f, 0.2f, 1 });
}

void LevelEditorWindow::RenderEntities(ID3D11DeviceContext* ctx, const EditorState& state) {
    if (!m_res.voxelShader || !m_res.cubeMesh || !m_res.cbPerObject) return;

    m_res.voxelShader->Bind(ctx);
    Texture* wt = ResourceManager::Get().GetTexture("_white");
    CBPerObject obj = {};

    for (int i = 0; i < state.scene.GetEntityCount(); i++) {
        const auto& e = state.scene.GetEntity(i);
        if (!e.visible) continue;

        XMMATRIX w = e.GetWorldMatrix();
        XMStoreFloat4x4(&obj.World, XMMatrixTranspose(w));
        XMStoreFloat4x4(&obj.WorldInvTranspose, XMMatrixInverse(nullptr, w));
        obj.ObjectColor = { e.color[0], e.color[1], e.color[2], e.color[3] };
        m_res.cbPerObject->Update(ctx, obj);
        m_res.cbPerObject->BindBoth(ctx, 1);

        if (e.meshType == MeshType::Cube) {
            Texture* ct = nullptr;
            if (!e.textureName.empty())
                ct = ResourceManager::Get().GetTexture(e.textureName);
            (ct ? ct : wt)->Bind(ctx, 1);
            m_res.cubeMesh->Draw(ctx);
        } else if (e.meshType == MeshType::Custom) {
            Texture* mt = nullptr;
            if (!e.textureName.empty())
                mt = ResourceManager::Get().GetTexture(e.textureName);
            if (!mt)
                mt = ResourceManager::Get().GetTexture(e.meshName);
            (mt ? mt : wt)->Bind(ctx, 1);
            Mesh* cm = ResourceManager::Get().GetMesh(e.meshName);
            if (cm) cm->Draw(ctx);
        }
    }

    obj.ObjectColor = { 0, 0, 0, 0 };
    m_res.cbPerObject->Update(ctx, obj);
    m_res.cbPerObject->BindBoth(ctx, 1);

    // Ground
    if (m_res.groundShader && m_res.groundMesh) {
        m_res.groundShader->Bind(ctx);
        if (wt) wt->Bind(ctx, 1);
        XMMATRIX gw = XMMatrixTranslation(0, -0.01f, 0);
        XMStoreFloat4x4(&obj.World, XMMatrixTranspose(gw));
        XMStoreFloat4x4(&obj.WorldInvTranspose, XMMatrixInverse(nullptr, gw));
        m_res.cbPerObject->Update(ctx, obj);
        m_res.cbPerObject->BindBoth(ctx, 1);
        m_res.groundMesh->Draw(ctx);
    }
}

void LevelEditorWindow::RenderSelectionHighlight(ID3D11DeviceContext* ctx, const EditorState& state) {
    (void)ctx;

    // Hover
    if (m_hoveredEntity >= 0 && m_hoveredEntity < state.scene.GetEntityCount()
        && m_hoveredEntity != state.selectedEntity) {
        const auto& e = state.scene.GetEntity(m_hoveredEntity);
        m_debugRenderer.DrawBox(
            { e.position[0], e.position[1], e.position[2] },
            { e.scale[0] * 0.52f, e.scale[1] * 0.52f, e.scale[2] * 0.52f },
            { 0.5f, 0.7f, 1.0f, 0.6f });
    }

    // Selected
    if (state.selectedEntity >= 0 && state.selectedEntity < state.scene.GetEntityCount()) {
        const auto& e = state.scene.GetEntity(state.selectedEntity);
        XMFLOAT3 c = { e.position[0], e.position[1], e.position[2] };
        m_debugRenderer.DrawBox(c,
            { e.scale[0] * 0.52f, e.scale[1] * 0.52f, e.scale[2] * 0.52f },
            { 1.0f, 0.8f, 0.2f, 1.0f });
    }

    // Placement preview
    if (m_currentTool == LevelEditTool::Place && !m_leftDragging) {
        XMFLOAT3 wp = ScreenToWorldPlane(m_mouseX, m_mouseY, 0.0f);
        if (m_gridSnap) {
            wp.x = roundf(wp.x / m_gridSnapSize) * m_gridSnapSize;
            wp.z = roundf(wp.z / m_gridSnapSize) * m_gridSnapSize;
        }
        m_debugRenderer.DrawBox({ wp.x, 0.5f, wp.z }, { 0.52f, 0.52f, 0.52f },
                                { 0.3f, 0.9f, 0.3f, 0.5f });
    }
}

// ==================== Gizmo Rendering ====================
void LevelEditorWindow::RenderGizmo(ID3D11DeviceContext* ctx, const EditorState& state) {
    (void)ctx;
    if (state.selectedEntity < 0 || state.selectedEntity >= state.scene.GetEntityCount())
        return;

    const auto& e = state.scene.GetEntity(state.selectedEntity);
    XMFLOAT3 c = { e.position[0], e.position[1], e.position[2] };

    // Offset by mesh bounds center for custom meshes
    if (e.meshType == MeshType::Custom) {
        auto mesh = ResourceManager::Get().GetMesh(e.meshName);
        if (mesh && mesh->HasBounds()) {
            auto bc = mesh->GetBoundsCenter();
            c.x += bc.x * e.scale[0];
            c.y += bc.y * e.scale[1];
            c.z += bc.z * e.scale[2];
        }
    }

    float gl = m_gizmoLength;
    float arrowSize = gl * 0.12f;

    bool highlightX = (m_hoveredAxis == 0 || m_activeAxis == 0 || m_axisConstraint == AxisConstraint::X);
    bool highlightY = (m_hoveredAxis == 1 || m_activeAxis == 1 || m_axisConstraint == AxisConstraint::Y);
    bool highlightZ = (m_hoveredAxis == 2 || m_activeAxis == 2 || m_axisConstraint == AxisConstraint::Z);

    // X axis (Red)
    XMFLOAT4 xc = highlightX ? XMFLOAT4(1.0f, 0.5f, 0.2f, 1.0f) : XMFLOAT4(0.9f, 0.15f, 0.15f, 1.0f);
    m_debugRenderer.DrawLine(c, { c.x + gl, c.y, c.z }, xc);
    m_debugRenderer.DrawLine({ c.x + gl, c.y, c.z }, { c.x + gl - arrowSize, c.y + arrowSize * 0.5f, c.z }, xc);
    m_debugRenderer.DrawLine({ c.x + gl, c.y, c.z }, { c.x + gl - arrowSize, c.y - arrowSize * 0.5f, c.z }, xc);

    // Y axis (Green)
    XMFLOAT4 yc = highlightY ? XMFLOAT4(0.5f, 1.0f, 0.2f, 1.0f) : XMFLOAT4(0.15f, 0.85f, 0.15f, 1.0f);
    m_debugRenderer.DrawLine(c, { c.x, c.y + gl, c.z }, yc);
    m_debugRenderer.DrawLine({ c.x, c.y + gl, c.z }, { c.x + arrowSize * 0.5f, c.y + gl - arrowSize, c.z }, yc);
    m_debugRenderer.DrawLine({ c.x, c.y + gl, c.z }, { c.x - arrowSize * 0.5f, c.y + gl - arrowSize, c.z }, yc);

    // Z axis (Blue)
    XMFLOAT4 zc = highlightZ ? XMFLOAT4(0.2f, 0.5f, 1.0f, 1.0f) : XMFLOAT4(0.15f, 0.3f, 0.9f, 1.0f);
    m_debugRenderer.DrawLine(c, { c.x, c.y, c.z + gl }, zc);
    m_debugRenderer.DrawLine({ c.x, c.y, c.z + gl }, { c.x, c.y + arrowSize * 0.5f, c.z + gl - arrowSize }, zc);
    m_debugRenderer.DrawLine({ c.x, c.y, c.z + gl }, { c.x, c.y - arrowSize * 0.5f, c.z + gl - arrowSize }, zc);

    // Scale tool: cubes at axis tips
    if (m_currentTool == LevelEditTool::Scale) {
        float bs = arrowSize * 0.8f;
        m_debugRenderer.DrawBox({ c.x + gl, c.y, c.z }, { bs, bs, bs }, xc);
        m_debugRenderer.DrawBox({ c.x, c.y + gl, c.z }, { bs, bs, bs }, yc);
        m_debugRenderer.DrawBox({ c.x, c.y, c.z + gl }, { bs, bs, bs }, zc);
    }

    // Rotate tool: rotation rings
    if (m_currentTool == LevelEditTool::Rotate) {
        int segments = 32;
        float radius = gl * 0.8f;
        for (int i = 0; i < segments; i++) {
            float a0 = (float)i / segments * 6.283185f;
            float a1 = (float)(i + 1) / segments * 6.283185f;

            // Y rotation ring (XZ plane)
            if (m_axisConstraint == AxisConstraint::Y || m_axisConstraint == AxisConstraint::None) {
                m_debugRenderer.DrawLine(
                    { c.x + cosf(a0) * radius, c.y, c.z + sinf(a0) * radius },
                    { c.x + cosf(a1) * radius, c.y, c.z + sinf(a1) * radius },
                    highlightY ? XMFLOAT4(0.5f, 1.0f, 0.2f, 0.6f) : XMFLOAT4(0.15f, 0.7f, 0.15f, 0.4f));
            }
            if (m_axisConstraint == AxisConstraint::X) {
                m_debugRenderer.DrawLine(
                    { c.x, c.y + cosf(a0) * radius, c.z + sinf(a0) * radius },
                    { c.x, c.y + cosf(a1) * radius, c.z + sinf(a1) * radius },
                    highlightX ? XMFLOAT4(1.0f, 0.5f, 0.2f, 0.6f) : XMFLOAT4(0.7f, 0.15f, 0.15f, 0.4f));
            }
            if (m_axisConstraint == AxisConstraint::Z) {
                m_debugRenderer.DrawLine(
                    { c.x + cosf(a0) * radius, c.y + sinf(a0) * radius, c.z },
                    { c.x + cosf(a1) * radius, c.y + sinf(a1) * radius, c.z },
                    highlightZ ? XMFLOAT4(0.2f, 0.5f, 1.0f, 0.6f) : XMFLOAT4(0.15f, 0.3f, 0.7f, 0.4f));
            }
        }
    }

    // Constraint guide line during drag
    if (m_isDragging && (m_axisConstraint == AxisConstraint::X || m_axisConstraint == AxisConstraint::Y || m_axisConstraint == AxisConstraint::Z)) {
        float lineExt = 50.0f;
        XMFLOAT4 lc = { 0.5f, 0.5f, 0.5f, 0.3f };
        switch (m_axisConstraint) {
            case AxisConstraint::X:
                lc = { 0.8f, 0.2f, 0.2f, 0.3f };
                m_debugRenderer.DrawLine({ c.x - lineExt, c.y, c.z }, { c.x + lineExt, c.y, c.z }, lc);
                break;
            case AxisConstraint::Y:
                lc = { 0.2f, 0.8f, 0.2f, 0.3f };
                m_debugRenderer.DrawLine({ c.x, c.y - lineExt, c.z }, { c.x, c.y + lineExt, c.z }, lc);
                break;
            case AxisConstraint::Z:
                lc = { 0.2f, 0.2f, 0.8f, 0.3f };
                m_debugRenderer.DrawLine({ c.x, c.y, c.z - lineExt }, { c.x, c.y, c.z + lineExt }, lc);
                break;
            default: break;
        }
    }
}

void LevelEditorWindow::RenderToolbar(ID3D11DeviceContext* ctx, EditorState& state) {
    (void)ctx;
    std::wstring title = L"Level Editor [";
    const char* tn = LevelEditToolName(m_currentTool);
    title += std::wstring(tn, tn + strlen(tn)) + L"]";

    if (m_axisConstraint != AxisConstraint::None) {
        const char* an = AxisConstraintName(m_axisConstraint);
        title += L" ";
        for (const char* p = an; *p; ++p) title += static_cast<wchar_t>(*p);
    }

    if (!m_currentLevelPath.empty()) {
        std::string n = LevelFile::GetLevelName(m_currentLevelPath);
        title += L" - ";
        for (char c : n) title += static_cast<wchar_t>(c);
    } else {
        title += L" - Unsaved";
    }
    if (m_unsavedChanges) title += L" *";
    if (m_gridSnap) title += L" [Snap]";
    title += L"  |  Ent: " + std::to_wstring(state.scene.GetEntityCount());
    SetWindowTextW(m_hwnd, title.c_str());
}

// ==================== ImGui Outliner Panel ====================

bool LevelEditorWindow::BeginSection(const char* icon, const char* label, bool defaultOpen) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 5));
    ImGui::PushStyleColor(ImGuiCol_Header,        kSectionBar);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  kSectionHover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,   kSectionHover);

    char buf[128];
    snprintf(buf, sizeof(buf), "%s  %s", icon, label);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
    if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    bool open = ImGui::TreeNodeEx(buf, flags);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    if (open) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 3));
        ImGui::Indent(4.0f);
        ImGui::Spacing();
    }
    return open;
}

void LevelEditorWindow::EndSection() {
    ImGui::Spacing();
    ImGui::Unindent(4.0f);
    ImGui::PopStyleVar(2);
    ImGui::TreePop();
}

void LevelEditorWindow::PropertyLabel(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(-1);
}

void LevelEditorWindow::SectionSeparator() {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.20f, 0.22f, 0.28f, 0.60f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void LevelEditorWindow::DrawOutlinerPanel(EditorState& state) {
    ImGui::SetNextWindowPos(ImVec2((float)m_width - m_panelWidth, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(m_panelWidth, (float)m_height), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.95f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));

    if (ImGui::Begin("##LevelEditorOutliner", nullptr, flags)) {
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::Text("Level Editor");
        ImGui::PopStyleColor();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::Text("%d ent", state.scene.GetEntityCount());
        ImGui::PopStyleColor();
        SectionSeparator();

        DrawToolSection(state);
        DrawLevelSection(state);
        DrawPCGSection(state);
        DrawEntitySection(state);
        DrawPrefabSection(state);
        DrawSceneSection(state);
        DrawLightingSection(state);
        DrawSkySection(state);
        DrawShadowsSection(state);
        DrawPostProcessSection(state);
        DrawArtStyleSection(state);
        DrawSSAOSection(state);
        DrawCharacterSection(state);
        DrawGridSection();
        DrawPlacementSection();
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ==================== Tool Section ====================
void LevelEditorWindow::DrawToolSection(EditorState& state) {
    (void)state;
    if (!BeginSection("T", "Tools", true)) return;

    float btnW = (ImGui::GetContentRegionAvail().x - 16) / 5.0f;
    const char* toolNames[] = { "Sel", "Move", "Rot", "Scl", "Place" };
    const char* toolKeys[]  = { "Q", "G", "R", "T", "P" };

    for (int i = 0; i < 5; i++) {
        bool active = (m_currentTool == static_cast<LevelEditTool>(i));
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.38f, 0.65f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.45f, 0.75f, 1.0f));
        }

        char btnLabel[32];
        snprintf(btnLabel, sizeof(btnLabel), "%s (%s)", toolNames[i], toolKeys[i]);

        if (ImGui::Button(btnLabel, ImVec2(btnW, 24))) {
            m_currentTool = static_cast<LevelEditTool>(i);
            if (i == 1) m_axisConstraint = AxisConstraint::XZ;
            else if (i == 2) m_axisConstraint = AxisConstraint::Y;
            else m_axisConstraint = AxisConstraint::None;
        }

        if (active) ImGui::PopStyleColor(2);
        if (i < 4) ImGui::SameLine();
    }

    if (m_currentTool == LevelEditTool::Move || m_currentTool == LevelEditTool::Rotate ||
        m_currentTool == LevelEditTool::Scale) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::Text("Axis Constraint:");
        ImGui::PopStyleColor();

        float axisBtnW = (ImGui::GetContentRegionAvail().x - 16) / 5.0f;
        struct AxisBtn { const char* label; AxisConstraint ac; ImVec4 color; };
        AxisBtn axisBtns[] = {
            { "Free", AxisConstraint::None, ImVec4(0.5f, 0.5f, 0.5f, 1.0f) },
            { "X",    AxisConstraint::X,    ImVec4(kAxisX.x, kAxisX.y, kAxisX.z, kAxisX.w) },
            { "Y",    AxisConstraint::Y,    ImVec4(kAxisY.x, kAxisY.y, kAxisY.z, kAxisY.w) },
            { "Z",    AxisConstraint::Z,    ImVec4(kAxisZ.x, kAxisZ.y, kAxisZ.z, kAxisZ.w) },
            { "XZ",   AxisConstraint::XZ,   ImVec4(0.6f, 0.3f, 0.6f, 1.0f) }
        };

        for (int i = 0; i < 5; i++) {
            bool active = (m_axisConstraint == axisBtns[i].ac);
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(
                    axisBtns[i].color.x * 0.5f, axisBtns[i].color.y * 0.5f,
                    axisBtns[i].color.z * 0.5f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(
                    axisBtns[i].color.x * 0.7f, axisBtns[i].color.y * 0.7f,
                    axisBtns[i].color.z * 0.7f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, axisBtns[i].color);
            }

            if (ImGui::Button(axisBtns[i].label, ImVec2(axisBtnW, 22))) {
                m_axisConstraint = axisBtns[i].ac;
            }

            if (active) ImGui::PopStyleColor(3);
            if (i < 4) ImGui::SameLine();
        }

        if (m_currentTool == LevelEditTool::Rotate) {
            ImGui::Spacing();
            PropertyLabel("Snap Rot");
            ImGui::Checkbox("##rotsnap", &m_rotationSnapOn);
            if (m_rotationSnapOn) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(60);
                ImGui::DragFloat("##rotsnapval", &m_rotationSnap, 1.0f, 1.0f, 90.0f, "%.0f\xc2\xb0");
            }
        }
        if (m_currentTool == LevelEditTool::Scale) {
            ImGui::Spacing();
            PropertyLabel("Uniform");
            ImGui::Checkbox("##uniformscl", &m_uniformScale);
            PropertyLabel("Snap Scl");
            ImGui::Checkbox("##sclsnap", &m_scaleSnapOn);
            if (m_scaleSnapOn) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(60);
                ImGui::DragFloat("##sclsnapval", &m_scaleSnap, 0.05f, 0.05f, 5.0f, "%.2f");
            }
        }
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.45f, 1.0f));
    ImGui::TextWrapped("Ctrl=Fine  X/Y/Z=Axis  Esc=Cancel");
    ImGui::PopStyleColor();

    EndSection();
}

// ==================== Level Section ====================
void LevelEditorWindow::DrawLevelSection(EditorState& state) {
    if (!BeginSection("LVL", "Level File")) return;

    if (ImGui::Button("New", ImVec2(50, 0))) {
        NewLevel(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(50, 0))) {
        SaveCurrentLevel(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load...", ImVec2(60, 0))) {
        ImGui::OpenPopup("##LELoadPopup");
    }
    ImGui::SameLine();
    if (ImGui::Button("Hot Swap", ImVec2(75, 0))) {
        m_hotSwapPending = true;
        state.physicsRebuildRequested = true;
        state.navRebuildRequested = true;
        state.entityDirty = true;
    }

    if (ImGui::BeginPopup("##LELoadPopup")) {
        auto files = LevelFile::ListLevels(m_levelsDirectory);
        if (files.empty()) {
            ImGui::TextDisabled("No .wtlevel files found");
        }
        for (auto& f : files) {
            std::string name = LevelFile::GetLevelName(f);
            if (ImGui::MenuItem(name.c_str())) {
                LoadLevel(f, state);
            }
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    PropertyLabel("Save As");
    ImGui::InputText("##levname", m_levelNameBuf, sizeof(m_levelNameBuf));
    if (m_levelNameBuf[0] != '\0') {
        ImGui::SameLine();
        if (ImGui::Button("Go")) {
            m_currentLevelPath = m_levelsDirectory + std::string(m_levelNameBuf) + ".wtlevel";
            SaveCurrentLevel(state);
            m_levelNameBuf[0] = '\0';
        }
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    if (!m_currentLevelPath.empty())
        ImGui::Text("File: %s", LevelFile::GetLevelName(m_currentLevelPath).c_str());
    else
        ImGui::Text("File: (unsaved)");
    ImGui::Text("Entities: %d", state.scene.GetEntityCount());
    if (m_unsavedChanges) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
        ImGui::Text("* Unsaved changes");
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleColor();

    EndSection();
}

// ==================== PCG Level Generator Section ====================
void LevelEditorWindow::DrawPCGSection(EditorState& state) {
    if (!BeginSection("PCG", "Level Generator", false)) return;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
    ImGui::Text("Procedural Urban Warfare");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Seed");
    int seedInt = static_cast<int>(m_pcgSettings.seed);
    if (ImGui::DragInt("##pcgseed", &seedInt, 1.0f, 0, 999999)) {
        m_pcgSettings.seed = static_cast<uint32_t>(seedInt);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::Text("  0 = random seed");
    ImGui::PopStyleColor();

    PropertyLabel("Arena Size");
    ImGui::DragFloat("##pcgarena", &m_pcgSettings.arenaSize, 0.5f, 20.0f, 100.0f, "%.0f");

    PropertyLabel("Grid");
    ImGui::PushItemWidth(50);
    ImGui::DragInt("##pcgcols", &m_pcgSettings.gridCols, 0.05f, 2, 5);
    ImGui::SameLine(); ImGui::Text("x");
    ImGui::SameLine();
    ImGui::DragInt("##pcgrows", &m_pcgSettings.gridRows, 0.05f, 2, 5);
    ImGui::PopItemWidth();

    PropertyLabel("Street Width");
    ImGui::DragFloat("##pcgstreet", &m_pcgSettings.streetWidth, 0.1f, 2.0f, 8.0f, "%.1f");

    PropertyLabel("Wall Height");
    ImGui::DragFloat("##pcgwallh", &m_pcgSettings.wallHeight, 0.1f, 2.0f, 8.0f, "%.1f");

    PropertyLabel("Building H");
    ImGui::PushItemWidth(60);
    ImGui::DragFloat("##pcgbhmin", &m_pcgSettings.buildingMinH, 0.1f, 1.5f, 8.0f, "%.1f");
    ImGui::SameLine(); ImGui::Text("-");
    ImGui::SameLine();
    ImGui::DragFloat("##pcgbhmax", &m_pcgSettings.buildingMaxH, 0.1f, 2.0f, 10.0f, "%.1f");
    ImGui::PopItemWidth();

    PropertyLabel("Building %");
    float bpct = m_pcgSettings.buildingChance * 100.0f;
    if (ImGui::DragFloat("##pcgbchance", &bpct, 0.5f, 0.0f, 100.0f, "%.0f%%"))
        m_pcgSettings.buildingChance = bpct / 100.0f;

    PropertyLabel("Roof %");
    float rpct = m_pcgSettings.roofChance * 100.0f;
    if (ImGui::DragFloat("##pcgroof", &rpct, 0.5f, 0.0f, 100.0f, "%.0f%%"))
        m_pcgSettings.roofChance = rpct / 100.0f;

    PropertyLabel("Cover Objects");
    ImGui::DragInt("##pcgcover", &m_pcgSettings.coverDensity, 0.2f, 0, 50);

    PropertyLabel("Detail Props");
    ImGui::DragInt("##pcgdetail", &m_pcgSettings.detailDensity, 0.2f, 0, 60);

    ImGui::Spacing();
    PropertyLabel("Options");
    ImGui::Checkbox("Windows##pcgwin", &m_pcgSettings.addWindows);
    ImGui::SameLine();
    ImGui::Checkbox("Fences##pcgfen", &m_pcgSettings.addFences);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Generate button
    ImVec4 genColor(0.2f, 0.6f, 0.2f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, genColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.5f, 0.15f, 1.0f));
    if (ImGui::Button("Generate Level", ImVec2(-1, 28))) {
        LevelGenerator gen;
        gen.settings = m_pcgSettings;
        gen.Generate(state.scene);
        state.selectedEntity = -1;
        state.physicsRebuildRequested = true;
        state.navRebuildRequested = true;
        state.entityDirty = true;
        m_unsavedChanges = true;
        m_currentLevelPath.clear();
        LOG_INFO("PCG: Generated level (seed %u, %d entities)",
                 gen.GetUsedSeed(), state.scene.GetEntityCount());
    }
    ImGui::PopStyleColor(3);

    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::Text("  Replaces current level!");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Checkbox("Random level on launch##pcglaunch", &state.pcgOnLaunch);

    EndSection();
}

// ==================== Entity Section ====================
void LevelEditorWindow::DrawEntitySection(EditorState& state) {
    if (!BeginSection("ENT", "Entities")) return;

    auto& scene = state.scene;
    int count = scene.GetEntityCount();

    if (ImGui::Button("+ Cube", ImVec2(65, 0))) {
        int idx = scene.AddEntity("", MeshType::Cube);
        state.selectedEntity = idx;
        m_unsavedChanges = true;
    }
    ImGui::SameLine();

    auto modelNames = ResourceManager::Get().GetModelNames();
    if (!modelNames.empty()) {
        if (ImGui::Button("+ Model", ImVec2(65, 0))) {
            ImGui::OpenPopup("##LEModelSpawn");
        }
        if (ImGui::BeginPopup("##LEModelSpawn")) {
            for (auto& mname : modelNames) {
                if (ImGui::MenuItem(mname.c_str())) {
                    int idx = scene.AddEntity("", MeshType::Custom);
                    scene.GetEntity(idx).meshName = mname;
                    scene.GetEntity(idx).name = mname + "_" + std::to_string(idx);
                    state.selectedEntity = idx;
                    m_unsavedChanges = true;
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
    }

    if (count > 0 && state.selectedEntity >= 0) {
        if (ImGui::Button("Dup", ImVec2(38, 0))) {
            int idx = scene.DuplicateEntity(state.selectedEntity);
            if (idx >= 0) { state.selectedEntity = idx; m_unsavedChanges = true; }
        }
        ImGui::SameLine();
        if (ImGui::Button("Del", ImVec2(38, 0))) {
            scene.RemoveEntity(state.selectedEntity);
            if (state.selectedEntity >= scene.GetEntityCount())
                state.selectedEntity = scene.GetEntityCount() - 1;
            m_unsavedChanges = true;
        }
    }

    count = scene.GetEntityCount();
    ImGui::Spacing();

    if (count > 0) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.07f, 0.09f, 1.0f));
        float listHeight = (float)((std::min)(count, 10)) * 20.0f + 4.0f;
        ImGui::BeginChild("##leentlist", ImVec2(0, listHeight), ImGuiChildFlags_Borders);

        for (int i = 0; i < count; i++) {
            auto& e = scene.GetEntity(i);
            bool selected = (state.selectedEntity == i);
            bool hovered = (m_hoveredEntity == i);

            char buf[128];
            snprintf(buf, sizeof(buf), " %s  %s", (e.meshType == MeshType::Cube ? "[C]" : "[M]"), e.name.c_str());

            if (hovered && !selected)
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.15f, 0.22f, 0.35f, 0.5f));
            else
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.28f, 0.45f, 1.0f));

            if (ImGui::Selectable(buf, selected, ImGuiSelectableFlags_None, ImVec2(0, 18))) {
                state.selectedEntity = i;
            }
            ImGui::PopStyleColor();

            if (!e.visible) {
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
                ImGui::Text("(hid)");
                ImGui::PopStyleColor();
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::Text("  No entities.");
        ImGui::PopStyleColor();
    }

    if (state.selectedEntity >= 0 && state.selectedEntity < count) {
        SectionSeparator();
        auto& e = scene.GetEntity(state.selectedEntity);

        char nameBuf[128];
        strncpy_s(nameBuf, e.name.c_str(), sizeof(nameBuf) - 1);
        PropertyLabel("Name");
        if (ImGui::InputText("##leentname", nameBuf, sizeof(nameBuf))) {
            e.name = nameBuf;
            m_unsavedChanges = true;
        }

        PropertyLabel("Type");
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::Text("%s", MeshTypeName(e.meshType));
        ImGui::PopStyleColor();

        if (e.meshType == MeshType::Custom) {
            PropertyLabel("Model");
            auto allModels = ResourceManager::Get().GetModelNames();
            int currentModel = -1;
            for (int m = 0; m < (int)allModels.size(); m++) {
                if (allModels[m] == e.meshName) { currentModel = m; break; }
            }
            if (ImGui::BeginCombo("##leentmodel", currentModel >= 0 ? allModels[currentModel].c_str() : "<none>")) {
                for (int m = 0; m < (int)allModels.size(); m++) {
                    bool sel = (m == currentModel);
                    if (ImGui::Selectable(allModels[m].c_str(), sel)) {
                        e.meshName = allModels[m];
                        m_unsavedChanges = true;
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        // Texture override picker (for any entity)
        {
            PropertyLabel("Texture");
            auto allTextures = ResourceManager::Get().GetTextureNames();
            int currentTex = -1;
            for (int t = 0; t < (int)allTextures.size(); t++) {
                if (allTextures[t] == e.textureName) { currentTex = t; break; }
            }
            const char* preview = currentTex >= 0 ? allTextures[currentTex].c_str() : "(default)";
            if (ImGui::BeginCombo("##leenttex", preview)) {
                // Option to clear texture override
                if (ImGui::Selectable("(default)", currentTex < 0)) {
                    e.textureName.clear();
                    m_unsavedChanges = true;
                }
                ImGui::Separator();
                for (int t = 0; t < (int)allTextures.size(); t++) {
                    bool sel = (t == currentTex);
                    if (ImGui::Selectable(allTextures[t].c_str(), sel)) {
                        e.textureName = allTextures[t];
                        m_unsavedChanges = true;
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        SectionSeparator();
        ImGui::PushStyleColor(ImGuiCol_Text, kAccentDim);
        ImGui::Text("  Transform");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        PropertyLabel("Position");
        if (ImGui::InputFloat3("##leentpos", e.position, "%.3f")) m_unsavedChanges = true;

        PropertyLabel("Rotation");
        if (ImGui::InputFloat3("##leentrot", e.rotation, "%.2f")) m_unsavedChanges = true;

        PropertyLabel("Scale");
        if (ImGui::InputFloat3("##leentscl", e.scale, "%.3f")) m_unsavedChanges = true;

        PropertyLabel("Color");
        if (ImGui::ColorEdit4("##leentcol", e.color,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) m_unsavedChanges = true;

        SectionSeparator();

        PropertyLabel("Visible");
        if (ImGui::Checkbox("##leentvis", &e.visible)) m_unsavedChanges = true;
        ImGui::SameLine();
        PropertyLabel("Shadow");
        if (ImGui::Checkbox("##leentshd", &e.castShadow)) m_unsavedChanges = true;

        // Destruction
        SectionSeparator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.3f, 1.0f));
        ImGui::Text("  Destruction");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        // Material type combo
        PropertyLabel("Material");
        const char* matNames[] = { "Concrete", "Wood", "Metal", "Glass" };
        int matIdx = static_cast<int>(e.materialType);
        if (ImGui::Combo("##leentmat", &matIdx, matNames, IM_ARRAYSIZE(matNames))) {
            e.materialType = static_cast<MaterialType>(matIdx);
            m_unsavedChanges = true;
        }

        PropertyLabel("Destructible");
        if (ImGui::Checkbox("##leentdest", &e.destructible)) m_unsavedChanges = true;

        if (e.destructible) {
            PropertyLabel("Health");
            if (ImGui::DragFloat("##leenthp", &e.health, 1.0f, 0.0f, 10000.0f, "%.0f")) m_unsavedChanges = true;
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            ImGui::Text("/ %.0f", e.maxHealth);
            ImGui::PopStyleColor();

            PropertyLabel("Max Health");
            if (ImGui::DragFloat("##leentmhp", &e.maxHealth, 1.0f, 1.0f, 10000.0f, "%.0f")) {
                if (e.health > e.maxHealth) e.health = e.maxHealth;
                m_unsavedChanges = true;
            }

            PropertyLabel("Debris Count");
            if (ImGui::DragInt("##leentdc", &e.debrisCount, 0.1f, 1, 50)) m_unsavedChanges = true;
            PropertyLabel("Debris Scale");
            if (ImGui::DragFloat("##leentds", &e.debrisScale, 0.01f, 0.05f, 2.0f, "%.2f")) m_unsavedChanges = true;
            PropertyLabel("Break Pieces");
            if (ImGui::DragInt("##leentbp", &e.breakPieceCount, 0.1f, 0, 8)) m_unsavedChanges = true;

            // Structural support
            PropertyLabel("Supported By");
            char supBuf[128] = {};
            strncpy(supBuf, e.supportedBy.c_str(), sizeof(supBuf) - 1);
            if (ImGui::InputText("##leentsup", supBuf, sizeof(supBuf))) {
                e.supportedBy = supBuf;
                m_unsavedChanges = true;
            }

            // Voxel chunk destruction
            PropertyLabel("Voxel Destruct");
            if (ImGui::Checkbox("##leentvox", &e.voxelDestruction)) m_unsavedChanges = true;
            if (e.voxelDestruction) {
                ImGui::SameLine();
                PropertyLabel("Res");
                if (ImGui::DragInt("##leentvoxres", &e.voxelRes, 0.05f, 2, 8)) {
                    e.ResetVoxelMask();
                    m_unsavedChanges = true;
                }
            }

            float frac = e.GetHealthFraction();
            ImVec4 barColor;
            if (frac > 0.5f) barColor = ImVec4((1.0f - frac) * 2.0f, 1.0f, 0.0f, 1.0f);
            else             barColor = ImVec4(1.0f, frac * 2.0f, 0.0f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
            char hpBuf[32];
            snprintf(hpBuf, sizeof(hpBuf), "%.0f / %.0f", e.health, e.maxHealth);
            ImGui::ProgressBar(frac, ImVec2(-1, 14), hpBuf);
            ImGui::PopStyleColor();

            const char* stages[] = { "Pristine", "Scratched", "Damaged", "Critical" };
            ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            ImGui::Text("  Stage: %s", stages[e.GetDamageStage()]);
            ImGui::PopStyleColor();

            if (ImGui::Button("Reset HP", ImVec2(70, 0))) {
                e.health = e.maxHealth;
                e.damageFlashTimer = 0.0f;
                e.hitDecalCount = 0;
                e.hitDecalNext = 0;
                if (e.voxelDestruction)
                    e.ResetVoxelMask();
            }
        }
    }

    EndSection();
}

// ==================== Prefab Section ====================
void LevelEditorWindow::DrawPrefabSection(EditorState& state) {
    if (!BeginSection("PRE", "Prefabs")) return;

    auto& scene = state.scene;
    float btnW = (ImGui::GetContentRegionAvail().x - 8) / 2.0f;

    // Helper: compute grid-snapped spawn position
    auto getSpawnPos = [&](float defaultY) -> XMFLOAT3 {
        XMFLOAT3 sp = { 0, defaultY, 0 };
        if (m_gridSnap) {
            XMFLOAT3 wp = ScreenToWorldPlane(m_width / 2, m_height / 2, 0.0f);
            sp.x = roundf(wp.x / m_gridSnapSize) * m_gridSnapSize;
            sp.z = roundf(wp.z / m_gridSnapSize) * m_gridSnapSize;
        }
        return sp;
    };

    // Helper: spawn a cube preset entity
    auto spawnCube = [&](const char* label, float sx, float sy, float sz,
                         float r, float g, float b, float hp,
                         float yOff = 0.0f, int debris = 6, float debrisS = 0.3f,
                         const char* texFolder = nullptr) {
        XMFLOAT3 sp = getSpawnPos(yOff);
        int idx = scene.AddEntity(label, MeshType::Cube);
        auto& e = scene.GetEntity(idx);
        e.name = std::string(label) + "_" + std::to_string(idx);
        e.position[0] = sp.x; e.position[1] = sp.y; e.position[2] = sp.z;
        e.scale[0] = sx; e.scale[1] = sy; e.scale[2] = sz;
        e.color[0] = r; e.color[1] = g; e.color[2] = b; e.color[3] = 1.0f;
        e.health = hp; e.maxHealth = hp;
        e.debrisCount = debris; e.debrisScale = debrisS;
        if (texFolder && texFolder[0]) {
            std::string texKey = std::string(texFolder) + "texture";
            if (ResourceManager::Get().GetTexture(texKey))
                e.textureName = texKey;
        }
        state.selectedEntity = idx;
        m_unsavedChanges = true;
    };

    // Helper: spawn a custom model entity
    auto spawnModel = [&](const std::string& modelKey, const char* texFolder, float yOff = 0.0f) {
        std::string display = modelKey.substr(modelKey.rfind('/') + 1);
        XMFLOAT3 sp = getSpawnPos(yOff);
        int idx = scene.AddEntity("", MeshType::Custom);
        auto& e = scene.GetEntity(idx);
        e.meshName = modelKey;
        e.name = display + "_" + std::to_string(idx);
        e.position[0] = sp.x; e.position[1] = sp.y; e.position[2] = sp.z;
        e.color[0] = 1.0f; e.color[1] = 1.0f; e.color[2] = 1.0f; e.color[3] = 1.0f;
        e.health = 200.0f; e.maxHealth = 200.0f;
        // Auto-assign texture from matching category folder
        if (texFolder && texFolder[0]) {
            std::string texKey = std::string(texFolder) + "texture";
            if (ResourceManager::Get().GetTexture(texKey))
                e.textureName = texKey;
        }
        state.selectedEntity = idx;
        m_unsavedChanges = true;
    };

    // Gather all model names for filtering
    auto allModels = ResourceManager::Get().GetModelNames();

    // Category definitions
    struct PrefabCat { const char* label; const char* folder; const char* texFolder; ImVec4 col; ImVec4 hover; };
    const PrefabCat cats[] = {
        { "Walls",      "PreFabs/Walls/",      "Walls/",       {0.22f,0.20f,0.16f,1}, {0.30f,0.27f,0.22f,1} },
        { "Floors",     "PreFabs/Floors/",     "Floors/",      {0.18f,0.22f,0.18f,1}, {0.24f,0.30f,0.24f,1} },
        { "Structures", "PreFabs/Structures/", "Walls/",       {0.20f,0.18f,0.22f,1}, {0.28f,0.24f,0.30f,1} },
        { "Doors",      "PreFabs/Doors/",      "Walls/",       {0.22f,0.18f,0.18f,1}, {0.30f,0.24f,0.24f,1} },
        { "Props",      "PreFabs/Props/",      "Props/",       {0.24f,0.20f,0.14f,1}, {0.32f,0.27f,0.18f,1} },
        { "Buildings",  "Prefabs/",            "",             {0.16f,0.22f,0.26f,1}, {0.22f,0.30f,0.36f,1} },
    };
    const int catCount = sizeof(cats) / sizeof(cats[0]);

    for (int c = 0; c < catCount; c++) {
        if (c % 2 != 0) ImGui::SameLine();

        // Count models in this category
        int modelCount = 0;
        for (auto& m : allModels)
            if (m.rfind(cats[c].folder, 0) == 0) modelCount++;

        ImGui::PushStyleColor(ImGuiCol_Button, cats[c].col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cats[c].hover);

        char btnId[64];
        snprintf(btnId, sizeof(btnId), "%s (%d)##pfcat%d", cats[c].label, modelCount, c);
        if (ImGui::Button(btnId, ImVec2(btnW, 28))) {
            char popId[32]; snprintf(popId, sizeof(popId), "##PFB_%d", c);
            ImGui::OpenPopup(popId);
        }
        ImGui::PopStyleColor(2);

        // ---- Category popup browser ----
        char popId[32]; snprintf(popId, sizeof(popId), "##PFB_%d", c);
        ImGui::SetNextWindowSizeConstraints(ImVec2(240, 120), ImVec2(360, 500));
        if (ImGui::BeginPopup(popId)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.88f, 0.65f, 1.0f));
            ImGui::Text("%s", cats[c].label);
            ImGui::PopStyleColor();
            ImGui::Separator();

            // --- Cube presets per category ---
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.7f, 1.0f));
            ImGui::Text("Basic Shapes");
            ImGui::PopStyleColor();

            if (c == 0) { // Walls
                if (ImGui::Selectable("  Wall (Cube 4x3x0.3)"))
                    spawnCube("Wall", 4,3,0.3f, 0.55f,0.52f,0.48f, 200, 1.5f);
            }
            else if (c == 1) { // Floors
                if (ImGui::Selectable("  Floor (Cube 8x0.1x8)"))
                    spawnCube("Floor", 8,0.1f,8, 0.45f,0.44f,0.42f, 500, -0.05f);
            }
            else if (c == 2) { // Structures
                if (ImGui::Selectable("  Pillar (Cube 0.5x4x0.5)"))
                    spawnCube("Pillar", 0.5f,4,0.5f, 0.50f,0.48f,0.45f, 300, 2.0f);
                if (ImGui::Selectable("  Bunker (Cube 5x2.5x4)"))
                    spawnCube("Bunker", 5,2.5f,4, 0.35f,0.36f,0.34f, 800, 1.25f, 12);
            }
            else if (c == 3) { // Doors
                // No cube presets
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f,0.4f,0.4f,1));
                ImGui::Text("  (none)");
                ImGui::PopStyleColor();
            }
            else if (c == 4) { // Props
                if (ImGui::Selectable("  Crate (Cube 1x1x1)"))
                    spawnCube("Crate", 1,1,1, 0.55f,0.40f,0.22f, 50, 0.5f, 8, 0.2f);
                if (ImGui::Selectable("  Cover (Cube 3x1x0.4)"))
                    spawnCube("Cover", 3,1,0.4f, 0.40f,0.42f,0.38f, 150, 0.5f);
            }
            else if (c == 5) { // Buildings
                if (ImGui::Selectable("  Building Block (Cube 6x4x6)"))
                    spawnCube("Building", 6,4,6, 0.48f,0.46f,0.44f, 500, 2.0f, 12, 0.4f);
            }

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.7f, 1.0f));
            ImGui::Text("Models");
            ImGui::PopStyleColor();
            ImGui::Separator();

            // Collect models in this category
            std::vector<std::string> catModels;
            for (auto& mname : allModels) {
                if (mname.rfind(cats[c].folder, 0) == 0)
                    catModels.push_back(mname);
            }

            for (auto& mname : catModels) {
                std::string display = mname.substr(mname.rfind('/') + 1);
                for (auto& ch : display) if (ch == '_') ch = ' ';
                char selLabel[128];
                snprintf(selLabel, sizeof(selLabel), "  %s##%s", display.c_str(), mname.c_str());
                if (ImGui::Selectable(selLabel)) {
                    spawnModel(mname, cats[c].texFolder, 0.0f);
                }
            }
            if (catModels.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f,0.4f,0.4f,1));
                ImGui::Text("  No models loaded");
                ImGui::PopStyleColor();
            }

            // --- Batch Import All ---
            if (!catModels.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.55f, 0.20f, 1.0f));
                char batchLabel[64];
                snprintf(batchLabel, sizeof(batchLabel), "Spawn All (%d)##batch%d", (int)catModels.size(), c);
                if (ImGui::Button(batchLabel, ImVec2(-1, 28))) {
                    for (auto& mname : catModels) {
                        spawnModel(mname, cats[c].texFolder, 0.0f);
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor(2);
            }

            ImGui::EndPopup();
        }
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.45f, 1.0f));
    ImGui::TextWrapped("Click a category to browse and spawn prefabs.");
    ImGui::PopStyleColor();

    EndSection();
}

// ==================== Grid Section ====================
void LevelEditorWindow::DrawGridSection() {
    if (!BeginSection("GRD", "Grid & Snap", false)) return;

    PropertyLabel("Show Grid");
    bool showGrid = (m_gridExtent > 0);
    if (ImGui::Checkbox("##showgrid", &showGrid)) {
        m_gridExtent = showGrid ? 50 : 0;
    }

    PropertyLabel("Grid Size");
    ImGui::DragFloat("##gridsize", &m_gridSize, 0.1f, 0.25f, 10.0f, "%.2f");

    SectionSeparator();

    PropertyLabel("Snap");
    ImGui::Checkbox("##snapon", &m_gridSnap);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::Text("(`)");
    ImGui::PopStyleColor();

    PropertyLabel("Snap Size");
    ImGui::DragFloat("##snapsize", &m_gridSnapSize, 0.1f, 0.1f, 10.0f, "%.2f");

    SectionSeparator();

    PropertyLabel("Gizmo Size");
    ImGui::DragFloat("##gizmosize", &m_gizmoLength, 0.1f, 0.5f, 10.0f, "%.1f");

    PropertyLabel("Cam Speed");
    ImGui::DragFloat("##camspeed", &m_camSpeed, 0.5f, 1.0f, 100.0f, "%.1f");

    PropertyLabel("Cam FOV");
    ImGui::DragFloat("##camfov", &m_camFOV, 0.5f, 30.0f, 120.0f, "%.0f");

    EndSection();
}

// ==================== Placement Section ====================
void LevelEditorWindow::DrawPlacementSection() {
    if (m_currentTool != LevelEditTool::Place) return;

    if (!BeginSection("PLC", "Placement")) return;

    PropertyLabel("Mesh");
    const char* meshTypes[] = { "Cube", "Custom" };
    int mt = static_cast<int>(m_placeMeshType);
    if (ImGui::Combo("##plcmesh", &mt, meshTypes, 2)) {
        m_placeMeshType = static_cast<MeshType>(mt);
    }

    if (m_placeMeshType == MeshType::Custom) {
        PropertyLabel("Model");
        auto modelNames = ResourceManager::Get().GetModelNames();
        if (ImGui::BeginCombo("##plcmodel", m_placeMeshName.empty() ? "<select>" : m_placeMeshName.c_str())) {
            for (auto& mname : modelNames) {
                bool sel = (mname == m_placeMeshName);
                if (ImGui::Selectable(mname.c_str(), sel)) {
                    m_placeMeshName = mname;
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    PropertyLabel("Color");
    ImGui::ColorEdit4("##plccol", m_placeColor,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

    EndSection();
}

// ==================== Scene Section ====================
void LevelEditorWindow::DrawSceneSection(EditorState& state) {
    if (!BeginSection("SCN", "Scene", false)) return;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.07f, 0.09f, 1.0f));
    ImGui::BeginChild("##lescenelist", ImVec2(0, 48), ImGuiChildFlags_Borders);

    {
        char buf[64];
        snprintf(buf, sizeof(buf), " [=]  Ground Plane");
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.28f, 0.45f, 1.0f));
        ImGui::Selectable(buf, false, ImGuiSelectableFlags_None, ImVec2(0, 20));
        ImGui::PopStyleColor();
    }
    if (state.showDebug) {
        char buf[64];
        snprintf(buf, sizeof(buf), " [*]  Debug Visuals");
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.28f, 0.45f, 1.0f));
        ImGui::Selectable(buf, false, ImGuiSelectableFlags_None, ImVec2(0, 20));
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    SectionSeparator();

    PropertyLabel("Ground");
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::Text("400 x 400 units");
    ImGui::PopStyleColor();

    PropertyLabel("Debug Lines");
    ImGui::Checkbox("##lescenedbg", &state.showDebug);

    EndSection();
}

// ==================== Lighting Section ====================
void LevelEditorWindow::DrawLightingSection(EditorState& state) {
    if (!BeginSection("SUN", "Lighting")) return;

    static const ImVec4 kOrange = ImVec4(1.00f, 0.60f, 0.20f, 1.00f);

    ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
    ImGui::Text("  Directional Light");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Direction");
    state.lightingDirty |= ImGui::DragFloat3("##lesundir", state.sunDirection, 0.01f, -1.0f, 1.0f);
    PropertyLabel("Intensity");
    state.lightingDirty |= ImGui::DragFloat("##lesunint", &state.sunIntensity, 0.05f, 0.0f, 10.0f);
    PropertyLabel("Color");
    state.lightingDirty |= ImGui::ColorEdit3("##lesuncol", state.sunColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

    SectionSeparator();

    ImGui::PushStyleColor(ImGuiCol_Text, kAccentDim);
    ImGui::Text("  Ambient");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Color");
    state.lightingDirty |= ImGui::ColorEdit3("##leambcol", state.ambientColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    PropertyLabel("Intensity");
    state.lightingDirty |= ImGui::DragFloat("##leambint", &state.ambientIntensity, 0.05f, 0.0f, 5.0f);

    SectionSeparator();

    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::Text("  Fog");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Color");
    state.lightingDirty |= ImGui::ColorEdit3("##lefogcol", state.fogColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    PropertyLabel("Density");
    state.lightingDirty |= ImGui::DragFloat("##lefogden", &state.fogDensity, 0.01f, 0.0f, 5.0f);
    PropertyLabel("Start");
    state.lightingDirty |= ImGui::DragFloat("##lefogst", &state.fogStart, 1.0f, 0.0f, 1000.0f);
    PropertyLabel("End");
    state.lightingDirty |= ImGui::DragFloat("##lefogen", &state.fogEnd, 1.0f, 0.0f, 2000.0f);

    EndSection();
}

// ==================== Sky Section ====================
void LevelEditorWindow::DrawSkySection(EditorState& state) {
    if (!BeginSection("SKY", "Sky / Environment")) return;

    static const ImVec4 kOrange = ImVec4(1.00f, 0.60f, 0.20f, 1.00f);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
    ImGui::Text("  Atmosphere");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Zenith");
    state.skyDirty |= ImGui::ColorEdit3("##leskyzen", state.skyZenithColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    PropertyLabel("Horizon");
    state.skyDirty |= ImGui::ColorEdit3("##leskyhor", state.skyHorizonColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    PropertyLabel("Ground");
    state.skyDirty |= ImGui::ColorEdit3("##leskygnd", state.skyGroundColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    PropertyLabel("Brightness");
    state.skyDirty |= ImGui::DragFloat("##leskybrt", &state.skyBrightness, 0.01f, 0.1f, 5.0f);
    PropertyLabel("Horizon Fall");
    state.skyDirty |= ImGui::DragFloat("##leskyhf", &state.skyHorizonFalloff, 0.01f, 0.1f, 3.0f);

    SectionSeparator();

    ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
    ImGui::Text("  Sun Disc");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Disc Size");
    float discAngleDeg = acosf(state.sunDiscSize) * (180.0f / 3.14159f);
    if (ImGui::DragFloat("##lesdisc", &discAngleDeg, 0.01f, 0.01f, 5.0f, "%.2f deg")) {
        state.sunDiscSize = cosf(discAngleDeg * (3.14159f / 180.0f));
        state.skyDirty = true;
    }
    PropertyLabel("Glow Int.");
    state.skyDirty |= ImGui::DragFloat("##lesglowi", &state.sunGlowIntensity, 0.01f, 0.0f, 2.0f);
    PropertyLabel("Glow Tight");
    state.skyDirty |= ImGui::DragFloat("##lesglowf", &state.sunGlowFalloff, 1.0f, 1.0f, 256.0f);

    SectionSeparator();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.95f, 1.0f));
    ImGui::Text("  Clouds");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Coverage");
    state.skyDirty |= ImGui::DragFloat("##lecldcov", &state.cloudCoverage, 0.01f, 0.0f, 1.0f);
    PropertyLabel("Speed");
    state.skyDirty |= ImGui::DragFloat("##lecldspd", &state.cloudSpeed, 0.005f, 0.0f, 0.5f);
    PropertyLabel("Density");
    state.skyDirty |= ImGui::DragFloat("##lecldden", &state.cloudDensity, 0.1f, 0.5f, 10.0f);
    PropertyLabel("Height");
    state.skyDirty |= ImGui::DragFloat("##lecldhgt", &state.cloudHeight, 0.01f, 0.05f, 1.0f);
    PropertyLabel("Color");
    state.skyDirty |= ImGui::ColorEdit3("##lecldcol", state.cloudColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    PropertyLabel("Sun Lit");
    state.skyDirty |= ImGui::DragFloat("##lecldsun", &state.cloudSunInfluence, 0.01f, 0.0f, 1.0f);

    EndSection();
}

// ==================== Shadows Section ====================
void LevelEditorWindow::DrawShadowsSection(EditorState& state) {
    if (!BeginSection("SHD", "Shadows")) return;

    PropertyLabel("Enabled");
    ImGui::Checkbox("##leshden", &state.shadowsEnabled);

    if (state.shadowsEnabled) {
        PropertyLabel("Intensity");
        ImGui::DragFloat("##leshdint", &state.shadowIntensity, 0.01f, 0.0f, 1.0f);
        PropertyLabel("Bias");
        ImGui::DragFloat("##leshdbias", &state.shadowBias, 0.0001f, 0.0f, 0.01f, "%.4f");
        PropertyLabel("Normal Bias");
        ImGui::DragFloat("##leshdnbias", &state.shadowNormalBias, 0.001f, 0.0f, 0.1f, "%.3f");
        PropertyLabel("Distance");
        ImGui::DragFloat("##leshddist", &state.shadowDistance, 0.5f, 5.0f, 100.0f);

        SectionSeparator();

        PropertyLabel("Resolution");
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::Text("%dx%d", state.shadowMapResolution, state.shadowMapResolution);
        ImGui::PopStyleColor();
    }

    EndSection();
}

// ==================== Post Processing Section ====================
void LevelEditorWindow::DrawPostProcessSection(EditorState& state) {
    if (!BeginSection("FX", "Post Processing")) return;

    PropertyLabel("Bloom");
    ImGui::Checkbox("##leppbloom", &state.ppBloomEnabled);

    if (state.ppBloomEnabled) {
        PropertyLabel("  Threshold");
        ImGui::DragFloat("##leppbloomth", &state.ppBloomThreshold, 0.01f, 0.0f, 5.0f);
        PropertyLabel("  Intensity");
        ImGui::DragFloat("##leppbloomint", &state.ppBloomIntensity, 0.01f, 0.0f, 3.0f);
    }

    SectionSeparator();

    PropertyLabel("Vignette");
    ImGui::Checkbox("##leppvignette", &state.ppVignetteEnabled);

    if (state.ppVignetteEnabled) {
        PropertyLabel("  Intensity");
        ImGui::DragFloat("##leppvigint", &state.ppVignetteIntensity, 0.01f, 0.0f, 2.0f);
        PropertyLabel("  Smoothness");
        ImGui::DragFloat("##leppvigsm", &state.ppVignetteSmoothness, 0.01f, 0.0f, 2.0f);
    }

    SectionSeparator();

    PropertyLabel("Brightness");
    ImGui::DragFloat("##leppbright", &state.ppBrightness, 0.005f, -1.0f, 1.0f);
    PropertyLabel("Contrast");
    ImGui::DragFloat("##leppcontrast", &state.ppContrast, 0.01f, 0.0f, 2.0f);
    PropertyLabel("Saturation");
    ImGui::DragFloat("##leppsat", &state.ppSaturation, 0.01f, 0.0f, 2.0f);
    PropertyLabel("Gamma");
    ImGui::DragFloat("##leppgamma", &state.ppGamma, 0.01f, 0.5f, 2.0f);
    PropertyLabel("Tint");
    ImGui::ColorEdit3("##lepptint", state.ppTint, ImGuiColorEditFlags_Float);

    EndSection();
}

// ==================== Art Style Section ====================
void LevelEditorWindow::DrawArtStyleSection(EditorState& state) {
    if (!BeginSection("ART", "Art Style")) return;

    static const ImVec4 kOrange = ImVec4(1.00f, 0.60f, 0.20f, 1.00f);

    ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
    ImGui::Text("  Cel-Shading");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Enabled");
    state.lightingDirty |= ImGui::Checkbox("##lecelen", &state.celEnabled);

    if (state.celEnabled) {
        PropertyLabel("Bands");
        state.lightingDirty |= ImGui::DragFloat("##lecelbands", &state.celBands, 0.1f, 2.0f, 6.0f, "%.0f");
        PropertyLabel("Rim Light");
        state.lightingDirty |= ImGui::DragFloat("##lecelrim", &state.celRimIntensity, 0.01f, 0.0f, 2.0f);
    }

    SectionSeparator();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.8f, 1.0f));
    ImGui::Text("  Ink Outlines");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Enabled");
    ImGui::Checkbox("##leoutlineen", &state.outlineEnabled);

    if (state.outlineEnabled) {
        PropertyLabel("Thickness");
        ImGui::DragFloat("##leoutthick", &state.outlineThickness, 0.05f, 0.5f, 4.0f);
        PropertyLabel("Color");
        ImGui::ColorEdit3("##leoutcol", state.outlineColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    }

    SectionSeparator();

    ImGui::PushStyleColor(ImGuiCol_Text, kAccentDim);
    ImGui::Text("  Hand-Drawn");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("Paper Grain");
    ImGui::DragFloat("##leppgrain", &state.paperGrainIntensity, 0.002f, 0.0f, 0.15f, "%.3f");
    PropertyLabel("Hatching");
    ImGui::DragFloat("##lehatchint", &state.hatchingIntensity, 0.01f, 0.0f, 1.0f);
    if (state.hatchingIntensity > 0.001f) {
        PropertyLabel("  Scale");
        ImGui::DragFloat("##lehatchscl", &state.hatchingScale, 0.1f, 1.0f, 16.0f);
    }

    EndSection();
}

// ==================== SSAO Section ====================
void LevelEditorWindow::DrawSSAOSection(EditorState& state) {
    if (!BeginSection("AO", "Ambient Occlusion")) return;

    PropertyLabel("Enabled");
    ImGui::Checkbox("##lessaoen", &state.ssaoEnabled);

    if (state.ssaoEnabled) {
        PropertyLabel("Radius");
        ImGui::DragFloat("##lessaorad", &state.ssaoRadius, 0.01f, 0.05f, 5.0f);
        PropertyLabel("Bias");
        ImGui::DragFloat("##lessaobias", &state.ssaoBias, 0.001f, 0.0f, 0.1f, "%.3f");
        PropertyLabel("Intensity");
        ImGui::DragFloat("##lessaoint", &state.ssaoIntensity, 0.05f, 0.0f, 5.0f);

        PropertyLabel("Samples");
        int ks = state.ssaoKernelSize;
        if (ImGui::SliderInt("##lessaokernel", &ks, 4, 64)) {
            state.ssaoKernelSize = ks;
        }
    }

    EndSection();
}

// ==================== Character Section ====================
void LevelEditorWindow::DrawCharacterSection(EditorState& state) {
    if (!BeginSection("CHR", "Character")) return;

    static const ImVec4 kOrange = ImVec4(1.00f, 0.60f, 0.20f, 1.00f);

    ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
    ImGui::Text("  Mode");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    PropertyLabel("FPS Mode");
    ImGui::Checkbox("##lecharmode", &state.characterMode);
    ImGui::SameLine();
    ImGui::TextDisabled("(F8)");

    PropertyLabel("Show Body");
    ImGui::Checkbox("##lecharbody", &state.charShowBody);

    if (state.characterMode) {
        SectionSeparator();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
        ImGui::Text("  Movement");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        PropertyLabel("Move Speed");
        ImGui::DragFloat("##lecharspd", &state.charMoveSpeed, 0.1f, 1.0f, 20.0f);
        PropertyLabel("Sprint Mult");
        ImGui::DragFloat("##lecharsprint", &state.charSprintMult, 0.1f, 1.0f, 5.0f);
        PropertyLabel("Jump Force");
        ImGui::DragFloat("##lecharjump", &state.charJumpForce, 0.1f, 1.0f, 20.0f);
        PropertyLabel("Gravity");
        ImGui::DragFloat("##lechargrav", &state.charGravity, 0.5f, 1.0f, 50.0f);
        PropertyLabel("Ground Y");
        ImGui::DragFloat("##lechargy", &state.charGroundY, 0.1f, -10.0f, 10.0f);
        PropertyLabel("Eye Height");
        ImGui::DragFloat("##lechareye", &state.charEyeHeight, 0.05f, 0.5f, 3.0f);

        SectionSeparator();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.8f, 0.5f, 1.0f));
        ImGui::Text("  Crouch");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        PropertyLabel("Eye Height");
        ImGui::DragFloat("##lecroucheye", &state.charCrouchEyeHeight, 0.05f, 0.3f, 1.5f);
        PropertyLabel("Speed Mult");
        ImGui::DragFloat("##lecrouchspd", &state.charCrouchSpeedMult, 0.05f, 0.1f, 1.0f);
        PropertyLabel("Transition");
        ImGui::DragFloat("##lecrouchtrans", &state.charCrouchTransSpeed, 0.5f, 1.0f, 20.0f);
        ImGui::TextDisabled("Hold Ctrl to crouch");

        SectionSeparator();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
        ImGui::Text("  Camera Tilt");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        PropertyLabel("Enabled");
        ImGui::Checkbox("##letiltena", &state.charCameraTiltEnabled);
        if (state.charCameraTiltEnabled) {
            PropertyLabel("Amount");
            ImGui::DragFloat("##letiltamt", &state.charCameraTiltAmount, 0.1f, 0.5f, 8.0f, "%.1f deg");
            PropertyLabel("Speed");
            ImGui::DragFloat("##letiltspd", &state.charCameraTiltSpeed, 0.5f, 1.0f, 20.0f);
        }

        SectionSeparator();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.8f, 1.0f, 1.0f));
        ImGui::Text("  Head Bob");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        PropertyLabel("Enabled");
        ImGui::Checkbox("##lecharhb", &state.charHeadBobEnabled);
        if (state.charHeadBobEnabled) {
            PropertyLabel("Speed");
            ImGui::DragFloat("##lecharhbs", &state.charHeadBobSpeed, 0.5f, 2.0f, 30.0f);
            PropertyLabel("Amount");
            ImGui::DragFloat("##lecharhba", &state.charHeadBobAmount, 0.005f, 0.0f, 0.2f, "%.3f");
            PropertyLabel("Sway");
            ImGui::DragFloat("##lecharhbw", &state.charHeadBobSway, 0.005f, 0.0f, 0.1f, "%.3f");
        }

        SectionSeparator();

        ImGui::PushStyleColor(ImGuiCol_Text, kAccentDim);
        ImGui::Text("  Body Colors");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        PropertyLabel("Head");
        ImGui::ColorEdit3("##lecolhead", state.charHeadColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        PropertyLabel("Torso");
        ImGui::ColorEdit3("##lecoltorso", state.charTorsoColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        PropertyLabel("Arms");
        ImGui::ColorEdit3("##lecolarms", state.charArmsColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        PropertyLabel("Legs");
        ImGui::ColorEdit3("##lecollegs", state.charLegsColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

        SectionSeparator();

        ImGui::PushStyleColor(ImGuiCol_Text, kAccentDim);
        ImGui::Text("  Character Model");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        PropertyLabel("Scale");
        ImGui::DragFloat("##lecharscale", &state.charModelScale, 0.005f, 0.01f, 1.0f, "%.3f");
    }

    EndSection();
}

// ==================== Level File Operations ====================
void LevelEditorWindow::SaveCurrentLevel(EditorState& state) {
    if (m_currentLevelPath.empty())
        m_currentLevelPath = m_levelsDirectory + "untitled.wtlevel";
    if (LevelFile::Save(m_currentLevelPath, state.scene)) {
        m_unsavedChanges = false;
        LOG_INFO("Level saved: %s", m_currentLevelPath.c_str());
    }
}

void LevelEditorWindow::LoadLevel(const std::string& path, EditorState& state) {
    if (LevelFile::Load(path, state.scene)) {
        m_currentLevelPath = path;
        state.selectedEntity = -1;
        m_unsavedChanges = false;
        state.physicsRebuildRequested = true;
        LOG_INFO("Level loaded: %s", path.c_str());
    }
}

void LevelEditorWindow::NewLevel(EditorState& state) {
    state.scene.Clear();
    state.selectedEntity = -1;
    m_currentLevelPath.clear();
    m_unsavedChanges = false;
    state.physicsRebuildRequested = true;
    LOG_INFO("New level created");
}

// ==================== Shutdown ====================
void LevelEditorWindow::Shutdown() {
    ShutdownImGui();
    g_levelEditor = nullptr;
    m_debugRenderer.Shutdown();
    m_rtv.Reset(); m_dsv.Reset();
    m_backBuffer.Reset(); m_depthBuffer.Reset();
    m_swapChain.Reset();
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    m_open = false;
    LOG_INFO("Level Editor window shutdown");
}

} // namespace WT
