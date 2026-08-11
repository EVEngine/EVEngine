#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::physics {

/**
 * Interactive 2D particle fluid (double-density relaxation) in pixel space.
 * Script-owned; independent of Box2D World.
 */
class Fluid {
public:
    explicit Fluid(int capacity = 512);
    ~Fluid();

    Fluid(const Fluid &)            = delete;
    Fluid &operator=(const Fluid &) = delete;

    void update(float dt);

    void  setGravity(float gx, float gy);
    float getGravityX() const { return gravityX_; }
    float getGravityY() const { return gravityY_; }

    /** Interaction / neighbor radius in pixels (default 18). */
    void  setSmoothingRadius(float radius);
    float getSmoothingRadius() const { return h_; }

    /** Target rest density for the relaxation solver (default 4). */
    void  setRestDensity(float density);
    float getRestDensity() const { return restDensity_; }

    /** Pressure stiffness (default 0.5). */
    void  setPressureStiffness(float k);
    float getPressureStiffness() const { return pressureK_; }

    /** Near-pressure (anti-clustering) stiffness (default 0.5). */
    void  setNearPressureStiffness(float k);
    float getNearPressureStiffness() const { return nearPressureK_; }

    void  setViscosity(float viscosity);
    float getViscosity() const { return viscosity_; }

    /** Solver iterations per frame (default 3). */
    void setIterations(int iterations);
    int  getIterations() const { return iterations_; }

    /** Axis-aligned container; particles bounce inside. */
    void setBounds(float x, float y, float w, float h);
    void clearBounds();

    /**
     * Spawn up to `count` particles at (x,y) with initial velocity.
     * Returns number actually added.
     */
    int emit(float x, float y, int count, float vx = 0.f, float vy = 0.f);

    /** Clear all particles. */
    void clear();

    /**
     * Mouse / pointer interaction: positive strength attracts, negative repels.
     * Applied as acceleration within radius (pixels).
     */
    void interactAt(float x, float y, float radius, float strength);

    void  setColor(float r, float g, float b, float a = 1.f);
    float getColorR() const { return colorR_; }
    float getColorG() const { return colorG_; }
    float getColorB() const { return colorB_; }
    float getColorA() const { return colorA_; }

    /** Particle draw size in pixels (default 5). */
    void  setParticleSize(float size);
    float getParticleSize() const { return particleSize_; }

    void draw(graphics::Graphics *gfx);

    int   getCapacity() const { return capacity_; }
    int   getParticleCount() const { return static_cast<int>(particles_.size()); }
    float getParticleX(int index) const;
    float getParticleY(int index) const;
    float getParticleVx(int index) const;
    float getParticleVy(int index) const;

    void destroy();

private:
    struct Particle {
        float x = 0.f, y = 0.f;
        float vx = 0.f, vy = 0.f;
        float density = 0.f;
    };

    void rebuildHash();
    void applyViscosity(float dt);
    void doubleDensityRelaxation();
    void collideBounds();
    bool validIndex(int index) const;
    int64_t cellKey(int cx, int cy) const;

    int   capacity_ = 512;
    float gravityX_ = 0.f;
    float gravityY_ = 980.f;
    float h_ = 18.f;
    float restDensity_ = 4.f;
    float pressureK_ = 0.5f;
    float nearPressureK_ = 0.5f;
    float viscosity_ = 0.12f;
    int   iterations_ = 3;

    bool  hasBounds_ = false;
    float boundX_ = 0.f, boundY_ = 0.f, boundW_ = 0.f, boundH_ = 0.f;

    float interactX_ = 0.f, interactY_ = 0.f;
    float interactRadius_ = 0.f;
    float interactStrength_ = 0.f;

    float colorR_ = 0.25f, colorG_ = 0.55f, colorB_ = 0.95f, colorA_ = 0.85f;
    float particleSize_ = 5.f;

    bool destroyed_ = false;

    std::vector<Particle> particles_;
    std::unordered_map<int64_t, std::vector<int>> hash_;
};

}  // namespace eve::physics
