#pragma once

#include <Windows.h>

namespace WT {

class Timer {
public:
    Timer();

    void Reset();
    void Tick();

    float DeltaTime() const { return m_deltaTime; }       // Seconds since last Tick
    float TotalTime() const { return m_totalTime; }        // Seconds since Reset
    float FixedDeltaTime() const { return m_fixedDeltaTime; }
    int   FPS() const { return m_fps; }

    // Fixed timestep accumulator — call after Tick(), use in a while loop
    bool  ShouldDoFixedUpdate();

    void SetFixedTimeStep(float dt) { m_fixedDeltaTime = dt; }

private:
    LARGE_INTEGER m_frequency;
    LARGE_INTEGER m_startTime;
    LARGE_INTEGER m_previousTime;
    LARGE_INTEGER m_currentTime;

    float m_deltaTime      = 0.0f;
    float m_totalTime       = 0.0f;
    float m_fixedDeltaTime  = 1.0f / 60.0f;  // 60 Hz simulation
    float m_accumulator     = 0.0f;

    // FPS counter
    int   m_frameCount = 0;
    int   m_fps        = 0;
    float m_fpsTimer   = 0.0f;
};

} // namespace WT
