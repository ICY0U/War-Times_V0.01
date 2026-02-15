#include "Camera.h"
#include "Util/MathHelpers.h"

namespace WT {

void Camera::Init(float fovDegrees, float aspectRatio, float nearZ, float farZ) {
    m_fov   = fovDegrees;
    m_nearZ = nearZ;
    m_farZ  = farZ;
    m_yaw   = 0.0f;
    m_pitch = 0.0f;
    UpdateProjection(aspectRatio);
}

void Camera::Update(float mouseDeltaX, float mouseDeltaY) {
    m_yaw   += mouseDeltaX * m_sensitivity * DEG_TO_RAD;
    m_pitch += mouseDeltaY * m_sensitivity * DEG_TO_RAD;

    // Clamp pitch to avoid flipping
    m_pitch = Clamp(m_pitch, -HALF_PI + 0.01f, HALF_PI - 0.01f);

    // Wrap yaw
    if (m_yaw > PI)   m_yaw -= TWO_PI;
    if (m_yaw < -PI)  m_yaw += TWO_PI;
}

void Camera::UpdateProjection(float aspectRatio) {
    XMMATRIX proj = XMMatrixPerspectiveFovLH(ToRadians(m_fov), aspectRatio, m_nearZ, m_farZ);
    XMStoreFloat4x4(&m_projMatrix, proj);
}

XMFLOAT3 Camera::GetForward() const {
    float cosPitch = cosf(m_pitch);
    return {
        sinf(m_yaw) * cosPitch,
        -sinf(m_pitch),
        cosf(m_yaw) * cosPitch
    };
}

XMFLOAT3 Camera::GetRight() const {
    // Right is perpendicular to forward on the XZ plane
    return {
        cosf(m_yaw),
        0.0f,
        -sinf(m_yaw)
    };
}

XMMATRIX Camera::GetViewMatrix() const {
    XMVECTOR pos = XMLoadFloat3(&m_position);
    XMFLOAT3 fwd = GetForward();
    XMVECTOR forward = XMLoadFloat3(&fwd);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    XMMATRIX view = XMMatrixLookToLH(pos, forward, up);

    // Apply roll (camera tilt) if non-zero
    if (fabsf(m_roll) > 0.0001f) {
        XMMATRIX rollMat = XMMatrixRotationZ(m_roll);
        view = view * rollMat;
    }

    return view;
}

XMMATRIX Camera::GetProjectionMatrix() const {
    return XMLoadFloat4x4(&m_projMatrix);
}

XMFLOAT4X4 Camera::GetViewMatrix4x4() const {
    XMFLOAT4X4 view;
    XMStoreFloat4x4(&view, GetViewMatrix());
    return view;
}

XMFLOAT4X4 Camera::GetProjectionMatrix4x4() const {
    return m_projMatrix;
}

} // namespace WT
