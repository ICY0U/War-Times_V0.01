#pragma once

#include <DirectXMath.h>
#include <vector>
#include <cstdlib>
#include <algorithm>

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
