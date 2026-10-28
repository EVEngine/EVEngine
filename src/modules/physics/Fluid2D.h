#pragma once
#include "common/Export.h"


#include <cstdint>
#include <unordered_map>
#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::physics {

/**
 * @brief Interactive 2D particle fluid (double-density relaxation) in pixel space.
 * Script-owned; independent of Box2D World.
 */
class EVENGINE_API_WORLD Fluid2D {
public:
    /** @brief Fluid 2 d. */
    explicit Fluid2D(int capacity = 512);
    /** @brief Fluid 2 d. */
    ~Fluid2D();

    Fluid2D(const Fluid2D &)            = delete;
    Fluid2D &operator=(const Fluid2D &) = delete;

    /** @brief Updates . */
    void update(float dt);

    /** @brief Sets the gravity. */
    void  setGravity(float gx, float gy);
    /** @brief Returns the gravity x. */
    float getGravityX() const { return gravityX_; }
    /** @brief Returns the gravity y. */
    float getGravityY() const { return gravityY_; }

    /** @brief Interaction / neighbor radius in pixels (default 18). */
    void  setSmoothingRadius(float radius);
    /** @brief Returns the smoothing radius. */
    float getSmoothingRadius() const { return h_; }

    /** @brief Target rest density for the relaxation solver (default 4). */
    void  setRestDensity(float density);
    /** @brief Returns the rest density. */
    float getRestDensity() const { return restDensity_; }

    /** @brief Pressure stiffness (default 0.5). */
    void  setPressureStiffness(float k);
    /** @brief Returns the pressure stiffness. */
    float getPressureStiffness() const { return pressureK_; }

    /** @brief Near-pressure (anti-clustering) stiffness (default 0.5). */
    void  setNearPressureStiffness(float k);
    /** @brief Returns the near pressure stiffness. */
    float getNearPressureStiffness() const { return nearPressureK_; }

    /** @brief Sets the viscosity. */
    void  setViscosity(float viscosity);
    /** @brief Returns the viscosity. */
    float getViscosity() const { return viscosity_; }

    /** @brief Solver iterations per frame (default 3). */
    void setIterations(int iterations);
    /** @brief Returns the iterations. */
    int  getIterations() const { return iterations_; }

    /** @brief Axis-aligned container; particles bounce inside. */
    void setBounds(float x, float y, float w, float h);
    /** @brief Clears bounds. */
    void clearBounds();

    /**
     * @brief Spawn up to `count` particles at (x,y) with initial velocity.
     * Returns number actually added.
     */
    int emit(float x, float y, int count, float vx = 0.f, float vy = 0.f);

    /** @brief Clear all particles. */
    void clear();

    /**
     * @brief Mouse / pointer interaction: positive strength attracts, negative repels.
     * Applied as acceleration within radius (pixels).
     */
    void interactAt(float x, float y, float radius, float strength);

    /** @brief Sets the color. */
    void  setColor(float r, float g, float b, float a = 1.f);
    /** @brief Returns the color r. */
    float getColorR() const { return colorR_; }
    /** @brief Returns the color g. */
    float getColorG() const { return colorG_; }
    /** @brief Returns the color b. */
    float getColorB() const { return colorB_; }
    /** @brief Returns the color a. */
    float getColorA() const { return colorA_; }

    /** @brief Particle draw size in pixels (default 5). */
    void  setParticleSize(float size);
    /** @brief Returns the particle size. */
    float getParticleSize() const { return particleSize_; }

    /** @brief Draws . */
    void draw(graphics::Graphics *gfx);

    /** @brief Returns the capacity. */
    int   getCapacity() const { return capacity_; }
    /** @brief Returns the particle count. */
    int   getParticleCount() const { return static_cast<int>(particles_.size()); }
    /** @brief Returns the particle x. */
    float getParticleX(int index) const;
    /** @brief Returns the particle y. */
    float getParticleY(int index) const;
    /** @brief Returns the particle vx. */
    float getParticleVx(int index) const;
    /** @brief Returns the particle vy. */
    float getParticleVy(int index) const;

    /** @brief Destroys . */
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
