#include "Input.h"
#include "Util/Log.h"
#include <windowsx.h>  // GET_X_LPARAM, GET_Y_LPARAM

namespace WT {

void Input::Init(HWND hwnd) {
    m_hwnd = hwnd;

    // Register for raw mouse input
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;  // HID_USAGE_PAGE_GENERIC
    rid.usUsage     = 0x02;  // HID_USAGE_GENERIC_MOUSE
    rid.dwFlags     = 0;
    rid.hwndTarget  = hwnd;

    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        LOG_ERROR("Failed to register raw input device");
    }
}

void Input::ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (wParam < MAX_KEYS) m_keysCurrent[wParam] = true;
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (wParam < MAX_KEYS) m_keysCurrent[wParam] = false;
            break;
        case WM_LBUTTONDOWN:
            m_mouseLeft = true;
            break;
        case WM_LBUTTONUP:
            m_mouseLeft = false;
            break;
        case WM_RBUTTONDOWN:
            m_mouseRight = true;
            break;
        case WM_RBUTTONUP:
            m_mouseRight = false;
            break;
        case WM_MOUSEWHEEL:
            m_scrollDelta += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / 120.0f;
            break;
        case WM_MOUSEMOVE:
            m_mousePos.x = static_cast<float>(GET_X_LPARAM(lParam));
            m_mousePos.y = static_cast<float>(GET_Y_LPARAM(lParam));
            break;
        case WM_INPUT: {
            UINT dataSize = 0;
            GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dataSize, sizeof(RAWINPUTHEADER));
            if (dataSize > 0) {
                BYTE buffer[128];
                if (dataSize <= sizeof(buffer)) {
                    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer, &dataSize, sizeof(RAWINPUTHEADER)) == dataSize) {
                        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer);
                        if (raw->header.dwType == RIM_TYPEMOUSE) {
                            m_rawDeltaX += static_cast<float>(raw->data.mouse.lLastX);
                            m_rawDeltaY += static_cast<float>(raw->data.mouse.lLastY);
                        }
                    }
                }
            }
            break;
        }
    }
}

void Input::Update() {
    // Copy current -> previous
    m_keysPrevious   = m_keysCurrent;
    m_mouseLeftPrev  = m_mouseLeft;
    m_mouseRightPrev = m_mouseRight;

    // Transfer accumulated raw deltas
    m_mouseDelta.x = m_rawDeltaX;
    m_mouseDelta.y = m_rawDeltaY;
    m_rawDeltaX = 0.0f;
    m_rawDeltaY = 0.0f;
    m_scrollDelta = 0.0f;

    // Re-center cursor if locked
    if (m_cursorLocked && m_hwnd) {
        RECT rect;
        GetClientRect(m_hwnd, &rect);
        POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
        ClientToScreen(m_hwnd, &center);
        SetCursorPos(center.x, center.y);
    }
}

void Input::SetCursorLocked(bool locked) {
    m_cursorLocked = locked;
    if (locked) {
        ShowCursor(FALSE);
        // Clip cursor to window
        if (m_hwnd) {
            RECT rect;
            GetClientRect(m_hwnd, &rect);
            POINT ul = { rect.left, rect.top };
            POINT lr = { rect.right, rect.bottom };
            ClientToScreen(m_hwnd, &ul);
            ClientToScreen(m_hwnd, &lr);
            RECT clipRect = { ul.x, ul.y, lr.x, lr.y };
            ClipCursor(&clipRect);
        }
    } else {
        ShowCursor(TRUE);
        ClipCursor(nullptr);
    }
}

} // namespace WT
