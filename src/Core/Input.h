#pragma once

#include <Windows.h>
#include <DirectXMath.h>
#include <array>

namespace WT {

using namespace DirectX;

// Maximum keys we track
constexpr int MAX_KEYS = 256;

class Input {
public:
    void Init(HWND hwnd);
    void ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void Update();  // Call once per frame to update previous-state arrays

    // Keyboard
    bool IsKeyDown(int vk) const    { return m_keysCurrent[vk]; }
    bool IsKeyUp(int vk) const      { return !m_keysCurrent[vk]; }
    bool IsKeyPressed(int vk) const { return m_keysCurrent[vk] && !m_keysPrevious[vk]; }  // Just pressed this frame
    bool IsKeyReleased(int vk) const{ return !m_keysCurrent[vk] && m_keysPrevious[vk]; }

    // Mouse
    XMFLOAT2 GetMouseDelta() const  { return m_mouseDelta; }
    XMFLOAT2 GetMousePosition() const { return m_mousePos; }
    bool IsLeftMouseDown() const    { return m_mouseLeft; }
    bool IsRightMouseDown() const   { return m_mouseRight; }
    bool IsLeftMousePressed() const { return m_mouseLeft && !m_mouseLeftPrev; }
    bool IsRightMousePressed() const{ return m_mouseRight && !m_mouseRightPrev; }
    float GetScrollDelta() const    { return m_scrollDelta; }

    // Cursor locking
    void SetCursorLocked(bool locked);
    bool IsCursorLocked() const { return m_cursorLocked; }
    void ToggleCursorLock() { SetCursorLocked(!m_cursorLocked); }

private:
    HWND m_hwnd = nullptr;

    // Keyboard state
    std::array<bool, MAX_KEYS> m_keysCurrent  = {};
    std::array<bool, MAX_KEYS> m_keysPrevious = {};

    // Mouse state
    XMFLOAT2 m_mouseDelta = { 0.0f, 0.0f };
    XMFLOAT2 m_mousePos   = { 0.0f, 0.0f };
    bool  m_mouseLeft     = false;
    bool  m_mouseRight    = false;
    bool  m_mouseLeftPrev = false;
    bool  m_mouseRightPrev= false;
    float m_scrollDelta   = 0.0f;

    // Raw input accumulation (since we may get multiple raw input messages per frame)
    float m_rawDeltaX = 0.0f;
    float m_rawDeltaY = 0.0f;

    bool  m_cursorLocked  = false;
};

} // namespace WT
