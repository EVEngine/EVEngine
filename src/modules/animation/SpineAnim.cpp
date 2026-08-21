#include "animation/SpineAnim.h"

#include "animation/Animation.h"
#include "animation/SpineAtlas.h"
#include "animation/SpineSkeleton.h"
#include "common/Exception.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "graphics/Texture.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace eve::animation {

SpineAnim::SpineAnim(SpineSkeleton *skeleton) : skeleton_(skeleton) {
    if (!skeleton_) throw Exception("SpineAnim: skeleton is null");
}

SpineAnim::~SpineAnim() {
    if (owner_) owner_->unregisterSpineAnim(this);
}

void SpineAnim::setAtlas(SpineAtlas *atlas) {
    atlas_ = atlas;
    pageTextures_.clear();
    if (atlas_) pageTextures_.resize(static_cast<size_t>(atlas_->getPageCount()), nullptr);
}

void SpineAnim::setPageTexture(int pageIndex, graphics::Texture *texture) {
    if (!atlas_) throw Exception("SpineAnim.setPageTexture: atlas is null");
    if (pageIndex < 0 || pageIndex >= atlas_->getPageCount())
        throw Exception("SpineAnim.setPageTexture: page index %d out of range", pageIndex);
    if (pageTextures_.size() < static_cast<size_t>(atlas_->getPageCount()))
        pageTextures_.resize(static_cast<size_t>(atlas_->getPageCount()), nullptr);
    pageTextures_[static_cast<size_t>(pageIndex)] = texture;
}

void SpineAnim::setPageTextureByName(const std::string &pageName, graphics::Texture *texture) {
    if (!atlas_) throw Exception("SpineAnim.setPageTextureByName: atlas is null");
    for (int i = 0; i < atlas_->getPageCount(); ++i) {
        if (atlas_->getPageName(i) == pageName) {
            setPageTexture(i, texture);
            return;
        }
    }
    throw Exception("SpineAnim.setPageTextureByName: unknown page '%s'", pageName.c_str());
}

graphics::Texture *SpineAnim::getPageTexture(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pageTextures_.size())) return nullptr;
    return pageTextures_[static_cast<size_t>(pageIndex)];
}

bool SpineAnim::play(const std::string &animationName) {
    if (!skeleton_ || !skeleton_->getData()) return false;
    int idx = skeleton_->getData()->findAnimation(animationName);
    if (idx < 0) return false;
    animIndex_ = idx;
    animName_  = animationName;
    time_      = 0.f;
    playing_   = true;
    paused_    = false;
    finished_  = false;
    apply();
    return true;
}

void SpineAnim::stop() {
    playing_  = false;
    paused_   = false;
    finished_ = false;
    time_     = 0.f;
    animIndex_ = -1;
    animName_.clear();
}

void SpineAnim::pause() {
    if (playing_) paused_ = true;
}

void SpineAnim::resume() {
    if (playing_ && paused_) paused_ = false;
}

void SpineAnim::setSpeed(float speed) {
    if (speed < 0.f) throw Exception("SpineAnim.setSpeed: speed must be >= 0");
    speed_ = speed;
}

void SpineAnim::setTime(float seconds) {
    if (seconds < 0.f) throw Exception("SpineAnim.setTime: time must be >= 0");
    time_ = seconds;
    if (playing_) apply();
}

float SpineAnim::getAnimationDuration() const {
    if (animIndex_ < 0 || !skeleton_ || !skeleton_->getData()) return 0.f;
    return skeleton_->getData()->getAnimationDuration(animIndex_);
}

void SpineAnim::setPosition(float x, float y) {
    x_ = x;
    y_ = y;
}

void SpineAnim::setScale(float sx, float sy) {
    scaleX_ = sx;
    scaleY_ = sy;
}

void SpineAnim::setColor(float r, float g, float b, float a) {
    r_ = r;
    g_ = g;
    b_ = b;
    a_ = a;
}

float SpineAnim::sampleFloat(const std::vector<SpineSkeletonData::FloatKey> &keys, float time,
                             float fallback) {
    if (keys.empty()) return fallback;
    if (time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        const auto &a = keys[i];
        const auto &b = keys[i + 1];
        if (time > b.time) continue;
        if (a.stepped || b.time <= a.time) return a.value;
        float t = (time - a.time) / (b.time - a.time);
        return a.value + (b.value - a.value) * t;
    }
    return keys.back().value;
}

void SpineAnim::sampleTranslate(const std::vector<SpineSkeletonData::TranslateKey> &keys,
                                float time, float &x, float &y) {
    if (keys.empty()) return;
    if (time <= keys.front().time) {
        x = keys.front().x;
        y = keys.front().y;
        return;
    }
    if (time >= keys.back().time) {
        x = keys.back().x;
        y = keys.back().y;
        return;
    }
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        const auto &a = keys[i];
        const auto &b = keys[i + 1];
        if (time > b.time) continue;
        if (a.stepped || b.time <= a.time) {
            x = a.x;
            y = a.y;
            return;
        }
        float t = (time - a.time) / (b.time - a.time);
        x       = a.x + (b.x - a.x) * t;
        y       = a.y + (b.y - a.y) * t;
        return;
    }
    x = keys.back().x;
    y = keys.back().y;
}

void SpineAnim::sampleScale(const std::vector<SpineSkeletonData::ScaleKey> &keys, float time,
                            float &x, float &y) {
    if (keys.empty()) return;
    if (time <= keys.front().time) {
        x = keys.front().x;
        y = keys.front().y;
        return;
    }
    if (time >= keys.back().time) {
        x = keys.back().x;
        y = keys.back().y;
        return;
    }
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        const auto &a = keys[i];
        const auto &b = keys[i + 1];
        if (time > b.time) continue;
        if (a.stepped || b.time <= a.time) {
            x = a.x;
            y = a.y;
            return;
        }
        float t = (time - a.time) / (b.time - a.time);
        x       = a.x + (b.x - a.x) * t;
        y       = a.y + (b.y - a.y) * t;
        return;
    }
    x = keys.back().x;
    y = keys.back().y;
}

std::string SpineAnim::sampleAttachment(const std::vector<SpineSkeletonData::AttachmentKey> &keys,
                                        float time, const std::string &fallback) {
    if (keys.empty()) return fallback;
    std::string cur = fallback;
    for (const auto &k : keys) {
        if (k.time > time) break;
        cur = k.name;
    }
    return cur;
}

void SpineAnim::apply() {
    if (!skeleton_ || !skeleton_->getData()) return;
    skeleton_->setToSetupPose();

    if (animIndex_ >= 0) {
        const auto &anim = skeleton_->getData()->animation(animIndex_);
        float t          = time_;
        const float dur  = anim.duration;
        if (loop_ && dur > 0.f) {
            t = std::fmod(t, dur);
            if (t < 0.f) t += dur;
        } else if (!loop_ && dur > 0.f && t > dur) {
            t = dur;
        }

        for (const auto &bt : anim.bones) {
            float x = skeleton_->getBoneLocalX(bt.boneIndex);
            float y = skeleton_->getBoneLocalY(bt.boneIndex);
            float r = skeleton_->getBoneLocalRotation(bt.boneIndex);
            float sx = skeleton_->getBoneLocalScaleX(bt.boneIndex);
            float sy = skeleton_->getBoneLocalScaleY(bt.boneIndex);

            // Spine translate/rotate/scale timelines are typically relative to setup pose
            // for rotate (additive angle) and absolute overrides for translate offset from setup.
            // We treat keys as absolute local values when present (common export style stores
            // absolute). Translate keys are offsets added to setup in official runtime;
            // many JSON exports store absolute. Use: setup + key for translate/scale deviation
            // matching spine-runtimes Timeline apply (translate is relative to setup).
            const auto &setup = skeleton_->getData()->bone(bt.boneIndex);
            if (!bt.translate.empty()) {
                float tx = 0.f, ty = 0.f;
                sampleTranslate(bt.translate, t, tx, ty);
                x = setup.x + tx;
                y = setup.y + ty;
            }
            if (!bt.rotate.empty()) {
                float angle = sampleFloat(bt.rotate, t, 0.f);
                r           = setup.rotation + angle;
            }
            if (!bt.scale.empty()) {
                float kx = 1.f, ky = 1.f;
                sampleScale(bt.scale, t, kx, ky);
                sx = setup.scaleX * kx;
                sy = setup.scaleY * ky;
            }
            skeleton_->setBoneLocalX(bt.boneIndex, x);
            skeleton_->setBoneLocalY(bt.boneIndex, y);
            skeleton_->setBoneLocalRotation(bt.boneIndex, r);
            skeleton_->setBoneLocalScaleX(bt.boneIndex, sx);
            skeleton_->setBoneLocalScaleY(bt.boneIndex, sy);
        }

        for (const auto &st : anim.slots) {
            std::string setupName = skeleton_->getData()->slot(st.slotIndex).attachment;
            std::string name      = sampleAttachment(st.attachment, t, setupName);
            skeleton_->setSlotAttachmentName(st.slotIndex, name);
        }
    }

    skeleton_->updateWorldTransform();
    rebuildDrawSlots();
}

bool SpineAnim::update(float dt) {
    if (dt < 0.f) throw Exception("SpineAnim.update: dt must be >= 0");
    if (!playing_ || paused_) return playing_ || paused_;

    time_ += dt * speed_;
    const float dur = getAnimationDuration();
    if (!loop_ && dur > 0.f && time_ >= dur) {
        time_     = dur;
        finished_ = true;
        playing_  = false;
        apply();
        return false;
    }
    apply();
    return true;
}

void SpineAnim::draw(graphics::Graphics *gfx) {
    if (!gfx) return;
    std::vector<graphics::DrawItem2D> items;
    collectDrawItems(items);
    graphics::RenderSystem::drawItems(*gfx, items, false);
}

void SpineAnim::rebuildDrawSlots() {
    drawSlots_.clear();
    if (!skeleton_) return;

    for (int si = 0; si < skeleton_->getSlotCount(); ++si) {
        const auto *att = skeleton_->getSlotRegion(si);
        if (!att) continue;
        int boneIndex = skeleton_->getData()->slot(si).bone;
        const auto &bone = skeleton_->bones_[static_cast<size_t>(boneIndex)];

        DrawSlot ds;
        ds.regionName = att->path.empty() ? att->name : att->path;

        // Atlas region packed dimensions + original dimensions + rotation flag.
        int  regionIndex = -1;
        float regionW = att->width, regionH = att->height;
        float origW = att->width, origH = att->height;
        bool  rotated = false;
        if (atlas_) {
            regionIndex = atlas_->findRegion(ds.regionName);
            ds.region   = regionIndex;
            if (regionIndex >= 0) {
                ds.page    = atlas_->getRegionPage(regionIndex);
                rotated    = atlas_->getRegionRotate(regionIndex);
                ds.rotated = rotated;
                regionW    = static_cast<float>(atlas_->getRegionWidth(regionIndex));
                regionH    = static_cast<float>(atlas_->getRegionHeight(regionIndex));
                origW      = static_cast<float>(atlas_->getRegionOriginalWidth(regionIndex));
                origH      = static_cast<float>(atlas_->getRegionOriginalHeight(regionIndex));
                int tw = atlas_->getPageWidth(ds.page);
                int th = atlas_->getPageHeight(ds.page);
                auto *tex = getPageTexture(ds.page);
                if (tex) {
                    tw = tex->getWidth();
                    th = tex->getHeight();
                }
                if (tw > 0 && th > 0)
                    atlas_->getRegionUV(regionIndex, tw, th, ds.u0, ds.v0, ds.u1, ds.v1);
            }
        }
        if (origW <= 0.f) origW = att->width;
        if (origH <= 0.f) origH = att->height;

        // Official spine RegionAttachment: scale the attachment rect to the
        // atlas region's PACKED size (regionWidth/regionHeight; for a rotated
        // region these already span origH×origW), rotate the corner offsets
        // around the BONE ORIGIN by the attachment rotation, then translate by
        // the attachment anchor (x, y) and map through the bone matrix.
        const float regionScaleX = (origW > 0.f) ? (att->width / origW) * att->scaleX : att->scaleX;
        const float regionScaleY = (origH > 0.f) ? (att->height / origH) * att->scaleY : att->scaleY;

        const float localX  = -att->width * 0.5f * att->scaleX;
        const float localY  = -att->height * 0.5f * att->scaleY;
        const float localX2 = localX + regionW * regionScaleX;
        const float localY2 = localY + regionH * regionScaleY;

        const float ar = att->rotation * static_cast<float>(M_PI / 180.0);
        const float ac = std::cos(ar);
        const float as = std::sin(ar);

        // Corner (lx, ly) → bone-space → spine world → screen (Y-down).
        auto toScreen = [&](float lx, float ly) -> std::pair<float, float> {
            const float rx = lx * ac - ly * as + att->x;
            const float ry = lx * as + ly * ac + att->y;
            const float wx = bone.a * rx + bone.b * ry + bone.worldX;
            const float wy = bone.c * rx + bone.d * ry + bone.worldY;
            return {x_ + wx * scaleX_, y_ + (flipY_ ? -wy : wy) * scaleY_};
        };

        const auto bl = toScreen(localX, localY);
        const auto br = toScreen(localX2, localY);
        const auto tl = toScreen(localX, localY2);
        const auto tr = toScreen(localX2, localY2);

        // Reconstruct the axis-aligned-then-rotated quad the 2D batcher draws:
        // center = average of corners, w = length of the top edge (UL→UR) in
        // screen space, h = length of the left edge (UL→BL). The batcher maps
        // (u0,v0)→top-left of the quad, which after the Spine Y-up → Y-down
        // flip corresponds to Spine's UL corner; so the rotation angle must be
        // the direction of the top edge, otherwise the sprite is upside down.
        ds.x = (bl.first + br.first + tl.first + tr.first) * 0.25f;
        ds.y = (bl.second + br.second + tl.second + tr.second) * 0.25f;
        const float dx = tr.first - tl.first;
        const float dy = tr.second - tl.second;
        ds.w = std::sqrt(dx * dx + dy * dy);
        const float hx = bl.first - tl.first;
        const float hy = bl.second - tl.second;
        ds.h = std::sqrt(hx * hx + hy * hy);
        ds.rotation = (ds.w > 1e-6f) ? std::atan2(dy, dx) * static_cast<float>(180.0 / M_PI) : 0.f;
        // Slot declaration order == Spine back-to-front draw order.
        ds.order = si;

        drawSlots_.push_back(ds);
    }
}

void SpineAnim::collectDrawItems(std::vector<graphics::DrawItem2D> &out) {
    apply();
    for (const DrawSlot &ds : drawSlots_) {
        graphics::DrawItem2D item;
        // Centered quad at attachment world position
        item.x      = ds.x - ds.w * 0.5f;
        item.y      = ds.y - ds.h * 0.5f;
        item.w      = ds.w;
        item.h      = ds.h;
        item.depthY = ds.y + ds.h * 0.5f;
        item.order  = ds.order;
        item.hasOrder = true;
        item.rotation = ds.rotation;
        item.color  = {r_, g_, b_, a_};
        item.layer  = layer_;
        item.receiveLight = false;
        item.texture = getPageTexture(ds.page);
        item.hasUV  = true;
        item.rotatedUV = ds.rotated;
        item.u0     = ds.u0;
        item.v0     = ds.v0;
        item.u1     = ds.u1;
        item.v1     = ds.v1;
        // A reflected skeleton cannot be represented by rotation plus positive
        // quad extents alone. Reconstructing the reflected quad adds a 180-degree
        // rotation, so regular regions need a V flip to leave only the requested
        // horizontal reflection. Rotated atlas regions exchange the UV axes.
        if (scaleX_ < 0.f) {
            if (ds.rotated)
                std::swap(item.u0, item.u1);
            else
                std::swap(item.v0, item.v1);
        }
        out.push_back(item);
    }
}

void SpineAnim::checkDrawSlot(int index) const {
    if (index < 0 || index >= getDrawSlotCount())
        throw Exception("SpineAnim: draw slot index %d out of range (count=%d)", index,
                        getDrawSlotCount());
}

int SpineAnim::getDrawSlotCount() const { return static_cast<int>(drawSlots_.size()); }

float SpineAnim::getDrawSlotX(int index) const {
    checkDrawSlot(index);
    return drawSlots_[static_cast<size_t>(index)].x;
}
float SpineAnim::getDrawSlotY(int index) const {
    checkDrawSlot(index);
    return drawSlots_[static_cast<size_t>(index)].y;
}
float SpineAnim::getDrawSlotWidth(int index) const {
    checkDrawSlot(index);
    return drawSlots_[static_cast<size_t>(index)].w;
}
float SpineAnim::getDrawSlotHeight(int index) const {
    checkDrawSlot(index);
    return drawSlots_[static_cast<size_t>(index)].h;
}
float SpineAnim::getDrawSlotRotation(int index) const {
    checkDrawSlot(index);
    return drawSlots_[static_cast<size_t>(index)].rotation;
}
int SpineAnim::getDrawSlotPage(int index) const {
    checkDrawSlot(index);
    return drawSlots_[static_cast<size_t>(index)].page;
}
std::string SpineAnim::getDrawSlotRegion(int index) const {
    checkDrawSlot(index);
    return drawSlots_[static_cast<size_t>(index)].regionName;
}

}  // namespace eve::animation
