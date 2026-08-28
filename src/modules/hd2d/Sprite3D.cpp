#include "hd2d/Hd2d.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
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
        float u = (i == 0 || i == 3) ? u0_ : u1_;
        float v = (i < 2) ? v0_ : v1_;
        if (flipX_) u = (u0_ + u1_) - u;
        if (flipY_) v = (v0_ + v1_) - v;
        uv[size_t(i) * 2u] = u;
        uv[size_t(i) * 2u + 1u] = v;
    }
    // Positions are required by updateMeshVertices; pass the unchanged base.
    gfx_->updateMeshVertices(quad_, kQuadPos.data(), kQuadNrm.data(), uv.data(),
                             int(kQuadPos.size() / 3), nullptr, 0);
}

void Sprite3D::setTexture(graphics::Texture *texture) {
    texture_ = texture;
    if (renderable_) renderable_->setTexture(texture);
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
    if (columns <= 0 || rows <= 0)
        throw eve::Exception("Sprite3D.setFrameGrid: columns and rows must be > 0");
    gridCols_ = columns;
    gridRows_ = rows;
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
    if (fps <= 0.f) throw eve::Exception("Sprite3D.play: fps must be > 0");
    anim_ = {start, end, fps, 0.f, true};
    setFrameIndex(start);
}
void Sprite3D::stop() { anim_.playing = false; }
bool Sprite3D::isPlaying() const { return anim_.playing; }

void Sprite3D::update(float dt) {
    if (anim_.playing && anim_.fps > 0.f) {
        anim_.clock += dt;
        const float frameDur = 1.f / anim_.fps;
        if (anim_.clock >= frameDur) {
            int steps = int(anim_.clock / frameDur);
            anim_.clock -= float(steps) * frameDur;
            const int span = anim_.end - anim_.start + 1;
            if (span > 0) {
                frameIndex_ = anim_.start + (frameIndex_ - anim_.start + steps) % span;
                setFrameIndex(frameIndex_);
            }
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
    const glm::vec3 pos(x_, y_, z_);
    // The billboard quad's front face is -Z, so it faces the camera when its
    // +Z points away from the eye. Cylindrical billboard: rotate around world Y
    // only, keeping the sprite upright (no pitch) so it faces the camera
    // squarely while standing vertical.
    glm::vec3 away = pos - eye;
    if (glm::dot(away, away) < 1e-6f) away = glm::vec3(0.f, 0.f, 1.f);
    away = glm::normalize(away);
    const float yaw = std::atan2(away.x, away.z);
    renderable_->setRotation(yaw, 0.f, 0.f);
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
    if (renderable_) renderable_->setTint(tintR_, tintG_, tintB_, tintA_);
}
void Sprite3D::setVisible(bool visible) {
    visible_ = visible;
    if (renderable_) renderable_->setVisible(visible);
}
bool Sprite3D::getVisible() const { return visible_; }

}  // namespace eve::hd2d
