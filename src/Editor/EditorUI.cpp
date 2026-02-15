#include "EditorUI.h"
#include "Util/Log.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// Forward declare from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace WT {

bool EditorUI::Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context) {
    // Create ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;    // Enable docking
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Keyboard navigation

    // Styling — dark theme with custom tweaks
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 4.0f;
    style.GrabRounding      = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.WindowPadding     = ImVec2(10, 8);
    style.FramePadding      = ImVec2(6, 4);
    style.ItemSpacing       = ImVec2(8, 4);
    style.ItemInnerSpacing  = ImVec2(4, 4);
    style.IndentSpacing     = 16.0f;
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 8.0f;
    style.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
    style.SeparatorTextBorderSize = 2.0f;

    // Refined dark theme — slate undertone
    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]           = ImVec4(0.09f, 0.09f, 0.11f, 0.97f);
    c[ImGuiCol_ChildBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_PopupBg]            = ImVec4(0.10f, 0.10f, 0.13f, 0.98f);
    c[ImGuiCol_Border]             = ImVec4(0.18f, 0.20f, 0.26f, 0.65f);
    c[ImGuiCol_BorderShadow]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]            = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
    c[ImGuiCol_FrameBgHovered]     = ImVec4(0.18f, 0.20f, 0.26f, 1.00f);
    c[ImGuiCol_FrameBgActive]      = ImVec4(0.14f, 0.16f, 0.22f, 1.00f);
    c[ImGuiCol_TitleBg]            = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    c[ImGuiCol_TitleBgActive]      = ImVec4(0.10f, 0.11f, 0.15f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.07f, 0.07f, 0.09f, 0.75f);
    c[ImGuiCol_MenuBarBg]          = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
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
    c[ImGuiCol_SeparatorHovered]   = ImVec4(0.30f, 0.45f, 0.70f, 0.78f);
    c[ImGuiCol_SeparatorActive]    = ImVec4(0.30f, 0.45f, 0.70f, 1.00f);
    c[ImGuiCol_ResizeGrip]         = ImVec4(0.22f, 0.26f, 0.36f, 0.20f);
    c[ImGuiCol_ResizeGripHovered]  = ImVec4(0.30f, 0.45f, 0.70f, 0.67f);
    c[ImGuiCol_ResizeGripActive]   = ImVec4(0.30f, 0.45f, 0.70f, 0.95f);
    c[ImGuiCol_Tab]                = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
    c[ImGuiCol_TabHovered]         = ImVec4(0.25f, 0.32f, 0.48f, 0.80f);
    c[ImGuiCol_TabSelected]        = ImVec4(0.18f, 0.24f, 0.38f, 1.00f);
    c[ImGuiCol_DockingPreview]     = ImVec4(0.30f, 0.45f, 0.70f, 0.70f);
    c[ImGuiCol_DockingEmptyBg]     = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    c[ImGuiCol_TextSelectedBg]     = ImVec4(0.25f, 0.40f, 0.65f, 0.35f);
    c[ImGuiCol_DragDropTarget]     = ImVec4(0.40f, 0.62f, 1.00f, 0.90f);
    c[ImGuiCol_NavHighlight]       = ImVec4(0.40f, 0.62f, 1.00f, 1.00f);
    c[ImGuiCol_PlotLines]          = ImVec4(0.40f, 0.62f, 1.00f, 1.00f);
    c[ImGuiCol_PlotHistogram]      = ImVec4(0.40f, 0.62f, 1.00f, 1.00f);

    // Platform/Renderer init
    if (!ImGui_ImplWin32_Init(hwnd)) {
        LOG_ERROR("ImGui Win32 init failed");
        return false;
    }
    ImGui_ImplDX11_Init(device, context);

    m_initialized = true;
    LOG_INFO("Editor UI initialized (ImGui %s, docking enabled)", IMGUI_VERSION);
    return true;
}

void EditorUI::Shutdown() {
    if (!m_initialized) return;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
    LOG_INFO("Editor UI shutdown");
}

bool EditorUI::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!m_initialized) return false;
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;
    return false;
}

void EditorUI::BeginFrame() {
    if (!m_initialized) return;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void EditorUI::EndFrame() {
    if (!m_initialized) return;
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

bool EditorUI::WantsKeyboard() const {
    if (!m_initialized) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool EditorUI::WantsMouse() const {
    if (!m_initialized) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

} // namespace WT
