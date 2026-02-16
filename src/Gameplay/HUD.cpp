#include "HUD.h"
#include "WeaponSystem.h"
#include "Core/Character.h"
#include "Util/Log.h"

// ImGui for 2D overlay drawing
#include "imgui.h"

#include <cstdio>
#include <cmath>

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

void HUD::Draw(const WeaponSystem& weapon, const Character& character,
               float playerYaw, int screenWidth, int screenHeight) {
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
        // Convert yaw from radians to degrees for compass
        float yawDeg = playerYaw * (180.0f / 3.14159265f);
        if (m_settings.showCompass) DrawCompass(yawDeg, sw);
        if (m_settings.showHealthBar) DrawHealthBar(character, sw, sh);
        DrawDamageVignette(character, sw, sh);
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

// ============================================================
// Compass — top of screen, shows cardinal directions
// ============================================================

void HUD::DrawCompass(float playerYaw, float screenW) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float opacity = m_settings.hudOpacity;

    // Compass bar at very top of screen
    float barY      = 18.0f;
    float barHalfW  = 220.0f;   // Half-width of visible compass strip
    float barCenterX = screenW * 0.5f;

    // Background bar
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.35f * opacity));
    dl->AddRectFilled(ImVec2(barCenterX - barHalfW, barY - 12.0f),
                       ImVec2(barCenterX + barHalfW, barY + 14.0f), bgCol, 3.0f);

    // Compass directions and their yaw angles (0 = +Z = North)
    struct CompassMark {
        const char* label;
        float yaw;     // degrees
        bool major;    // cardinal vs intercardinal
    };
    static const CompassMark marks[] = {
        { "N",  0.0f,   true  },
        { "NE", 45.0f,  false },
        { "E",  90.0f,  true  },
        { "SE", 135.0f, false },
        { "S",  180.0f, true  },
        { "SW", 225.0f, false },
        { "W",  270.0f, true  },
        { "NW", 315.0f, false },
    };

    float pixelsPerDeg = barHalfW / 90.0f;  // 90 degrees fills half the bar

    ImU32 majorCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.95f * opacity));
    ImU32 minorCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.7f, 0.7f, 0.6f * opacity));
    ImU32 tickCol  = ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.5f, 0.5f, 0.4f * opacity));
    ImU32 northCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.3f, 0.3f, 1.0f * opacity));

    // Draw tick marks every 15 degrees
    for (int deg = 0; deg < 360; deg += 15) {
        float diff = static_cast<float>(deg) - playerYaw;
        // Wrap to [-180, 180]
        while (diff > 180.0f)  diff -= 360.0f;
        while (diff < -180.0f) diff += 360.0f;

        float screenX = barCenterX + diff * pixelsPerDeg;
        if (screenX < barCenterX - barHalfW || screenX > barCenterX + barHalfW) continue;

        float tickH = (deg % 90 == 0) ? 6.0f : (deg % 45 == 0) ? 4.0f : 2.0f;
        dl->AddLine(ImVec2(screenX, barY + 4.0f), ImVec2(screenX, barY + 4.0f + tickH), tickCol, 1.0f);
    }

    // Draw cardinal/intercardinal labels
    ImFont* font = ImGui::GetFont();
    for (const auto& m : marks) {
        float diff = m.yaw - playerYaw;
        while (diff > 180.0f)  diff -= 360.0f;
        while (diff < -180.0f) diff += 360.0f;

        float screenX = barCenterX + diff * pixelsPerDeg;
        if (screenX < barCenterX - barHalfW + 10.0f || screenX > barCenterX + barHalfW - 10.0f) continue;

        ImU32 col = (m.label[0] == 'N' && m.label[1] == 0) ? northCol : (m.major ? majorCol : minorCol);
        float fontSize = m.major ? 16.0f : 12.0f;

        ImVec2 textSize = font->CalcTextSizeA(fontSize, 1000.0f, 0.0f, m.label);
        dl->AddText(font, fontSize, ImVec2(screenX - textSize.x * 0.5f, barY - 10.0f), col, m.label);
    }

    // Center indicator (small triangle pointing down)
    ImU32 indicatorCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.9f * opacity));
    dl->AddTriangleFilled(
        ImVec2(barCenterX - 4.0f, barY - 13.0f),
        ImVec2(barCenterX + 4.0f, barY - 13.0f),
        ImVec2(barCenterX, barY - 8.0f),
        indicatorCol);

    // Bearing number
    float bearing = playerYaw;
    while (bearing < 0.0f)   bearing += 360.0f;
    while (bearing >= 360.0f) bearing -= 360.0f;
    char bearingText[16];
    snprintf(bearingText, sizeof(bearingText), "%03.0f", bearing);
    ImVec2 bSize = font->CalcTextSizeA(11.0f, 1000.0f, 0.0f, bearingText);
    dl->AddText(font, 11.0f, ImVec2(barCenterX - bSize.x * 0.5f, barY + 5.0f),
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.8f, 0.8f, 0.7f * opacity)), bearingText);
}

// ============================================================
// Health Bar — bottom-left of screen
// ============================================================

void HUD::DrawHealthBar(const Character& character, float screenW, float screenH) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float opacity = m_settings.hudOpacity;

    float barW = 200.0f;
    float barH = 12.0f;
    float x = 20.0f;
    float y = screenH - 40.0f;

    float hpFrac = character.GetHealth() / character.GetMaxHealth();
    hpFrac = (hpFrac < 0.0f) ? 0.0f : (hpFrac > 1.0f ? 1.0f : hpFrac);

    // Background
    ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.5f * opacity));
    dl->AddRectFilled(ImVec2(x - 2, y - 2), ImVec2(x + barW + 2, y + barH + 2), bgCol, 3.0f);

    // Health color: green→yellow→red
    float r, g;
    if (hpFrac > 0.5f) { r = 1.0f - (hpFrac - 0.5f) * 2.0f; g = 1.0f; }
    else               { r = 1.0f; g = hpFrac * 2.0f; }
    ImU32 fillCol = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, 0.1f, 0.85f * opacity));

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + barW * hpFrac, y + barH), fillCol, 2.0f);

    // Border
    ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.4f, 0.4f, 0.6f * opacity));
    dl->AddRect(ImVec2(x - 1, y - 1), ImVec2(x + barW + 1, y + barH + 1), borderCol, 3.0f);

    // HP text
    ImFont* font = ImGui::GetFont();
    char hpText[32];
    snprintf(hpText, sizeof(hpText), "HP  %.0f / %.0f", character.GetHealth(), character.GetMaxHealth());
    ImU32 labelCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.9f, 0.9f, opacity));
    dl->AddText(font, 13.0f, ImVec2(x, y - 17.0f), labelCol, hpText);
}

// ============================================================
// Damage Vignette — red screen overlay when taking damage
// ============================================================

void HUD::DrawDamageVignette(const Character& character, float screenW, float screenH) {
    float flash = character.GetDamageFlash();
    if (flash <= 0.0f) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float alpha = flash * 0.4f;  // Max 40% opacity

    // Draw screen-edge vignette (4 gradient rects from edges)
    float edgeW = screenW * 0.15f;
    float edgeH = screenH * 0.15f;

    ImU32 colFull = ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.0f, 0.0f, alpha));
    ImU32 colZero = ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.0f, 0.0f, 0.0f));

    // Top edge
    dl->AddRectFilledMultiColor(
        ImVec2(0, 0), ImVec2(screenW, edgeH),
        colFull, colFull, colZero, colZero);
    // Bottom edge
    dl->AddRectFilledMultiColor(
        ImVec2(0, screenH - edgeH), ImVec2(screenW, screenH),
        colZero, colZero, colFull, colFull);
    // Left edge
    dl->AddRectFilledMultiColor(
        ImVec2(0, 0), ImVec2(edgeW, screenH),
        colFull, colZero, colZero, colFull);
    // Right edge
    dl->AddRectFilledMultiColor(
        ImVec2(screenW - edgeW, 0), ImVec2(screenW, screenH),
        colZero, colFull, colFull, colZero);
}

} // namespace WT
