#pragma once
#include "../Renderer/D3DContext.h"
#include "../Renderer/SpriteBatch.h"
#include "../Util/Color.h"
#include <vector>
#include <random>
#include <memory>

namespace pfd {

struct Particle {
    float  x, y;       // position
    float  vx, vy;     // velocity
    float  r, g, b, a; // color
    float  life;        // remaining seconds
    float  maxLife;
    float  size;
    uint32_t flags;     // bit 0 = isGlow
    bool   active;      // is this particle slot in use?
};

class ParticleSystem {
public:
    bool Initialize(ID3D11Device* device);
    void EmitBurst(float x, float y, util::Color color, int count = 20);
    void EmitContinuous(float x, float y, util::Color color);
    void EmitSparks(float x, float y, util::Color color);
    void Update(float dt);
    void Draw(SpriteBatch& batch);

    void SetGravity(float g) { m_gravity = g; }
    void SetEmberMode(bool on) { m_emberMode = on; }
    size_t Count() const;
    void Clear();

    static constexpr size_t MAX_PARTICLES = 50000;

private:
    Particle& AllocateParticle();
    
    std::vector<Particle> m_particles;
    Particle m_dummyParticle{};   // sink for AllocateParticle when pool is full
    size_t m_nextSearchIdx = 0;   // ring-search cursor for free slot
    std::mt19937 m_rng{std::random_device{}()};
    float m_gravity = 300.0f;
    bool  m_emberMode = false;
    size_t m_activeCount = 0;
};

} // namespace pfd
