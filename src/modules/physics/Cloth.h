#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::physics {

/**
 * Interactive 2D cloth — Verlet particles + distance constraints.
 * Pixel space (same convention as Box2D World). Script-owned.
 */
class Cloth {
public:
    /**
     * @param cols grid columns (>= 2)
     * @param rows grid rows (>= 2)
     * @param spacing particle spacing in pixels
     * @param originX top-left particle X (pixels)
     * @param originY top-left particle Y (pixels)
     */
    Cloth(int cols, int rows, float spacing, float originX, float originY);
    ~Cloth();

    Cloth(const Cloth &)            = delete;
    Cloth &operator=(const Cloth &) = delete;

    void update(float dt);

    void  setGravity(float gx, float gy);
    float getGravityX() const { return gravityX_; }
    float getGravityY() const { return gravityY_; }

    /** Constraint relaxation strength in [0,1] (default 0.85). */
    void  setStiffness(float stiffness);
    float getStiffness() const { return stiffness_; }

    /** Constraint solver iterations per substep (default 4). */
    void setIterations(int iterations);
    int  getIterations() const { return iterations_; }

    /** Damping applied to Verlet velocity [0,1] (default 0.01). */
    void  setDamping(float damping);
    float getDamping() const { return damping_; }

    /** Axis-aligned walls; particles bounce inside. Disabled if w/h <= 0. */
    void setBounds(float x, float y, float w, float h);
    void clearBounds();

    void pin(int index);
    void unpin(int index);
    void pinTopRow();
    bool isPinned(int index) const;

    /**
     * Grab nearest free particle within radius (pixels).
     * Returns particle index, or -1 if none.
     */
    int  grabAt(float x, float y, float radius = 24.f);
    void moveGrab(float x, float y);
    void releaseGrab();
    bool isGrabbing() const { return grabIndex_ >= 0; }
    int  getGrabIndex() const { return grabIndex_; }

    /** Uniform wind / force impulse applied this frame (pixels/s² * mass). */
    void applyForce(float fx, float fy);

    void  setColor(float r, float g, float b, float a = 1.f);
    float getColorR() const { return colorR_; }
    float getColorG() const { return colorG_; }
    float getColorB() const { return colorB_; }
    float getColorA() const { return colorA_; }

    void draw(graphics::Graphics *gfx);

    int   getCols() const { return cols_; }
    int   getRows() const { return rows_; }
    int   getParticleCount() const { return static_cast<int>(particles_.size()); }
    float getParticleX(int index) const;
    float getParticleY(int index) const;
    void  setParticlePosition(int index, float x, float y);

    float getSpacing() const { return spacing_; }
    float getOriginX() const { return originX_; }
    float getOriginY() const { return originY_; }

    void destroy();

private:
    struct Particle {
        float x = 0.f, y = 0.f;
        float px = 0.f, py = 0.f;
        bool  pinned = false;
    };
    struct Link {
        int   a = 0, b = 0;
        float rest = 0.f;
    };

    void rebuildLinks();
    void integrate(float dt);
    void solveConstraints();
    void collideBounds();
    bool validIndex(int index) const;

    int   cols_ = 0;
    int   rows_ = 0;
    float spacing_ = 10.f;
    float originX_ = 0.f;
    float originY_ = 0.f;

    float gravityX_ = 0.f;
    float gravityY_ = 980.f;
    float stiffness_ = 0.85f;
    float damping_   = 0.01f;
    int   iterations_ = 4;

    bool  hasBounds_ = false;
    float boundX_ = 0.f, boundY_ = 0.f, boundW_ = 0.f, boundH_ = 0.f;

    int   grabIndex_ = -1;
    float grabX_ = 0.f, grabY_ = 0.f;
    float forceX_ = 0.f, forceY_ = 0.f;

    float colorR_ = 0.75f, colorG_ = 0.82f, colorB_ = 0.95f, colorA_ = 1.f;

    bool destroyed_ = false;

    std::vector<Particle> particles_;
    std::vector<Link>     links_;
};

}  // namespace eve::physics
