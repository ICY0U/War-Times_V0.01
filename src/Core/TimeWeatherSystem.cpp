#include "TimeWeatherSystem.h"
#include <algorithm>

namespace WT {

// ---- Math helpers ----
static float Clamp01(float x) { return (x < 0.0f) ? 0.0f : (x > 1.0f) ? 1.0f : x; }
static float SmoothStep(float edge0, float edge1, float x) {
    float t = Clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}
static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// ---- Time-of-day constants (in hours) ----
static constexpr float kDawn      = 5.0f;    // Start of sunrise
static constexpr float kSunrise   = 6.5f;    // Sunrise complete
static constexpr float kMorning   = 8.0f;    // Full day
static constexpr float kEvening   = 17.0f;   // Start of golden hour
static constexpr float kSunset    = 19.5f;   // Sunset
static constexpr float kDusk      = 20.5f;   // End of twilight
static constexpr float kNightFull = 21.0f;   // Full night

// ---- Color palettes for the day ----
struct SkyPalette {
    XMFLOAT3 zenith;
    XMFLOAT3 horizon;
    XMFLOAT3 ground;
    XMFLOAT3 sunColor;
    float    sunIntensity;
    XMFLOAT3 ambientColor;
    float    ambientIntensity;
    XMFLOAT3 fogColor;
    float    fogDensity;
};

// Midnight
static const SkyPalette kNight = {
    {0.01f, 0.01f, 0.04f},   // zenith — very dark blue
    {0.02f, 0.02f, 0.06f},   // horizon
    {0.01f, 0.01f, 0.02f},   // ground
    {0.3f, 0.3f, 0.5f},      // moon-ish blue light
    0.08f,                     // sun intensity (moonlight)
    {0.02f, 0.02f, 0.05f},   // ambient
    0.15f,                     // ambient intensity
    {0.01f, 0.01f, 0.03f},   // fog color
    0.0008f                    // fog density
};

// Dawn/Dusk
static const SkyPalette kDawnDusk = {
    {0.15f, 0.10f, 0.25f},   // zenith — purple-blue
    {0.8f, 0.35f, 0.15f},    // horizon — orange
    {0.15f, 0.08f, 0.05f},   // ground
    {1.0f, 0.6f, 0.3f},      // sun color — warm orange
    0.6f,
    {0.2f, 0.12f, 0.08f},    // ambient
    0.35f,
    {0.5f, 0.3f, 0.15f},     // fog — warm
    0.001f
};

// Day (noon)
static const SkyPalette kDay = {
    {0.15f, 0.35f, 0.65f},   // zenith — bright blue
    {0.4f, 0.55f, 0.7f},     // horizon — light blue
    {0.18f, 0.15f, 0.12f},   // ground — earthy
    {1.0f, 0.95f, 0.85f},    // sun color — slightly warm white
    1.0f,
    {0.25f, 0.3f, 0.35f},    // ambient — blue-ish
    0.5f,
    {0.5f, 0.55f, 0.6f},     // fog — desaturated sky
    0.0005f
};

// Helpers to lerp palettes
static SkyPalette LerpPalette(const SkyPalette& a, const SkyPalette& b, float t) {
    SkyPalette r;
    auto L3 = [](const XMFLOAT3& x, const XMFLOAT3& y, float t) -> XMFLOAT3 {
        return { x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t };
    };
    r.zenith           = L3(a.zenith, b.zenith, t);
    r.horizon          = L3(a.horizon, b.horizon, t);
    r.ground           = L3(a.ground, b.ground, t);
    r.sunColor         = L3(a.sunColor, b.sunColor, t);
    r.sunIntensity     = Lerp(a.sunIntensity, b.sunIntensity, t);
    r.ambientColor     = L3(a.ambientColor, b.ambientColor, t);
    r.ambientIntensity = Lerp(a.ambientIntensity, b.ambientIntensity, t);
    r.fogColor         = L3(a.fogColor, b.fogColor, t);
    r.fogDensity       = Lerp(a.fogDensity, b.fogDensity, t);
    return r;
}

// ====================================================================
// Update
// ====================================================================
void TimeWeatherSystem::Update(float dt, TimeWeatherSettings& settings) {
    // ---- Advance time of day ----
    if (settings.dayNightEnabled && !settings.paused) {
        // 1 real minute = daySpeed in-game hours  =>  hours per second = daySpeed / 60
        settings.timeOfDay += (settings.daySpeed / 60.0f) * dt;
        if (settings.timeOfDay >= 24.0f) settings.timeOfDay -= 24.0f;
        if (settings.timeOfDay < 0.0f)   settings.timeOfDay += 24.0f;
    }

    // ---- Weather transition ----
    if (settings.currentWeather != settings.targetWeather) {
        settings.weatherTransition -= settings.weatherTransSpeed * dt;
        if (settings.weatherTransition <= 0.0f) {
            settings.currentWeather     = settings.targetWeather;
            settings.weatherTransition  = 1.0f;
        }
    } else {
        settings.weatherTransition = 1.0f;
    }

    float time = settings.timeOfDay;

    // ---- Compute sun direction + elevation ----
    XMFLOAT3 sunDir;
    float elevation;
    ComputeSunPosition(time, settings.sunAzimuth, sunDir, elevation);
    m_output.sunDirection = sunDir;

    // ---- Select sky palette based on time of day ----
    SkyPalette palette;
    // Determine blend between night / dawn / day / dusk / night
    if (time < kDawn) {
        // Full night
        palette = kNight;
    } else if (time < kSunrise) {
        float t = SmoothStep(kDawn, kSunrise, time);
        palette = LerpPalette(kNight, kDawnDusk, t);
    } else if (time < kMorning) {
        float t = SmoothStep(kSunrise, kMorning, time);
        palette = LerpPalette(kDawnDusk, kDay, t);
    } else if (time < kEvening) {
        // Full day
        palette = kDay;
    } else if (time < kSunset) {
        float t = SmoothStep(kEvening, kSunset, time);
        palette = LerpPalette(kDay, kDawnDusk, t);
    } else if (time < kDusk) {
        float t = SmoothStep(kSunset, kDusk, time);
        palette = LerpPalette(kDawnDusk, kNight, t);
    } else {
        // Full night
        palette = kNight;
    }

    // ---- Apply weather modifications ----
    float cloudCovTarget = 0.0f, fogMultTarget = 1.0f, windMultTarget = 1.0f;
    XMFLOAT3 cloudColorTarget = {1.0f, 1.0f, 1.0f};

    // Current weather params
    float cc1, fm1, wm1;
    XMFLOAT3 ccol1;
    ComputeWeatherParams(settings.currentWeather, cc1, fm1, wm1, ccol1);

    // Target weather params (if transitioning)
    float cc2, fm2, wm2;
    XMFLOAT3 ccol2;
    ComputeWeatherParams(settings.targetWeather, cc2, fm2, wm2, ccol2);

    float wt = Clamp01(1.0f - settings.weatherTransition);
    cloudCovTarget   = Lerp(cc1, cc2, wt);
    fogMultTarget    = Lerp(fm1, fm2, wt);
    windMultTarget   = Lerp(wm1, wm2, wt);
    cloudColorTarget = Lerp3(ccol1, ccol2, wt);

    // ---- Assemble output ----
    m_output.skyZenith   = palette.zenith;
    m_output.skyHorizon  = palette.horizon;
    m_output.skyGround   = palette.ground;
    m_output.skyBrightness = palette.sunIntensity;  // Overall brightness

    m_output.sunColor     = palette.sunColor;
    m_output.sunIntensity = palette.sunIntensity;

    m_output.ambientColor     = palette.ambientColor;
    m_output.ambientIntensity = palette.ambientIntensity;

    // Fog: base from palette, modified by weather
    m_output.fogColor   = palette.fogColor;
    m_output.fogDensity = palette.fogDensity * fogMultTarget;

    // Clouds
    m_output.cloudCoverage = Clamp01(0.3f + cloudCovTarget);  // Base cloud coverage 0.3
    m_output.cloudSpeed    = settings.windSpeed * windMultTarget;
    m_output.cloudColor    = cloudColorTarget;

    // Time info
    m_output.timeOfDay = settings.timeOfDay;
    m_output.isNight   = (elevation < -5.0f);
}

// ====================================================================
// ComputeSunPosition — maps hour -> sun direction
// ====================================================================
void TimeWeatherSystem::ComputeSunPosition(float timeOfDay, float azimuth,
                                            XMFLOAT3& outDir, float& elevation)
{
    // Map time to sun elevation: 0h=-90°, 6h=0° (sunrise), 12h=max (90°), 18h=0° (sunset), 24h=-90°
    float hourAngle = (timeOfDay - 12.0f) * 15.0f;  // 15°/hour, 0 at noon
    elevation = 90.0f - fabsf(hourAngle); // Peaks at 90° at noon

    // Clamp sun elevation
    float elevRad = elevation * (3.14159265f / 180.0f);
    float azimRad = azimuth * (3.14159265f / 180.0f);

    // Spherical -> direction vector (pointing TO the sun)
    float cosElev = cosf(elevRad);
    outDir.x = cosElev * sinf(azimRad);
    outDir.y = sinf(elevRad);
    outDir.z = cosElev * cosf(azimRad);

    // Normalize
    float len = sqrtf(outDir.x * outDir.x + outDir.y * outDir.y + outDir.z * outDir.z);
    if (len > 0.001f) {
        outDir.x /= len;
        outDir.y /= len;
        outDir.z /= len;
    }
}

// ====================================================================
// ComputeSkyColors — not used directly (palette-based instead)
// ====================================================================
void TimeWeatherSystem::ComputeSkyColors(float /*elevation*/, float /*weatherBlend*/) {
    // Reserved for future more complex sky color computation
}

// ====================================================================
// ComputeWeatherParams — output weather-specific values
// ====================================================================
void TimeWeatherSystem::ComputeWeatherParams(WeatherType weather,
                                              float& outCloud, float& outFogMul,
                                              float& outWindMul, XMFLOAT3& outCloudColor)
{
    switch (weather) {
        case WeatherType::Clear:
            outCloud     = 0.0f;
            outFogMul    = 1.0f;
            outWindMul   = 1.0f;
            outCloudColor = {1.0f, 1.0f, 1.0f};
            break;
        case WeatherType::Cloudy:
            outCloud     = 0.35f;
            outFogMul    = 1.5f;
            outWindMul   = 1.2f;
            outCloudColor = {0.8f, 0.8f, 0.8f};
            break;
        case WeatherType::Overcast:
            outCloud     = 0.65f;
            outFogMul    = 2.5f;
            outWindMul   = 1.0f;
            outCloudColor = {0.5f, 0.5f, 0.55f};
            break;
        case WeatherType::Foggy:
            outCloud     = 0.3f;
            outFogMul    = 6.0f;
            outWindMul   = 0.5f;
            outCloudColor = {0.7f, 0.7f, 0.7f};
            break;
        case WeatherType::Rainy:
            outCloud     = 0.7f;
            outFogMul    = 3.5f;
            outWindMul   = 2.0f;
            outCloudColor = {0.35f, 0.35f, 0.4f};
            break;
        default:
            outCloud     = 0.0f;
            outFogMul    = 1.0f;
            outWindMul   = 1.0f;
            outCloudColor = {1.0f, 1.0f, 1.0f};
            break;
    }
}

// ====================================================================
// ApplyToEditorState — push computed values into the engine's CB arrays
// ====================================================================
void TimeWeatherSystem::ApplyToEditorState(
    float* sunDirection, float& sunIntensity, float* sunColor,
    float* ambientColor, float& ambientIntensity,
    float* fogColor, float& fogDensity,
    float* skyZenith, float* skyHorizon, float* skyGround,
    float& skyBrightness,
    float& cloudCoverage, float& cloudSpeed, float* cloudColor) const
{
    sunDirection[0] = m_output.sunDirection.x;
    sunDirection[1] = m_output.sunDirection.y;
    sunDirection[2] = m_output.sunDirection.z;
    sunIntensity    = m_output.sunIntensity;
    sunColor[0]     = m_output.sunColor.x;
    sunColor[1]     = m_output.sunColor.y;
    sunColor[2]     = m_output.sunColor.z;

    ambientColor[0]  = m_output.ambientColor.x;
    ambientColor[1]  = m_output.ambientColor.y;
    ambientColor[2]  = m_output.ambientColor.z;
    ambientIntensity = m_output.ambientIntensity;

    fogColor[0]  = m_output.fogColor.x;
    fogColor[1]  = m_output.fogColor.y;
    fogColor[2]  = m_output.fogColor.z;
    fogDensity   = m_output.fogDensity;

    skyZenith[0]  = m_output.skyZenith.x;
    skyZenith[1]  = m_output.skyZenith.y;
    skyZenith[2]  = m_output.skyZenith.z;
    skyHorizon[0] = m_output.skyHorizon.x;
    skyHorizon[1] = m_output.skyHorizon.y;
    skyHorizon[2] = m_output.skyHorizon.z;
    skyGround[0]  = m_output.skyGround.x;
    skyGround[1]  = m_output.skyGround.y;
    skyGround[2]  = m_output.skyGround.z;
    skyBrightness = m_output.skyBrightness;

    cloudCoverage  = m_output.cloudCoverage;
    cloudSpeed     = m_output.cloudSpeed;
    cloudColor[0]  = m_output.cloudColor.x;
    cloudColor[1]  = m_output.cloudColor.y;
    cloudColor[2]  = m_output.cloudColor.z;
}

} // namespace WT
