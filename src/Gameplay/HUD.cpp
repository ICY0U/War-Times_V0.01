#include "HUD.h"
#include "WeaponSystem.h"
#include "Util/Log.h"

// ImGui for 2D overlay drawing
#include "imgui.h"

#include <cstdio>

namespace WT {

void HUD::Init() {
    LOG_INFO("HUD initialized");
}

void HUD::Shutdown() {
    LOG_INFO("HUD shutdown");
}

// ============================================================
// Main Draw
// ============================================================

void HUD::Draw(const WeaponSystem& weapon, int screenWidth, int screenHeight) {
    float sw = static_cast<float>(screenWidth);
    float sh = static_cast<float>(screenHeight);
    float cx = sw * 0.5f;
    float cy = sh * 0.5f;

    // Create a fullscreen transparent ImGui window for HUD overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(sw, sh));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoBringToFrontOnFocus |
                              ImGuiWindowFlags_NoFocusOnAppearing |
                              ImGuiWindowFlags_NoDocking;

    if (ImGui::Begin("##HUD", nullptr, flags)) {
        if (m_settings.showCrosshair) DrawCrosshair(weapon, cx, cy);
        if (m_settings.showHitMarker) DrawHitMarker(weapon, cx, cy);
        if (m_settings.showReloadBar && weapon.IsReloading()) DrawReloadBar(weapon, cx, cy);
        if (m_settings.showAmmo) DrawAmmoCounter(weapon, sw, sh);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

// ============================================================
// Crosshair
// ============================================================

void HUD::DrawCrosshair(const WeaponSystem& weapon, float cx, float cy) {
    const auto& ws = weapon.GetSettings();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float size = ws.crosshairSize;
    float gap  = ws.crosshairGap;
    float thick = ws.crosshairThickness;
    ImU32 col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(ws.crosshairColor[0], ws.crosshairColor[1],
               ws.crosshairColor[2], ws.crosshairColor[3] * m_settings.hudOpacity));

    // Expand gap when firing (dynamic crosshair)
    if (weapon.IsFiring()) {
        gap += 4.0f;
        size += 3.0f;
    }
    if (weapon.IsADS()) {
        gap *= 0.3f;
        size *= 0.6f;
    }

    // Top line
    dl->AddLine(ImVec2(cx, cy - gap - size), ImVec2(cx, cy - gap), col, thick);
    // Bottom line
    dl->AddLine(ImVec2(cx, cy + gap), ImVec2(cx, cy + gap + size), col, thick);
    // Left line
    dl->AddLine(ImVec2(cx - gap - size, cy), ImVec2(cx - gap, cy), col, thick);
    // Right line
    dl->AddLine(ImVec2(cx + gap, cy), ImVec2(cx + gap + size, cy), col, thick);

    // Center dot
    if (ws.crosshairDot) {
        dl->AddRectFilled(ImVec2(cx - 1.0f, cy - 1.0f), ImVec2(cx + 1.0f, cy + 1.0f), col);
    }
}

// ============================================================
// Hit Marker
// ============================================================

void HUD::DrawHitMarker(const WeaponSystem& weapon, float cx, float cy) {
    if (!weapon.IsHitMarkerActive()) return;

    const auto& ws = weapon.GetSettings();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float s = ws.hitMarkerSize;
    ImU32 col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(ws.hitMarkerColor[0], ws.hitMarkerColor[1],
               ws.hitMarkerColor[2], ws.hitMarkerColor[3] * m_settings.hudOpacity));

    float thick = 2.0f;

    // Four diagonal lines forming an X
    dl->AddLine(ImVec2(cx - s, cy - s), ImVec2(cx - s * 0.4f, cy - s * 0.4f), col, thick);
    dl->AddLine(ImVec2(cx + s, cy - s), ImVec2(cx + s * 0.4f, cy - s * 0.4f), col, thick);
    dl->AddLine(ImVec2(cx - s, cy + s), ImVec2(cx - s * 0.4f, cy + s * 0.4f), col, thick);
    dl->AddLine(ImVec2(cx + s, cy + s), ImVec2(cx + s * 0.4f, cy + s * 0.4f), col, thick);
}

// ============================================================
// Ammo Counter
// ============================================================

void HUD::DrawAmmoCounter(const WeaponSystem& weapon, float screenW, float screenH) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float opacity = m_settings.hudOpacity;

    // Position: bottom-right corner
    float x = screenW - 180.0f;
    float y = screenH - 70.0f;

    // Background panel
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.4f * opacity));
    dl->AddRectFilled(ImVec2(x - 10.0f, y - 10.0f),
                       ImVec2(x + 170.0f, y + 55.0f), bgCol, 4.0f);

    // Weapon name
    ImU32 labelCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.7f, 0.7f, opacity));
    dl->AddText(ImVec2(x, y), labelCol, WeaponTypeName(weapon.GetCurrentWeapon()));

    // Ammo count: current / max
    char ammoText[64];
    snprintf(ammoText, sizeof(ammoText), "%d / %d", weapon.GetCurrentAmmo(), weapon.GetMaxAmmo());

    ImU32 ammoCol;
    if (weapon.GetCurrentAmmo() == 0)
        ammoCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.2f, 0.2f, opacity));
    else if (weapon.GetCurrentAmmo() <= weapon.GetMaxAmmo() / 4)
        ammoCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.7f, 0.2f, opacity));
    else
        ammoCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, opacity));

    // Large font: just scale the position and draw big text
    ImFont* font = ImGui::GetFont();
    dl->AddText(font, 28.0f, ImVec2(x, y + 15.0f), ammoCol, ammoText);

    // Reserve ammo
    char reserveText[32];
    snprintf(reserveText, sizeof(reserveText), "| %d", weapon.GetReserveAmmo());
    dl->AddText(font, 16.0f, ImVec2(x + 110.0f, y + 22.0f), labelCol, reserveText);

    // Reloading indicator
    if (weapon.IsReloading()) {
        ImU32 reloadCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.9f, 0.3f, opacity));
        dl->AddText(ImVec2(x + 40.0f, y - 5.0f), reloadCol, "RELOADING");
    }
}

// ============================================================
// Reload Progress Bar
// ============================================================

void HUD::DrawReloadBar(const WeaponSystem& weapon, float cx, float cy) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float opacity = m_settings.hudOpacity;

    float barWidth  = 120.0f;
    float barHeight = 4.0f;
    float barY = cy + 30.0f;
    float barX = cx - barWidth * 0.5f;

    float progress = weapon.GetReloadProgress();

    // Background
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.2f, 0.2f, 0.6f * opacity));
    dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth, barY + barHeight), bgCol, 2.0f);

    // Fill
    ImU32 fillCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.9f, 0.3f, 0.9f * opacity));
    dl->AddRectFilled(ImVec2(barX, barY),
                       ImVec2(barX + barWidth * progress, barY + barHeight), fillCol, 2.0f);
}

} // namespace WT
