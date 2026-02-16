#pragma once

#include <DirectXMath.h>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include "Core/Entity.h"

namespace WT {

using namespace DirectX;

// ============================================================
// Particle — a single physics-driven visual element
// Used for debris chunks, impact sparks, dust puffs, etc.
// ============================================================
struct Particle {
    XMFLOAT3 position     = { 0, 0, 0 };
    XMFLOAT3 velocity     = { 0, 0, 0 };
    XMFLOAT3 rotation     = { 0, 0, 0 };    // Euler degrees
    XMFLOAT3 angularVel   = { 0, 0, 0 };    // Degrees/sec
    XMFLOAT3 scale        = { 1, 1, 1 };
    float    color[4]     = { 1, 1, 1, 1 };
    float    lifetime     = 0.0f;
    float    maxLifetime  = 2.0f;
    float    gravity      = 9.8f;
    float    friction     = 0.8f;            // Velocity damping on ground bounce
    float    groundY      = 0.0f;            // Ground plane for bouncing
    bool     alive        = true;

    enum class Type : uint8_t {
        Debris,      // Solid chunk (cube, bounces)
        Spark,       // Bright flash, fast fade
        Dust,        // Slow fade, rises
        Smoke,       // Slow rise, expands
    };
    Type type = Type::Debris;
};

// ============================================================
// ParticleSystem — manages all active particles
// Lightweight CPU physics + render data generation
// ============================================================
class ParticleSystem {
public:
    void Init(float groundY = 0.0f) {
        m_groundY = groundY;
        m_particles.reserve(512);
    }

    // ---- Spawn helpers ----

    // Spawn debris from a destroyed entity
    void SpawnDebris(const XMFLOAT3& center, const XMFLOAT3& entityScale,
                     const float entityColor[4], int count, float debrisScaleFactor) {
        for (int i = 0; i < count; i++) {
            Particle p;
            p.type = Particle::Type::Debris;

            // Random position within entity bounds
            p.position = {
                center.x + RandRange(-entityScale.x * 0.4f, entityScale.x * 0.4f),
                center.y + RandRange(-entityScale.y * 0.3f, entityScale.y * 0.4f),
                center.z + RandRange(-entityScale.z * 0.4f, entityScale.z * 0.4f)
            };

            // Explode outward
            float speed = RandRange(2.0f, 6.0f);
            float angle = RandRange(0.0f, 6.283f);
            p.velocity = {
                cosf(angle) * speed,
                RandRange(2.0f, 5.0f),
                sinf(angle) * speed
            };

            // Random tumble
            p.angularVel = {
                RandRange(-360.0f, 360.0f),
                RandRange(-360.0f, 360.0f),
                RandRange(-180.0f, 180.0f)
            };

            // Scale is fraction of original entity
            float s = debrisScaleFactor * RandRange(0.3f, 1.0f);
            float avgScale = (entityScale.x + entityScale.y + entityScale.z) / 3.0f;
            p.scale = { s * avgScale, s * avgScale, s * avgScale };

            // Slightly varied color from entity
            float colorVar = RandRange(0.8f, 1.1f);
            p.color[0] = entityColor[0] * colorVar;
            p.color[1] = entityColor[1] * colorVar;
            p.color[2] = entityColor[2] * colorVar;
            p.color[3] = 1.0f;

            p.maxLifetime = RandRange(1.5f, 3.0f);
            p.gravity = 12.0f;
            p.friction = 0.5f;
            p.groundY = m_groundY;
            p.alive = true;

            m_particles.push_back(p);
        }
    }

    // Spawn impact sparks at hit point
    void SpawnImpactSparks(const XMFLOAT3& hitPos, const XMFLOAT3& hitNormal, int count = 8) {
        for (int i = 0; i < count; i++) {
            Particle p;
            p.type = Particle::Type::Spark;
            p.position = hitPos;

            // Sparks fly along normal + random spread
            float speed = RandRange(3.0f, 8.0f);
            XMFLOAT3 dir = {
                hitNormal.x + RandRange(-0.5f, 0.5f),
                hitNormal.y + RandRange(-0.3f, 0.5f),
                hitNormal.z + RandRange(-0.5f, 0.5f)
            };
            XMVECTOR d = XMVector3Normalize(XMLoadFloat3(&dir));
            XMStoreFloat3(&p.velocity, d * speed);

            p.scale = { 0.02f, 0.02f, 0.06f }; // Elongated spark
            p.color[0] = 1.0f;
            p.color[1] = RandRange(0.6f, 0.9f);
            p.color[2] = RandRange(0.1f, 0.3f);
            p.color[3] = 1.0f;

            p.maxLifetime = RandRange(0.1f, 0.3f);
            p.gravity = 6.0f;
            p.groundY = m_groundY;
            p.alive = true;

            m_particles.push_back(p);
        }
    }

    // Spawn dust puff at impact
    void SpawnDustPuff(const XMFLOAT3& hitPos, const XMFLOAT3& hitNormal,
                       const float color[4], int count = 5) {
        for (int i = 0; i < count; i++) {
            Particle p;
            p.type = Particle::Type::Dust;
            p.position = {
                hitPos.x + RandRange(-0.1f, 0.1f),
                hitPos.y + RandRange(-0.05f, 0.1f),
                hitPos.z + RandRange(-0.1f, 0.1f)
            };

            // Dust rises slowly outward from normal
            float speed = RandRange(0.3f, 1.0f);
            p.velocity = {
                hitNormal.x * speed + RandRange(-0.3f, 0.3f),
                fabsf(hitNormal.y) * speed + RandRange(0.2f, 0.5f),
                hitNormal.z * speed + RandRange(-0.3f, 0.3f)
            };

            float s = RandRange(0.05f, 0.15f);
            p.scale = { s, s, s };

            // Desaturated version of hit surface color
            float gray = (color[0] + color[1] + color[2]) / 3.0f;
            p.color[0] = gray * 0.8f + 0.2f;
            p.color[1] = gray * 0.8f + 0.2f;
            p.color[2] = gray * 0.8f + 0.15f;
            p.color[3] = 0.6f;

            p.maxLifetime = RandRange(0.5f, 1.2f);
            p.gravity = -0.3f;  // Rises slightly
            p.groundY = m_groundY;
            p.alive = true;

            m_particles.push_back(p);
        }
    }

    // Spawn smoke rising from a damaged entity
    void SpawnSmoke(const XMFLOAT3& center, const XMFLOAT3& entityScale, int count = 3) {
        for (int i = 0; i < count; i++) {
            Particle p;
            p.type = Particle::Type::Smoke;
            p.position = {
                center.x + RandRange(-entityScale.x * 0.3f, entityScale.x * 0.3f),
                center.y + entityScale.y * 0.4f,
                center.z + RandRange(-entityScale.z * 0.3f, entityScale.z * 0.3f)
            };
            p.velocity = { RandRange(-0.2f, 0.2f), RandRange(0.5f, 1.5f), RandRange(-0.2f, 0.2f) };
            float s = RandRange(0.08f, 0.2f);
            p.scale = { s, s, s };
            p.color[0] = 0.3f; p.color[1] = 0.3f; p.color[2] = 0.3f; p.color[3] = 0.5f;
            p.maxLifetime = RandRange(1.0f, 2.5f);
            p.gravity = -0.4f; // Rises
            p.groundY = m_groundY;
            p.alive = true;
            m_particles.push_back(p);
        }
    }

    // Spawn fire/ember particles (bright orange-yellow fading to red)
    void SpawnFireEmbers(const XMFLOAT3& center, const XMFLOAT3& entityScale, int count = 4) {
        for (int i = 0; i < count; i++) {
            Particle p;
            p.type = Particle::Type::Spark;
            p.position = {
                center.x + RandRange(-entityScale.x * 0.3f, entityScale.x * 0.3f),
                center.y + RandRange(-entityScale.y * 0.1f, entityScale.y * 0.3f),
                center.z + RandRange(-entityScale.z * 0.3f, entityScale.z * 0.3f)
            };
            float speed = RandRange(1.5f, 4.0f);
            float angle = RandRange(0.0f, 6.283f);
            p.velocity = { cosf(angle) * speed, RandRange(1.0f, 3.0f), sinf(angle) * speed };
            float s = RandRange(0.02f, 0.06f);
            p.scale = { s, s * 2.0f, s };
            p.color[0] = 1.0f; p.color[1] = RandRange(0.5f, 0.8f); p.color[2] = 0.1f; p.color[3] = 1.0f;
            p.maxLifetime = RandRange(0.3f, 0.8f);
            p.gravity = -1.0f; // Floats up
            p.groundY = m_groundY;
            p.alive = true;
            m_particles.push_back(p);
        }
    }

    // Spawn a small explosion burst (combined debris + sparks + smoke)
    void SpawnExplosion(const XMFLOAT3& center, const XMFLOAT3& entityScale,
                        const float entityColor[4], int debrisCount, float debrisScaleFactor) {
        // Core debris
        SpawnDebris(center, entityScale, entityColor, debrisCount, debrisScaleFactor);
        // Impact sparks burst outward
        XMFLOAT3 up = { 0, 1, 0 };
        SpawnImpactSparks(center, up, debrisCount * 2);
        // Smoke cloud
        SpawnSmoke(center, entityScale, debrisCount);
        // Fire embers
        SpawnFireEmbers(center, entityScale, debrisCount);
    }

    // Spawn downwash / thruster effect beneath a drone
    // Creates spiral helix particles spinning down from propellers
    void SpawnDroneDownwash(const XMFLOAT3& dronePos, float bodyScale, float groundY,
                            float altitude, float speed, float bobPhase) {
        float heightAboveGround = altitude;
        if (heightAboveGround > 6.0f) return;

        float intensity = 1.0f - (heightAboveGround / 6.0f);
        intensity = (std::max)(0.0f, intensity);

        // ---- Tiny wispy spiral particles from each propeller ----
        float armLen = bodyScale * 0.7f;
        for (int prop = 0; prop < 4; prop++) {
            float propAngle = (prop * 1.5708f) + 0.7854f;
            float propX = dronePos.x + std::sin(propAngle) * armLen;
            float propZ = dronePos.z + std::cos(propAngle) * armLen;
            float propY = dronePos.y;

            // 3-5 wisp particles per propeller
            int spiralCount = 3 + static_cast<int>(intensity * 2.0f);
            for (int i = 0; i < spiralCount; i++) {
                Particle p;
                p.type = Particle::Type::Dust;

                float t = RandRange(0.0f, 1.0f);
                float spiralAngle = bobPhase * 8.0f + prop * 1.5708f + t * 18.85f; // ~3 tight turns
                float spiralR = 0.08f + t * 0.2f;  // Tighter spiral
                float startY = propY - t * heightAboveGround * 0.7f;

                p.position = {
                    propX + std::cos(spiralAngle) * spiralR,
                    startY,
                    propZ + std::sin(spiralAngle) * spiralR
                };

                // Tangent velocity (swirling) + gentle downward drift
                float velAngle = spiralAngle + 1.5708f;
                float tangentSpeed = RandRange(0.5f, 1.2f) * intensity;
                float downSpeed = RandRange(0.8f, 2.0f);
                p.velocity = {
                    std::cos(velAngle) * tangentSpeed + (p.position.x - propX) * 0.3f,
                    -downSpeed,
                    std::sin(velAngle) * tangentSpeed + (p.position.z - propZ) * 0.3f
                };

                // Very small elongated wisps — wind streaks
                float w = RandRange(0.008f, 0.02f);
                float h = w * RandRange(2.0f, 4.0f);
                p.scale = { w, h, w };

                // White rotor wash wisps
                p.color[0] = 0.9f + RandRange(0.0f, 0.1f);
                p.color[1] = 0.9f + RandRange(0.0f, 0.1f);
                p.color[2] = 0.92f + RandRange(0.0f, 0.08f);
                p.color[3] = 0.18f * intensity;

                p.maxLifetime = RandRange(0.15f, 0.4f);
                p.gravity = -0.05f;
                p.groundY = groundY;
                p.alive = true;
                m_particles.push_back(p);
            }
        }

        // Ground dust kicked up by downwash
        int groundCount = static_cast<int>(3.0f + intensity * 5.0f);
        for (int i = 0; i < groundCount; i++) {
            Particle p;
            p.type = Particle::Type::Dust;

            // Spawn at ground beneath drone with radial spread
            float spread = bodyScale * 1.5f * (1.0f + heightAboveGround * 0.2f);
            float angle = RandRange(0.0f, 6.283f);
            float dist = RandRange(0.2f, spread);
            p.position = {
                dronePos.x + std::cos(angle) * dist,
                groundY + RandRange(0.02f, 0.15f),
                dronePos.z + std::sin(angle) * dist
            };

            // Kick outward + upward — dust being blown away
            float outSpeed = RandRange(0.8f, 2.5f) * intensity;
            p.velocity.x = std::cos(angle) * outSpeed;
            p.velocity.z = std::sin(angle) * outSpeed;
            p.velocity.y = RandRange(0.3f, 1.2f) * intensity;  // Kicked upward

            // Earthy dust particles — varied size
            float s = RandRange(0.02f, 0.06f) * (0.8f + intensity * 0.4f);
            p.scale = { s, s * 0.7f, s };

            // Mix of brown dust and white wisps
            if (RandRange(0.0f, 1.0f) > 0.35f) {
                // Brown/tan dust
                float brownMix = RandRange(0.0f, 1.0f);
                p.color[0] = 0.55f + brownMix * 0.15f;
                p.color[1] = 0.45f + brownMix * 0.1f;
                p.color[2] = 0.3f + brownMix * 0.05f;
            } else {
                // White dust wisps
                p.color[0] = 0.88f + RandRange(0.0f, 0.12f);
                p.color[1] = 0.88f + RandRange(0.0f, 0.12f);
                p.color[2] = 0.9f + RandRange(0.0f, 0.1f);
            }
            p.color[3] = (0.2f + RandRange(0.0f, 0.15f)) * intensity;

            p.maxLifetime = RandRange(0.4f, 1.0f);
            p.gravity = -0.3f;  // Rises (blown upward by air)
            p.groundY = groundY;
            p.alive = true;
            m_particles.push_back(p);
        }

        // Thin streaks when moving fast
        if (speed > 1.5f) {
            int jetCount = static_cast<int>(speed * 0.4f);
            for (int i = 0; i < jetCount && i < 3; i++) {
                Particle p;
                p.type = Particle::Type::Spark;
                p.position = {
                    dronePos.x + RandRange(-bodyScale * 0.5f, bodyScale * 0.5f),
                    dronePos.y - bodyScale * 0.3f,
                    dronePos.z + RandRange(-bodyScale * 0.5f, bodyScale * 0.5f)
                };
                p.velocity = {
                    RandRange(-0.3f, 0.3f),
                    RandRange(-2.0f, -4.0f),
                    RandRange(-0.3f, 0.3f)
                };
                p.scale = { 0.008f, 0.04f, 0.008f }; // Very thin wind streaks
                p.color[0] = 0.7f; p.color[1] = 0.85f; p.color[2] = 1.0f; p.color[3] = 0.18f;
                p.maxLifetime = RandRange(0.08f, 0.2f);
                p.gravity = 1.5f;
                p.groundY = groundY;
                p.alive = true;
                m_particles.push_back(p);
            }
        }
    }

    // ---- Material-aware FX ----

    // Spawn impact FX at hit point based on material type
    void SpawnMaterialImpact(const XMFLOAT3& hitPos, const XMFLOAT3& hitNormal,
                             const float entityColor[4], MaterialType mat) {
        switch (mat) {
        case MaterialType::Wood:
            SpawnDustPuff(hitPos, hitNormal, entityColor, 6);    // More dust
            SpawnImpactSparks(hitPos, hitNormal, 2);             // Fewer sparks
            SpawnFireEmbers(hitPos, { 0.3f, 0.3f, 0.3f }, 2);   // Tiny embers
            break;
        case MaterialType::Metal:
            SpawnImpactSparks(hitPos, hitNormal, 12);            // Lots of sparks
            SpawnDustPuff(hitPos, hitNormal, entityColor, 1);    // Minimal dust
            break;
        case MaterialType::Glass: {
            // Fast white/clear sparks
            for (int i = 0; i < 10; i++) {
                Particle p;
                p.type = Particle::Type::Spark;
                p.position = hitPos;
                float speed = RandRange(5.0f, 12.0f);
                XMFLOAT3 dir = {
                    hitNormal.x + RandRange(-0.7f, 0.7f),
                    hitNormal.y + RandRange(-0.3f, 0.7f),
                    hitNormal.z + RandRange(-0.7f, 0.7f)
                };
                XMVECTOR d = XMVector3Normalize(XMLoadFloat3(&dir));
                XMStoreFloat3(&p.velocity, d * speed);
                p.scale = { 0.015f, 0.015f, 0.015f };
                p.color[0] = 0.9f; p.color[1] = 0.95f; p.color[2] = 1.0f; p.color[3] = 0.8f;
                p.maxLifetime = RandRange(0.15f, 0.4f);
                p.gravity = 8.0f;
                p.groundY = m_groundY;
                p.alive = true;
                m_particles.push_back(p);
            }
            break;
        }
        default: // Concrete
            SpawnImpactSparks(hitPos, hitNormal, 6);
            SpawnDustPuff(hitPos, hitNormal, entityColor, 4);
            break;
        }
    }

    // Spawn material-aware explosion on destroy
    void SpawnMaterialExplosion(const XMFLOAT3& center, const XMFLOAT3& entityScale,
                                const float entityColor[4], int debrisCount,
                                float debrisScaleFactor, MaterialType mat) {
        XMFLOAT3 up = { 0, 1, 0 };
        switch (mat) {
        case MaterialType::Wood: {
            // Warm brown debris + lots of embers + dust
            float woodColor[4] = { entityColor[0] * 0.8f, entityColor[1] * 0.6f, entityColor[2] * 0.4f, 1.0f };
            SpawnDebris(center, entityScale, woodColor, debrisCount, debrisScaleFactor * 0.7f);
            SpawnFireEmbers(center, entityScale, debrisCount * 2);
            SpawnDustPuff(center, up, entityColor, debrisCount);
            SpawnSmoke(center, entityScale, debrisCount / 2 + 1);
            break;
        }
        case MaterialType::Metal: {
            // Metallic gray debris + tons of sparks + minimal dust
            float metalColor[4] = { 0.5f, 0.5f, 0.55f, 1.0f };
            SpawnDebris(center, entityScale, metalColor, debrisCount, debrisScaleFactor);
            SpawnImpactSparks(center, up, debrisCount * 4);
            SpawnSmoke(center, entityScale, debrisCount / 3 + 1);
            break;
        }
        case MaterialType::Glass: {
            // Mostly fast sharp sparks, minimal debris
            float glassColor[4] = { 0.85f, 0.9f, 0.95f, 0.6f };
            SpawnDebris(center, entityScale, glassColor, debrisCount / 2 + 1, debrisScaleFactor * 0.4f);
            for (int i = 0; i < debrisCount * 3; i++) {
                Particle p;
                p.type = Particle::Type::Spark;
                p.position = {
                    center.x + RandRange(-entityScale.x * 0.3f, entityScale.x * 0.3f),
                    center.y + RandRange(-entityScale.y * 0.2f, entityScale.y * 0.3f),
                    center.z + RandRange(-entityScale.z * 0.3f, entityScale.z * 0.3f)
                };
                float speed = RandRange(4.0f, 10.0f);
                float angle = RandRange(0.0f, 6.283f);
                p.velocity = { cosf(angle) * speed, RandRange(2.0f, 6.0f), sinf(angle) * speed };
                p.scale = { 0.01f, 0.01f, 0.01f };
                p.color[0] = 0.9f; p.color[1] = 0.95f; p.color[2] = 1.0f; p.color[3] = 0.9f;
                p.maxLifetime = RandRange(0.2f, 0.5f);
                p.gravity = 10.0f;
                p.groundY = m_groundY;
                p.alive = true;
                m_particles.push_back(p);
            }
            break;
        }
        default: // Concrete
            SpawnExplosion(center, entityScale, entityColor, debrisCount, debrisScaleFactor);
            break;
        }
    }

    // ---- Update ----
    void Update(float dt) {
        for (auto& p : m_particles) {
            if (!p.alive) continue;

            p.lifetime += dt;
            if (p.lifetime >= p.maxLifetime) {
                p.alive = false;
                continue;
            }

            // Gravity
            p.velocity.y -= p.gravity * dt;

            // Move
            p.position.x += p.velocity.x * dt;
            p.position.y += p.velocity.y * dt;
            p.position.z += p.velocity.z * dt;

            // Ground bounce (debris) or stop
            if (p.position.y < p.groundY + p.scale.y * 0.5f) {
                p.position.y = p.groundY + p.scale.y * 0.5f;
                if (p.type == Particle::Type::Debris) {
                    p.velocity.y = -p.velocity.y * p.friction;
                    p.velocity.x *= p.friction;
                    p.velocity.z *= p.friction;
                    p.angularVel.x *= 0.7f;
                    p.angularVel.y *= 0.7f;
                    p.angularVel.z *= 0.7f;
                    // Stop bouncing if very slow
                    if (fabsf(p.velocity.y) < 0.2f) {
                        p.velocity = { 0, 0, 0 };
                        p.angularVel = { 0, 0, 0 };
                    }
                } else {
                    p.velocity.y = 0.0f;
                }
            }

            // Rotation (debris tumbles)
            p.rotation.x += p.angularVel.x * dt;
            p.rotation.y += p.angularVel.y * dt;
            p.rotation.z += p.angularVel.z * dt;

            // Fade out near end of life
            float lifeFrac = p.lifetime / p.maxLifetime;
            if (lifeFrac > 0.7f) {
                float fadeT = (lifeFrac - 0.7f) / 0.3f;
                p.color[3] = (1.0f - fadeT);
            }

            // Dust/smoke: expand over time
            if (p.type == Particle::Type::Dust || p.type == Particle::Type::Smoke) {
                float expand = 1.0f + lifeFrac * 1.5f;
                p.scale.x *= (1.0f + dt * (expand - 1.0f));
                p.scale.y *= (1.0f + dt * (expand - 1.0f));
                p.scale.z *= (1.0f + dt * (expand - 1.0f));
            }
        }

        // Remove dead particles
        m_particles.erase(
            std::remove_if(m_particles.begin(), m_particles.end(),
                [](const Particle& p) { return !p.alive; }),
            m_particles.end()
        );
    }

    // ---- Accessors ----
    const std::vector<Particle>& GetParticles() const { return m_particles; }
    int GetParticleCount() const { return static_cast<int>(m_particles.size()); }
    void Clear() { m_particles.clear(); }
    void SetGroundY(float y) { m_groundY = y; }

private:
    static float RandRange(float lo, float hi) {
        return lo + (rand() / (float)RAND_MAX) * (hi - lo);
    }

    std::vector<Particle> m_particles;
    float m_groundY = 0.0f;
};

} // namespace WT
