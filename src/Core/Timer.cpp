#include "Timer.h"

namespace WT {

Timer::Timer() {
    QueryPerformanceFrequency(&m_frequency);
    QueryPerformanceCounter(&m_startTime);
    m_previousTime = m_startTime;
    m_currentTime  = m_startTime;
}

void Timer::Reset() {
    QueryPerformanceCounter(&m_startTime);
    m_previousTime = m_startTime;
    m_currentTime  = m_startTime;
    m_deltaTime    = 0.0f;
    m_totalTime    = 0.0f;
    m_accumulator  = 0.0f;
    m_frameCount   = 0;
    m_fps          = 0;
    m_fpsTimer     = 0.0f;
}

void Timer::Tick() {
    QueryPerformanceCounter(&m_currentTime);

    m_deltaTime = static_cast<float>(m_currentTime.QuadPart - m_previousTime.QuadPart)
                / static_cast<float>(m_frequency.QuadPart);

    // Clamp to avoid spiral-of-death (e.g. breakpoint pause)
    if (m_deltaTime > 0.25f) {
        m_deltaTime = 0.25f;
    }

    m_totalTime = static_cast<float>(m_currentTime.QuadPart - m_startTime.QuadPart)
                / static_cast<float>(m_frequency.QuadPart);

    m_previousTime = m_currentTime;

    // Accumulate for fixed timestep
    m_accumulator += m_deltaTime;

    // FPS counter
    m_frameCount++;
    m_fpsTimer += m_deltaTime;
    if (m_fpsTimer >= 1.0f) {
        m_fps = m_frameCount;
        m_frameCount = 0;
        m_fpsTimer -= 1.0f;
    }
}

bool Timer::ShouldDoFixedUpdate() {
    if (m_accumulator >= m_fixedDeltaTime) {
        m_accumulator -= m_fixedDeltaTime;
        return true;
    }
    return false;
}

} // namespace WT
