#ifndef NEW_RENDER_PARTICLERENDERER_H
#define NEW_RENDER_PARTICLERENDERER_H

#include <cstdint>

namespace newcore {

class Camera3D;
class ColumnOcclusion;
class Graphics2D;
class ParticleSystem;
class Texture;

// Port of ParticleSystem::renderSystems + ParticleEmitter::render
// (src/ParticleSystem.cpp:32-137, :201-226). Mutates `ps`: dead particles
// are reaped here, as in src (there is no update tick).
void drawParticles(Graphics2D& g, ParticleSystem& ps, const Camera3D& cam,
                   const ColumnOcclusion& occl, int64_t gameTime, bool grayscale,
                   const Texture (&gibs)[3]);

} // namespace newcore

#endif // NEW_RENDER_PARTICLERENDERER_H
