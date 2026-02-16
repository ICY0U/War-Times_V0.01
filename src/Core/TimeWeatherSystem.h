#pragma once
// ============================================================
// TimeWeatherSystem — Day/Night cycle + Weather for D3D11 engine
// Drives sky colors, sun direction, fog, ambient, cloud density.
// ============================================================

#include <DirectXMath.h>
#include <cmath>

namespace WT {

using namespace DirectX;

// ---- Weather presets ----
enum class WeatherType : int {
    Clear = 0,
    Cloudy,
    Overcast,
    Foggy,
    Rainy,
    Count
};

inline const char* WeatherTypeName(WeatherType w) {
    switch (w) {
        case WeatherType::Clear:    return "Clear";
        case WeatherType::Cloudy:   return "Cloudy";
        case WeatherType::Overcast: return "Overcast";
        case WeatherType::Foggy:    return "Foggy";
        case WeatherType::Rainy:    return "Rainy";
        default:                    return "Unknown";
    }
}

// ---- Day/Night + Weather settings ----
struct TimeWeatherSettings {
    // Day/Night cycle
    bool  dayNightEnabled   = false;
    float timeOfDay         = 12.0f;    // 0-24 hours (12 = noon)
    float daySpeed          = 1.0f;     // Multiplier (1 = 1 in-game hour per real minute)
    bool  paused            = false;    // Pause the clock

    // Weather
    WeatherType currentWeather  = WeatherType::Clear;
    WeatherType targetWeather   = WeatherType::Clear;
    float weatherTransition     = 1.0f;  // 0 = fully current, 1 = fully target
    float weatherTransSpeed     = 0.1f;  // Transition speed (per second)

    // Wind (affects clouds, particles, rain)
    float windDirection     = 0.0f;      // Degrees (0 = north/+Z)
    float windSpeed         = 1.0f;      // 0-5 scale

    // Manual overrides (when day/night is off, these are used directly)
    float sunAngle          = 45.0f;     // Sun elevation above horizon in degrees
    float sunAzimuth        = 135.0f;    // Sun compass bearing in degrees
};

// ---- Computed output from the time/weather system ----
struct TimeWeatherOutput {
    // Sun
    XMFLOAT3 sunDirection;        // Normalized, pointing TO the sun
    float    sunIntensity;
    XMFLOAT3 sunColor;

    // Sky
    XMFLOAT3 skyZenith;
    XMFLOAT3 skyHorizon;
    XMFLOAT3 skyGround;
    float    skyBrightness;

    // Ambient
    XMFLOAT3 ambientColor;
    float    ambientIntensity;

    // Fog
    XMFLOAT3 fogColor;
    float    fogDensity;

    // Clouds
    float cloudCoverage;
    float cloudSpeed;
    XMFLOAT3 cloudColor;

    // Time info
    float timeOfDay;          // Current normalized time (0-24)
    bool  isNight;            // Convenience for gameplay
};

// ============================================================
// TimeWeatherSystem — computes sky/light params from time + weather
// ============================================================

class TimeWeatherSystem {
public:
    void Update(float dt, TimeWeatherSettings& settings);

    // Get the computed output (valid after Update)
    const TimeWeatherOutput& GetOutput() const { return m_output; }

    // Apply computed values to editor state arrays (call from Application::Update)
    void ApplyToEditorState(float* sunDirection, float& sunIntensity, float* sunColor,
                            float* ambientColor, float& ambientIntensity,
                            float* fogColor, float& fogDensity,
                            float* skyZenith, float* skyHorizon, float* skyGround,
                            float& skyBrightness,
                            float& cloudCoverage, float& cloudSpeed, float* cloudColor) const;

private:
    // Interpolate sky theme based on time of day
    void ComputeSunPosition(float timeOfDay, float azimuth, XMFLOAT3& outDir, float& elevation);
    void ComputeSkyColors(float elevation, float weatherBlend);
    void ComputeWeatherParams(WeatherType weather, float& outCloud, float& outFog,
                               float& outWind, XMFLOAT3& outCloudColor);

    static XMFLOAT3 Lerp3(const XMFLOAT3& a, const XMFLOAT3& b, float t) {
        return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
    }

    TimeWeatherOutput m_output = {};
};

} // namespace WT
