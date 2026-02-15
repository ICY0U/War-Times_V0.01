#pragma once

#include <DirectXMath.h>
#include <d3d11.h>
#include <cstdint>

namespace WT {

using namespace DirectX;

class WeaponSystem;
class Renderer;

// ============================================================
// HUD — 2D overlay for crosshair, ammo, hit markers
// Uses ImGui's draw list for 2D rendering on top of the scene.
// ============================================================

struct HUDSettings {
    bool  showCrosshair    = true;
    bool  showAmmo         = true;
    bool  showHitMarker    = true;
    bool  showReloadBar    = true;
    float hudOpacity       = 1.0f;
};

class HUD {
public:
    void Init();
    void Shutdown();

    // Draw HUD elements (call inside ImGui frame, after scene rendering)
    void Draw(const WeaponSystem& weapon, int screenWidth, int screenHeight);

    HUDSettings& GetSettings() { return m_settings; }
    const HUDSettings& GetSettings() const { return m_settings; }

private:
    void DrawCrosshair(const WeaponSystem& weapon, float cx, float cy);
    void DrawAmmoCounter(const WeaponSystem& weapon, float screenW, float screenH);
    void DrawHitMarker(const WeaponSystem& weapon, float cx, float cy);
    void DrawReloadBar(const WeaponSystem& weapon, float cx, float cy);

    HUDSettings m_settings;
};

} // namespace WT
