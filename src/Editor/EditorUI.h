#pragma once

#include <d3d11.h>
#include <Windows.h>

namespace WT {

class EditorUI {
public:
    bool Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // Call in WndProc — returns true if ImGui consumed the event
    bool HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Frame lifecycle
    void BeginFrame();
    void EndFrame();  // Issues ImGui draw calls

    // Is the editor visible?
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool v) { m_visible = v; }
    void ToggleVisible() { m_visible = !m_visible; }

    // Does ImGui want keyboard/mouse? (suppress game input when true)
    bool WantsKeyboard() const;
    bool WantsMouse() const;

private:
    bool m_visible     = true;
    bool m_initialized = false;
};

} // namespace WT
