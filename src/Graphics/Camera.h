#pragma once

#include <DirectXMath.h>
#include <cstdlib>

namespace WT {

using namespace DirectX;

class Camera {
public:
    void Init(float fovDegrees, float aspectRatio, float nearZ, float farZ);
    void Update(float mouseDeltaX, float mouseDeltaY);
    void UpdateProjection(float aspectRatio);

    // Getters
    XMMATRIX GetViewMatrix() const;
    XMMATRIX GetProjectionMatrix() const;
    XMFLOAT4X4 GetViewMatrix4x4() const;
    XMFLOAT4X4 GetProjectionMatrix4x4() const;

    XMFLOAT3 GetPosition() const   { return m_position; }
    XMFLOAT3 GetForward() const;
    XMFLOAT3 GetRight() const;
    XMFLOAT3 GetUp() const         { return { 0, 1, 0 }; }
    float GetYaw() const           { return m_yaw; }
    float GetPitch() const         { return m_pitch; }

    void SetPosition(const XMFLOAT3& pos) { m_position = pos; }
    void SetPosition(float x, float y, float z) { m_position = { x, y, z }; }
    void SetSensitivity(float s)   { m_sensitivity = s; }
    void SetRoll(float roll)       { m_roll = roll; }
    float GetRoll() const          { return m_roll; }

    // Screen shake
    void AddScreenShake(float intensity, float duration) {
        m_shakeIntensity = intensity;
        m_shakeDuration = duration;
        m_shakeTimer = duration;
    }
    void UpdateShake(float dt) {
        if (m_shakeTimer > 0.0f) {
            m_shakeTimer -= dt;
            if (m_shakeTimer <= 0.0f) {
                m_shakeTimer = 0.0f;
                m_shakeOffset = { 0, 0, 0 };
            } else {
                float t = m_shakeTimer / m_shakeDuration;
                float amp = m_shakeIntensity * t; // Decaying shake
                m_shakeOffset = {
                    ((rand() / (float)RAND_MAX) * 2.0f - 1.0f) * amp,
                    ((rand() / (float)RAND_MAX) * 2.0f - 1.0f) * amp * 0.6f,
                    0.0f
                };
            }
        }
    }
    XMFLOAT3 GetShakeOffset() const { return m_shakeOffset; }
    bool IsShaking() const { return m_shakeTimer > 0.0f; }

private:
    XMFLOAT3 m_position   = { 0, 0, 0 };
    float    m_yaw         = 0.0f;     // Radians, around Y axis
    float    m_pitch       = 0.0f;     // Radians, around X axis
    float    m_roll        = 0.0f;     // Radians, around Z axis (camera tilt)
    float    m_sensitivity = 0.15f;    // Degrees per pixel of raw input
    float    m_fov         = 70.0f;    // Degrees (stored for reference)
    float    m_nearZ       = 0.1f;
    float    m_farZ        = 500.0f;

    XMFLOAT4X4 m_projMatrix;

    // Screen shake
    float    m_shakeIntensity = 0.0f;
    float    m_shakeDuration  = 0.0f;
    float    m_shakeTimer     = 0.0f;
    XMFLOAT3 m_shakeOffset    = { 0, 0, 0 };
};

} // namespace WT
