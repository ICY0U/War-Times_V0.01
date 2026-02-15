#pragma once

#include <Windows.h>
#include <string>
#include <cstdio>
#include <cstdarg>

namespace WT {

enum class LogLevel {
    Info,
    Warning,
    Error
};

inline void Log(LogLevel level, const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    const char* prefix = "";
    switch (level) {
        case LogLevel::Info:    prefix = "[INFO]  "; break;
        case LogLevel::Warning: prefix = "[WARN]  "; break;
        case LogLevel::Error:   prefix = "[ERROR] "; break;
    }

    char output[1100];
    snprintf(output, sizeof(output), "%s%s\n", prefix, buffer);
    OutputDebugStringA(output);

#ifdef _DEBUG
    printf("%s", output);
#endif
}

#define LOG_INFO(fmt, ...)  WT::Log(WT::LogLevel::Info,    fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  WT::Log(WT::LogLevel::Warning, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) WT::Log(WT::LogLevel::Error,   fmt, ##__VA_ARGS__)

// Helper: check HRESULT and log on failure
inline bool CheckHR(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        LOG_ERROR("%s failed with HRESULT 0x%08X", operation, static_cast<unsigned>(hr));
        return false;
    }
    return true;
}

#define HR_CHECK(hr, op) do { if (!WT::CheckHR(hr, op)) return false; } while(0)

} // namespace WT
