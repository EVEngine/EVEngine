#include "hd2d/Hd2d.h"

#include "common/Assert.h"
#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <limits>
#include <vector>

namespace eve::hd2d {

namespace {

// Unit quad in the XY plane, normal -Z, full 0..1 UV. Spans [-0.5,0.5] so the
// Renderable3D scale maps it to the requested world-space size.
const std::vector<float> kQuadPos = {-0.5f, -0.5f, 0.f, 0.5f, -0.5f, 0.f,
                                     0.5f,  0.5f,  0.f, -0.5f, 0.5f, 0.f};
const std::vector<float> kQuadNrm = {0.f, 0.f, -1.f, 0.f, 0.f, -1.f,
                                     0.f, 0.f, -1.f, 0.f, 0.f, -1.f};
const std::vector<uint32_t> kQuadIdx = {0, 2, 1, 0, 3, 2};
const std::vector<float> kQuadUv = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};

}  // namespace

// ---------------------------------------------------------------------------
// Sprite3D
// ---------------------------------------------------------------------------

Sprite3D::Sprite3D() = default;
Sprite3D::~Sprite3D() {
    if (renderable_) renderable_->setVisible(false);
}

void Sprite3D::buildQuad(graphics::Graphics *gfx) {
    if (!gfx) throw eve::Exception("Sprite3D: null graphics");
    gfx_ = gfx;
    quad_ = gfx->newMeshFromArrays(kQuadPos.data(), kQuadNrm.data(), kQuadUv.data(),
                                   int(kQuadPos.size() / 3), kQuadIdx.data(),
                                   int(kQuadIdx.size()));
    if (!quad_) throw eve::Exception("Sprite3D: quad mesh creation failed");

    renderable_ = graphics::Renderable3D::create();
    if (!renderable_) throw eve::Exception("Sprite3D: Renderable3D create failed");
    renderable_->setMesh(quad_);
    auto* material = gfx->newMaterial();
    material->setSurfaceMode("masked");
    material->setReceiveLight(false);
    material->setAlbedoTexture(texture_);
    material->setTint(tintR_, tintG_, tintB_, tintA_);
    renderable_->setMaterial(material);
    // 2D sprites are self-lit (HD-2D characters), not shaded by 3D lights.
    renderable_->setReceiveLight(false);
    renderable_->setScale(width_, height_, 1.f);
    renderable_->setPosition(x_, y_, z_);
    renderable_->setTint(tintR_, tintG_, tintB_, tintA_);
    renderable_->setVisible(visible_);
    updateFrameUv();
}

void Sprite3D::updateFrameUv() {
    if (!gfx_ || !quad_) return;
    std::vector<float> uv(8);
    for (int i = 0; i < 4; ++i) {
        // The visible front is -Z, so local +X is the viewer's left.
        float u = (i == 0 || i == 3) ? u1_ : u0_;
        // Image row zero is at the top; the first two vertices are the feet.
        float v = (i < 2) ? v1_ : v0_;
        if (flipX_) u = (u0_ + u1_) - u;
        if (flipY_) v = (v0_ + v1_) - v;
        uv[size_t(i) * 2u] = u;
        uv[size_t(i) * 2u + 1u] = v;
    }
    auto positions = kQuadPos;
    for (int i = 0; i < 4; ++i) {
        // Local +X projects left on the visible -Z face.
        positions[size_t(i) * 3u] += pivotX_ - 0.5f;
        positions[size_t(i) * 3u + 1u] += pivotY_ - 0.5f;
    }
    gfx_->updateMeshVertices(quad_, positions.data(), kQuadNrm.data(), uv.data(), int(kQuadPos.size() / 3), nullptr, 0);
}

void Sprite3D::setTexture(graphics::Texture *texture) {
    texture_ = texture;
    if (renderable_) {
        renderable_->setTexture(texture);
        renderable_->getMaterial()->setAlbedoTexture(texture);
    }
}
graphics::Texture *Sprite3D::getTexture() const { return texture_; }

void Sprite3D::setFrame(float u0, float v0, float u1, float v1) {
    u0_ = u0;
    v0_ = v0;
    u1_ = u1;
    v1_ = v1;
    updateFrameUv();
}
void Sprite3D::getFrame(float &u0, float &v0, float &u1, float &v1) const {
    u0 = u0_;
    v0 = v0_;
    u1 = u1_;
    v1 = v1_;
}
void Sprite3D::setFlipX(bool flip) {
    flipX_ = flip;
    updateFrameUv();
}
void Sprite3D::setFlipY(bool flip) {
    flipY_ = flip;
    updateFrameUv();
}

void Sprite3D::setFrameGrid(int columns, int rows) {
    if (columns <= 0 || rows <= 0 || columns > std::numeric_limits<int>::max() / rows)
        throw eve::Exception("Sprite3D.setFrameGrid: columns and rows must be > 0");
    gridCols_ = columns;
    gridRows_ = rows;
    stop();
    frameIndex_ = std::min(frameIndex_, gridCols_ * gridRows_ - 1);
    setFrameIndex(frameIndex_);
}
int Sprite3D::getFrameGridColumns() const { return gridCols_; }
int Sprite3D::getFrameGridRows() const { return gridRows_; }
int Sprite3D::getFrameCount() const { return gridCols_ * gridRows_; }

void Sprite3D::setFrameIndex(int index) {
    frameIndex_ = std::max(0, std::min(index, gridCols_ * gridRows_ - 1));
    const int cx = frameIndex_ % gridCols_;
    const int cy = frameIndex_ / gridCols_;
    const float u0 = float(cx) / float(gridCols_);
    const float u1 = float(cx + 1) / float(gridCols_);
    const float v0 = float(cy) / float(gridRows_);
    const float v1 = float(cy + 1) / float(gridRows_);
    setFrame(u0, v0, u1, v1);
}
int Sprite3D::getFrameIndex() const { return frameIndex_; }

void Sprite3D::play(int start, int end, float fps) {
    if (start < 0 || end < start || end >= gridCols_ * gridRows_)
        throw eve::Exception("Sprite3D.play: frame range [%d,%d] out of grid", start, end);
    if (!std::isfinite(fps) || fps <= 0.f) throw eve::Exception("Sprite3D.play: fps must be finite and > 0");
    anim_ = {start, end, fps, 0.f, true};
    setFrameIndex(start);
}
void Sprite3D::stop() { anim_.playing = false; }
bool Sprite3D::isPlaying() const { return anim_.playing; }

void Sprite3D::update(float dt) {
    if (anim_.playing && std::isfinite(dt) && dt > 0.f) {
        const int span = anim_.end - anim_.start + 1;
        // Reduce in floating point before converting to int. A long suspension
        // or a large finite dt must never overflow the integral frame counter.
        const double frames = double(anim_.clock) + double(dt) * double(anim_.fps);
        const double whole  = std::floor(frames);
        anim_.clock         = float(frames - whole);
        if (whole >= 1.0) {
            const int steps  = int(std::fmod(whole, double(span)));
            const int offset = std::clamp(frameIndex_ - anim_.start, 0, span - 1);
            setFrameIndex(anim_.start + int((int64_t(offset) + steps) % span));
        }
    }
    orientToCamera();
}

void Sprite3D::setCamera(graphics::Camera3D *camera) {
    camera_ = camera;
    orientToCamera();
}

void Sprite3D::orientToCamera() {
    if (!renderable_ || !camera_) return;
    const auto &cam = *camera_->data();
    const glm::vec3 eye(cam.eyeX, cam.eyeY, cam.eyeZ);
    glm::vec3       forward = glm::vec3(cam.targetX, cam.targetY, cam.targetZ) - eye;
    const float     length2 = glm::dot(forward, forward);
    if (!std::isfinite(length2) || length2 < 1e-12f) return;
    forward /= std::sqrt(length2);
    glm::vec3   right        = glm::cross(forward, glm::vec3(cam.upX, cam.upY, cam.upZ));
    const float rightLength2 = glm::dot(right, right);
    if (!std::isfinite(rightLength2) || rightLength2 < 1e-12f) return;
    right /= std::sqrt(rightLength2);
    // Use the camera basis, not eye-to-sprite: all sprites remain parallel to
    // the image plane, including off-center sprites and rolled cameras.
    glm::mat4 rotation(1.f);
    rotation[0] = glm::vec4(-right, 0.f);
    rotation[1] = glm::vec4(glm::cross(right, forward), 0.f);
    rotation[2] = glm::vec4(forward, 0.f);
    float yaw, pitch, roll;
    glm::extractEulerAngleYXZ(rotation, yaw, pitch, roll);
    renderable_->setRotation(yaw, pitch, roll);
}

void Sprite3D::setPivot(float x, float y) {
    EV_PARAM_CHECK((std::isfinite(x) && std::isfinite(y)), "Sprite3D pivot must be finite");
    pivotX_ = x;
    pivotY_ = y;
    updateFrameUv();
}

void Sprite3D::setPosition(float x, float y, float z) {
    x_ = x;
    y_ = y;
    z_ = z;
    if (renderable_) {
        renderable_->setPosition(x, y, z);
        orientToCamera();
    }
}
float Sprite3D::getPositionX() const { return x_; }
float Sprite3D::getPositionY() const { return y_; }
float Sprite3D::getPositionZ() const { return z_; }

void Sprite3D::setSize(float width, float height) {
    if (width <= 0.f || height <= 0.f)
        throw eve::Exception("Sprite3D.setSize: width and height must be > 0");
    width_ = width;
    height_ = height;
    if (renderable_) renderable_->setScale(width, height, 1.f);
}
float Sprite3D::getWidth() const { return width_; }
float Sprite3D::getHeight() const { return height_; }

void Sprite3D::setTint(float r, float g, float b, float a) {
    tintR_ = std::max(0.f, std::min(1.f, r));
    tintG_ = std::max(0.f, std::min(1.f, g));
    tintB_ = std::max(0.f, std::min(1.f, b));
    tintA_ = std::max(0.f, std::min(1.f, a));
    if (renderable_) {
        renderable_->setTint(tintR_, tintG_, tintB_, tintA_);
        renderable_->getMaterial()->setTint(tintR_, tintG_, tintB_, tintA_);
    }
}
void Sprite3D::setVisible(bool visible) {
    visible_ = visible;
    if (renderable_) renderable_->setVisible(visible);
}
bool Sprite3D::getVisible() const { return visible_; }

}  // namespace eve::hd2d
