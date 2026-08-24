#include "animation/Animation.h"
#include "animation/AnimClip.h"
#include "animation/AnimClipRegistry.h"
#include "animation/AnimImporter.h"
#include "animation/AnimLattice.h"
#include "animation/AnimLayerMixer.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimSkin.h"
#include "animation/AnimStateMachine.h"
#include "animation/AnimTrail.h"
#include "animation/ControlAnim.h"
#include "animation/ControlPose.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionMatcher.h"
#include "animation/SpineAnim.h"
#include "animation/SpineAtlas.h"
#include "animation/SpineSkeleton.h"
#include "animation/SpineSkeletonData.h"
#include "animation/SpriteAnim.h"
#include "animation/SpriteClip.h"
#include "animation/SpriteSheet.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "graphics/Mesh.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "data/DataModule.h"
#include "data/JsonDocument.h"
#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::animation {

namespace {

void copyFloatArray(ssq::Array arr, std::vector<float> &out) {
    const size_t n = arr.size();
    out.resize(n);
    for (size_t i = 0; i < n; ++i) out[i] = arr.get<float>(i);
}

void bindPositionsFromArray(AnimLattice *self, ssq::Array arr) {
    std::vector<float> pos;
    copyFloatArray(arr, pos);
    if (pos.size() % 3u != 0) {
        throw Exception("AnimLattice.bindPositionsFromArray: array size must be a multiple of 3");
    }
    self->bindPositions(pos.data(), static_cast<int>(pos.size() / 3u));
}

bool updateDeformedPositionsFromArray(AnimLattice *self, ssq::Array arr) {
    std::vector<float> pos;
    copyFloatArray(arr, pos);
    return self->updateDeformedPositions(pos);
}

bool updateDeformedNormalsFromArray(AnimLattice *self, ssq::Array posArr, ssq::Array nrmArr) {
    std::vector<float> pos;
    std::vector<float> nrm;
    copyFloatArray(posArr, pos);
    copyFloatArray(nrmArr, nrm);
    return self->updateDeformedNormals(pos, nrm);
}

}  // namespace

Module_IMPL(Animation, new Animation());

Animation::~Animation() {
    // Owned players may outlive the module if script GC still holds them; detach.
    for (Tween *t : tweens_) {
        if (t) t->setOwner(nullptr);
    }
    tweens_.clear();
    for (SpriteAnim *a : spriteAnims_) {
        if (a) a->setOwner(nullptr);
    }
    spriteAnims_.clear();
    for (SpineAnim *a : spineAnims_) {
        if (a) a->setOwner(nullptr);
    }
    spineAnims_.clear();
}

Tween *Animation::newTween(float duration) {
    auto *t = new Tween(duration);
    registerTween(t);
    return t;
}

SpriteSheet *Animation::newSpriteSheet() { return new SpriteSheet(); }

SpriteSheet *Animation::newSpriteSheetFromSequence(graphics::Graphics *gfx,
                                                    const std::string &pattern, int first,
                                                    int last, int columns) {
    if (!gfx) throw Exception("Animation.newSpriteSheetFromSequence: gfx is null");
    const size_t marker = pattern.find("{n}");
    if (marker == std::string::npos)
        throw Exception("Animation.newSpriteSheetFromSequence: pattern must contain '{n}'");
    if (first < 0 || last < first)
        throw Exception("Animation.newSpriteSheetFromSequence: expected 0 <= first <= last");

    const int count = last - first + 1;
    if (columns <= 0) columns = static_cast<int>(std::ceil(std::sqrt(float(count))));
    if (columns <= 0)
        throw Exception("Animation.newSpriteSheetFromSequence: columns must be > 0");
    const std::string cacheKey = pattern + "#" + std::to_string(first) + ":" +
                                 std::to_string(last) + ":" + std::to_string(columns);
    auto cached = spriteSequenceCache_.find(cacheKey);
    if (cached != spriteSequenceCache_.end()) return cached->second->clone();
    const int rows = (count + columns - 1) / columns;

    image::Image *images = image::Image::create();
    std::vector<image::ImageData *> frames;
    struct Bounds { int x = 0, y = 0, w = 1, h = 1; };
    std::vector<Bounds> bounds;
    frames.reserve(static_cast<size_t>(count));
    int frameW = 0, frameH = 0;
    int packedW = 1, packedH = 1;
    for (int n = first; n <= last; ++n) {
        std::string path = pattern;
        path.replace(marker, 3, std::to_string(n));
        image::ImageData *frame = images->newImageDataFromFile(path);
        if (!frame) throw Exception("Animation.newSpriteSheetFromSequence: failed '%s'", path.c_str());
        if (frame->getFormat() != "RGBA8")
            throw Exception("Animation.newSpriteSheetFromSequence: '%s' must be RGBA8", path.c_str());
        if (frames.empty()) {
            frameW = frame->getWidth();
            frameH = frame->getHeight();
        } else if (frame->getWidth() != frameW || frame->getHeight() != frameH) {
            throw Exception("Animation.newSpriteSheetFromSequence: frame size mismatch at '%s'",
                            path.c_str());
        }
        frames.push_back(frame);
        int minX = frameW, minY = frameH, maxX = -1, maxY = -1;
        for (int py = 0; py < frameH; ++py) {
            for (int px = 0; px < frameW; ++px) {
                if (frame->getPixel(px, py).a <= 1.f / 255.f) continue;
                minX = std::min(minX, px); minY = std::min(minY, py);
                maxX = std::max(maxX, px); maxY = std::max(maxY, py);
            }
        }
        Bounds b;
        if (maxX >= minX && maxY >= minY)
            b = {minX, minY, maxX - minX + 1, maxY - minY + 1};
        bounds.push_back(b);
        packedW = std::max(packedW, b.w);
        packedH = std::max(packedH, b.h);
    }

    constexpr int padding = 1;
    const int cellW = packedW + padding * 2;
    const int cellH = packedH + padding * 2;
    std::unique_ptr<image::ImageData> atlas(
        images->newImageData(columns * cellW, rows * cellH, "RGBA8"));
    auto *sheet = new SpriteSheet();
    try {
        for (int i = 0; i < count; ++i) {
            const int cellX = (i % columns) * cellW;
            const int cellY = (i / columns) * cellH;
            const int x = cellX + padding;
            const int y = cellY + padding;
            image::ImageData *frame = frames[static_cast<size_t>(i)];
            const Bounds &b = bounds[size_t(i)];
            atlas->paste(frame, x, y, b.x, b.y, b.w, b.h);
            atlas->paste(frame, x, cellY, b.x, b.y, b.w, 1);
            atlas->paste(frame, x, y + b.h, b.x, b.y + b.h - 1, b.w, 1);
            atlas->paste(frame, cellX, y, b.x, b.y, 1, b.h);
            atlas->paste(frame, x + b.w, y, b.x + b.w - 1, b.y, 1, b.h);
            atlas->paste(frame, cellX, cellY, b.x, b.y, 1, 1);
            atlas->paste(frame, x + b.w, cellY, b.x + b.w - 1, b.y, 1, 1);
            atlas->paste(frame, cellX, y + b.h, b.x, b.y + b.h - 1, 1, 1);
            atlas->paste(frame, x + b.w, y + b.h, b.x + b.w - 1, b.y + b.h - 1, 1, 1);
            sheet->addFrameTrimmed(std::to_string(first + i), x, y, b.w, b.h,
                                   frameW, frameH, b.x, b.y);
        }
        sheet->setTexture(gfx->newTextureFromImageData(atlas.get()));
        spriteSequenceCache_[cacheKey].reset(sheet->clone());
    } catch (...) {
        delete sheet;
        throw;
    }
    return sheet;
}

int Animation::getSpriteSequenceCacheCount() const { return int(spriteSequenceCache_.size()); }
int Animation::getSpriteSequenceCacheBytes() const {
    int bytes = 0;
    for (const auto &entry : spriteSequenceCache_) {
        auto *texture = entry.second->getTexture();
        if (texture) bytes += texture->getWidth() * texture->getHeight() * 4;
    }
    return bytes;
}
void Animation::clearSpriteSequenceCache() { spriteSequenceCache_.clear(); }

SpriteSheet *Animation::newSpriteSheetFromAtlasJson(graphics::Graphics *gfx,
                                                     const std::string &texturePath,
                                                     const std::string &jsonPath) {
    if (!gfx) throw Exception("Animation.newSpriteSheetFromAtlasJson: gfx is null");
    auto *fs = ModuleManager::getInstance<filesystem::Filesystem>("Filesystem");
    if (!fs) fs = filesystem::Filesystem::create();
    std::unique_ptr<filesystem::FileData> file(fs->read(jsonPath));
    std::string text(static_cast<const char *>(file->getData()), size_t(file->getSize()));
    auto *dm = data::DataModule::create();
    std::string error;
    std::unique_ptr<data::JsonDocument> doc(dm->decodeJson(text, &error));
    if (!doc || !doc->isObject()) throw Exception("Atlas JSON: %s", error.c_str());
    auto root = doc->object();
    if (!root->has("frames")) throw Exception("Atlas JSON: missing frames");
    auto frames = root->getObject("frames");
    if (!frames) throw Exception("Atlas JSON: frames must be an object");
    auto *sheet = new SpriteSheet();
    try {
        for (const auto &name : frames->getNames()) {
            auto item = frames->getObject(name);
            if (item->optValue<bool>("rotated", false))
                throw Exception("Atlas JSON: rotated frames are not supported");
            auto rect = item->getObject("frame");
            auto src = item->getObject("spriteSourceSize");
            auto size = item->getObject("sourceSize");
            const int w = rect->getValue<int>("w"), h = rect->getValue<int>("h");
            sheet->addFrameTrimmed(name, rect->getValue<int>("x"), rect->getValue<int>("y"), w, h,
                                   size ? size->getValue<int>("w") : w,
                                   size ? size->getValue<int>("h") : h,
                                   src ? src->getValue<int>("x") : 0,
                                   src ? src->getValue<int>("y") : 0);
        }
        sheet->setTexture(gfx->newTextureFromFile(texturePath));
    } catch (...) { delete sheet; throw; }
    return sheet;
}

SpriteClip *Animation::newSpriteClip(const std::string &name) { return new SpriteClip(name); }

SpriteAnim *Animation::newSpriteAnim() {
    auto *a = new SpriteAnim();
    registerSpriteAnim(a);
    return a;
}

SpineAtlas *Animation::newSpineAtlas() { return new SpineAtlas(); }

SpineSkeletonData *Animation::newSpineSkeletonData() { return new SpineSkeletonData(); }

SpineSkeleton *Animation::newSpineSkeleton(SpineSkeletonData *data) {
    return new SpineSkeleton(data);
}

SpineAnim *Animation::newSpineAnim(SpineSkeleton *skeleton) {
    auto *a = new SpineAnim(skeleton);
    registerSpineAnim(a);
    return a;
}

SpineAtlas *Animation::newSpineAtlasFromFile(const std::string &path) {
    auto *atlas = new SpineAtlas();
    std::string err;
    if (!atlas->loadFromFile(path, &err)) {
        delete atlas;
        return nullptr;
    }
    return atlas;
}

SpineAtlas *Animation::newSpineAtlasFromText(const std::string &text) {
    auto *atlas = new SpineAtlas();
    std::string err;
    if (!atlas->loadFromText(text, &err)) {
        delete atlas;
        return nullptr;
    }
    return atlas;
}

SpineSkeletonData *Animation::newSpineSkeletonDataFromFile(const std::string &path) {
    auto *data = new SpineSkeletonData();
    std::string err;
    if (!data->loadFromFile(path, &err)) {
        delete data;
        return nullptr;
    }
    return data;
}

SpineSkeletonData *Animation::newSpineSkeletonDataFromJson(const std::string &json) {
    auto *data = new SpineSkeletonData();
    std::string err;
    if (!data->loadFromJson(json, &err)) {
        delete data;
        return nullptr;
    }
    return data;
}

AnimSkeleton *Animation::newSkeleton() { return new AnimSkeleton(); }

AnimClip *Animation::newClip(const std::string &name) { return new AnimClip(name); }

AnimPose *Animation::newPose(int boneCount) { return new AnimPose(boneCount); }

AnimPlayer *Animation::newPlayer(AnimSkeleton *skeleton) { return new AnimPlayer(skeleton); }

AnimBoneMask* Animation::newBoneMask(AnimSkeleton* skeleton) { return new AnimBoneMask(skeleton); }

AnimLayerMixer* Animation::newLayerMixer(AnimSkeleton* skeleton) { return new AnimLayerMixer(skeleton); }

AnimStateMachine *Animation::newStateMachine(AnimSkeleton *skeleton) {
    return new AnimStateMachine(skeleton);
}

MotionDatabase *Animation::newMotionDatabase(AnimSkeleton *skeleton) {
    return new MotionDatabase(skeleton);
}

MotionMatcher *Animation::newMotionMatcher(AnimSkeleton *skeleton, MotionDatabase *database) {
    return new MotionMatcher(skeleton, database);
}

ControlAnim *Animation::newControlAnim(float frequencyHz, float dampingZeta, float response) {
    return new ControlAnim(frequencyHz, dampingZeta, response);
}

ControlPose *Animation::newControlPose(AnimSkeleton *skeleton) { return new ControlPose(skeleton); }

AnimSkeleton *Animation::newSkeletonFromModel(eve::model3d::ModelData *model) {
    return AnimImporter::loadSkeletonFromModel(model);
}

AnimClip *Animation::newClipFromModel(eve::model3d::ModelData *model, AnimSkeleton *skeleton,
                                      int animIndex) {
    return AnimImporter::loadClipFromModel(model, skeleton, animIndex);
}

AnimSkeleton *Animation::newSkeletonFromEvaFile(const std::string &path) {
    AnimSkeleton *sk = nullptr;
    AnimClip *clip   = nullptr;
    AnimImporter::importEvaFile(path, &sk, &clip);
    delete clip;
    return sk;
}

AnimClip *Animation::newClipFromEvaFile(const std::string &path) {
    AnimSkeleton *sk = nullptr;
    AnimClip *clip   = nullptr;
    AnimImporter::importEvaFile(path, &sk, &clip);
    delete sk;
    if (clip) AnimClipRegistry::registerPath(path, clip);
    return clip;
}

AnimSkin *Animation::newSkinFromModel(eve::model3d::ModelData *model, int meshIndex,
                                      AnimSkeleton *skeleton) {
    return AnimSkin::fromModel(model, meshIndex, skeleton);
}

AnimLattice *Animation::newLattice(int divX, int divY, int divZ) {
    return new AnimLattice(divX, divY, divZ);
}

AnimLattice *Animation::newLatticeFromModel(eve::model3d::ModelData *model, int meshIndex,
                                            int divX, int divY, int divZ) {
    return AnimLattice::fromModel(model, meshIndex, divX, divY, divZ);
}

AnimTrail *Animation::newTrail(int capacity) { return new AnimTrail(capacity); }

void Animation::registerTween(Tween *t) {
    if (!t) return;
    t->setOwner(this);
    if (std::find(tweens_.begin(), tweens_.end(), t) == tweens_.end()) tweens_.push_back(t);
}

void Animation::unregisterTween(Tween *t) {
    if (!t) return;
    auto it = std::find(tweens_.begin(), tweens_.end(), t);
    if (it != tweens_.end()) tweens_.erase(it);
    if (t->owner() == this) t->setOwner(nullptr);
}

void Animation::registerSpriteAnim(SpriteAnim *a) {
    if (!a) return;
    a->setOwner(this);
    if (std::find(spriteAnims_.begin(), spriteAnims_.end(), a) == spriteAnims_.end())
        spriteAnims_.push_back(a);
}

void Animation::unregisterSpriteAnim(SpriteAnim *a) {
    if (!a) return;
    auto it = std::find(spriteAnims_.begin(), spriteAnims_.end(), a);
    if (it != spriteAnims_.end()) spriteAnims_.erase(it);
    if (a->owner() == this) a->setOwner(nullptr);
}

void Animation::registerSpineAnim(SpineAnim *a) {
    if (!a) return;
    a->setOwner(this);
    if (std::find(spineAnims_.begin(), spineAnims_.end(), a) == spineAnims_.end())
        spineAnims_.push_back(a);
}

void Animation::unregisterSpineAnim(SpineAnim *a) {
    if (!a) return;
    auto it = std::find(spineAnims_.begin(), spineAnims_.end(), a);
    if (it != spineAnims_.end()) spineAnims_.erase(it);
    if (a->owner() == this) a->setOwner(nullptr);
}

void Animation::update(float dt) {
    // Copy pointer lists: destructors during update must not invalidate iteration.
    std::vector<Tween *> tweenSnap = tweens_;
    for (Tween *t : tweenSnap) {
        if (!t) continue;
        if (t->isActive()) t->update(dt);
    }
    std::vector<SpriteAnim *> spriteSnap = spriteAnims_;
    for (SpriteAnim *a : spriteSnap) {
        if (a && a->isPlaying()) a->update(dt);
    }
    std::vector<SpineAnim *> spineSnap = spineAnims_;
    for (SpineAnim *a : spineSnap) {
        if (a && a->isPlaying()) a->update(dt);
    }
}

int Animation::getActiveCount() const {
    int n = 0;
    for (const Tween *t : tweens_) {
        if (t && t->isActive()) ++n;
    }
    for (const SpriteAnim *a : spriteAnims_) {
        if (a && a->isPlaying()) ++n;
    }
    for (const SpineAnim *a : spineAnims_) {
        if (a && a->isPlaying()) ++n;
    }
    return n;
}

void Animation::clearFinished() {
    auto it = tweens_.begin();
    while (it != tweens_.end()) {
        Tween *t = *it;
        if (!t || t->isFinished() || t->isStopped()) {
            if (t && t->owner() == this) t->setOwner(nullptr);
            it = tweens_.erase(it);
        } else {
            ++it;
        }
    }
}

void Animation::clearAll() {
    for (Tween *t : tweens_) {
        if (t && t->owner() == this) t->setOwner(nullptr);
    }
    tweens_.clear();
}

void Animation::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Animation::create, false);
    expose(cls);

    auto tw = table.addClass<Tween>(
        "Tween", std::function<Tween *()>([]() -> Tween * { return nullptr; }), true);
    tw.addFunc("setFrom", &Tween::setFrom);
    tw.addFunc("setTo", &Tween::setTo);
    tw.addFunc("setDelta", &Tween::setDelta);
    tw.addFunc("setFromAngle", &Tween::setFromAngle);
    tw.addFunc("setToAngle", &Tween::setToAngle);
    tw.addFunc("setDeltaAngle", &Tween::setDeltaAngle);
    tw.addFunc("has", &Tween::has);
    tw.addFunc("get", &Tween::get);
    tw.addFunc("getFrom", &Tween::getFrom);
    tw.addFunc("getTo", &Tween::getTo);
    tw.addFunc("getDelta", &Tween::getDelta);
    tw.addFunc("setDuration", &Tween::setDuration);
    tw.addFunc("getDuration", &Tween::getDuration);
    tw.addFunc("setDelay", &Tween::setDelay);
    tw.addFunc("getDelay", &Tween::getDelay);
    tw.addFunc("setEase", &Tween::setEase);
    tw.addFunc("getEase", &Tween::getEase);
    tw.addFunc("setRepeat", &Tween::setRepeat);
    tw.addFunc("getRepeat", &Tween::getRepeat);
    tw.addFunc("setYoyo", &Tween::setYoyo);
    tw.addFunc("getYoyo", &Tween::getYoyo);
    tw.addFunc("start", &Tween::start);
    tw.addFunc("pause", &Tween::pause);
    tw.addFunc("resume", &Tween::resume);
    tw.addFunc("stop", &Tween::stop);
    tw.addFunc("reset", &Tween::reset);
    tw.addFunc("isRunning", &Tween::isRunning);
    tw.addFunc("isPaused", &Tween::isPaused);
    tw.addFunc("isFinished", &Tween::isFinished);
    tw.addFunc("isStopped", &Tween::isStopped);
    tw.addFunc("isDelayed", &Tween::isDelayed);
    tw.addFunc("isActive", &Tween::isActive);
    tw.addFunc("getElapsed", &Tween::getElapsed);
    tw.addFunc("getProgress", &Tween::getProgress);
    tw.addFunc("getEasedProgress", &Tween::getEasedProgress);
    tw.addFunc("update", &Tween::update);
    tw.addFunc("evaluate", &Tween::evaluate);
    tw.addFunc("getPropertyCount", &Tween::getPropertyCount);
    tw.addFunc("getPropertyName", &Tween::getPropertyName);

    auto sk = table.addClass<AnimSkeleton>(
        "AnimSkeleton", std::function<AnimSkeleton *()>([]() -> AnimSkeleton * { return nullptr; }),
        true);
    sk.addFunc("addBone", &AnimSkeleton::addBone);
    sk.addFunc("getBoneCount", &AnimSkeleton::getBoneCount);
    sk.addFunc("getBoneName", &AnimSkeleton::getBoneName);
    sk.addFunc("findBone", &AnimSkeleton::findBone);
    sk.addFunc("getParent", &AnimSkeleton::getParent);
    sk.addFunc("setBindPosition", &AnimSkeleton::setBindPosition);
    sk.addFunc("setBindRotation", &AnimSkeleton::setBindRotation);
    sk.addFunc("setBindScale", &AnimSkeleton::setBindScale);
    sk.addFunc("getBindPositionX", &AnimSkeleton::getBindPositionX);
    sk.addFunc("getBindPositionY", &AnimSkeleton::getBindPositionY);
    sk.addFunc("getBindPositionZ", &AnimSkeleton::getBindPositionZ);
    sk.addFunc("getBindRotationX", &AnimSkeleton::getBindRotationX);
    sk.addFunc("getBindRotationY", &AnimSkeleton::getBindRotationY);
    sk.addFunc("getBindRotationZ", &AnimSkeleton::getBindRotationZ);
    sk.addFunc("getBindRotationW", &AnimSkeleton::getBindRotationW);
    sk.addFunc("getBindScaleX", &AnimSkeleton::getBindScaleX);
    sk.addFunc("getBindScaleY", &AnimSkeleton::getBindScaleY);
    sk.addFunc("getBindScaleZ", &AnimSkeleton::getBindScaleZ);
    sk.addFunc("applyBindPose", &AnimSkeleton::applyBindPose);

    auto clip = table.addClass<AnimClip>(
        "AnimClip", std::function<AnimClip *()>([]() -> AnimClip * { return nullptr; }), true);
    clip.addFunc("setName", &AnimClip::setName);
    clip.addFunc("getName", &AnimClip::getName);
    clip.addFunc("setDuration", &AnimClip::setDuration);
    clip.addFunc("getDuration", &AnimClip::getDuration);
    clip.addFunc("setLoop", &AnimClip::setLoop);
    clip.addFunc("getLoop", &AnimClip::getLoop);
    clip.addFunc("setSampleRate", &AnimClip::setSampleRate);
    clip.addFunc("getSampleRate", &AnimClip::getSampleRate);
    clip.addFunc("addPositionKey", &AnimClip::addPositionKey);
    clip.addFunc("addRotationKey", &AnimClip::addRotationKey);
    clip.addFunc("addScaleKey", &AnimClip::addScaleKey);
    clip.addFunc("addEvent", &AnimClip::addEvent);
    clip.addFunc("getEventCount", &AnimClip::getEventCount);
    clip.addFunc("getEventTime", &AnimClip::getEventTime);
    clip.addFunc("getEventName", &AnimClip::getEventName);
    clip.addFunc("getEventPayload", &AnimClip::getEventPayload);
    clip.addFunc("getPositionKeyCount", &AnimClip::getPositionKeyCount);
    clip.addFunc("getRotationKeyCount", &AnimClip::getRotationKeyCount);
    clip.addFunc("getScaleKeyCount", &AnimClip::getScaleKeyCount);
    clip.addFunc("applyPlanarRootMotion", &AnimClip::applyPlanarRootMotion);
    clip.addFunc("sample", &AnimClip::sample);
    clip.addFunc("wrapTime", &AnimClip::wrapTime);

    auto pose = table.addClass<AnimPose>(
        "AnimPose", std::function<AnimPose *()>([]() -> AnimPose * { return nullptr; }), true);
    pose.addFunc("resize", &AnimPose::resize);
    pose.addFunc("getBoneCount", &AnimPose::getBoneCount);
    pose.addFunc("copyFrom", &AnimPose::copyFrom);
    pose.addFunc("blendFrom", &AnimPose::blendFrom);
    pose.addFunc("setLocalPosition", &AnimPose::setLocalPosition);
    pose.addFunc("setLocalRotation", &AnimPose::setLocalRotation);
    pose.addFunc("setLocalScale", &AnimPose::setLocalScale);
    pose.addFunc("getLocalPositionX", &AnimPose::getLocalPositionX);
    pose.addFunc("getLocalPositionY", &AnimPose::getLocalPositionY);
    pose.addFunc("getLocalPositionZ", &AnimPose::getLocalPositionZ);
    pose.addFunc("getLocalRotationX", &AnimPose::getLocalRotationX);
    pose.addFunc("getLocalRotationY", &AnimPose::getLocalRotationY);
    pose.addFunc("getLocalRotationZ", &AnimPose::getLocalRotationZ);
    pose.addFunc("getLocalRotationW", &AnimPose::getLocalRotationW);
    pose.addFunc("getLocalScaleX", &AnimPose::getLocalScaleX);
    pose.addFunc("getLocalScaleY", &AnimPose::getLocalScaleY);
    pose.addFunc("getLocalScaleZ", &AnimPose::getLocalScaleZ);
    pose.addFunc("computeWorld", &AnimPose::computeWorld);
    pose.addFunc("getWorldPositionX", &AnimPose::getWorldPositionX);
    pose.addFunc("getWorldPositionY", &AnimPose::getWorldPositionY);
    pose.addFunc("getWorldPositionZ", &AnimPose::getWorldPositionZ);
    pose.addFunc("getWorldRotationX", &AnimPose::getWorldRotationX);
    pose.addFunc("getWorldRotationY", &AnimPose::getWorldRotationY);
    pose.addFunc("getWorldRotationZ", &AnimPose::getWorldRotationZ);
    pose.addFunc("getWorldRotationW", &AnimPose::getWorldRotationW);
    pose.addFunc("getWorldMatrixElement", &AnimPose::getWorldMatrixElement);

    auto skin = table.addClass<AnimSkin>(
        "AnimSkin", std::function<AnimSkin *()>([]() -> AnimSkin * { return nullptr; }), true);
    skin.addFunc("getVertexCount", &AnimSkin::getVertexCount);
    skin.addFunc("getBoneCount", &AnimSkin::getBoneCount);
    skin.addFunc("getInfluenceCount", &AnimSkin::getInfluenceCount);
    skin.addFunc("getSkeletonBone", &AnimSkin::getSkeletonBone);
    skin.addFunc("getSkinBoneName", &AnimSkin::getSkinBoneName);
    skin.addFunc("getInverseBindElement", &AnimSkin::getInverseBindElement);
    skin.addFunc("getBindPositionX", &AnimSkin::getBindPositionX);
    skin.addFunc("getBindPositionY", &AnimSkin::getBindPositionY);
    skin.addFunc("getBindPositionZ", &AnimSkin::getBindPositionZ);
    skin.addFunc("getVertexBone", &AnimSkin::getVertexBone);
    skin.addFunc("getVertexWeight", &AnimSkin::getVertexWeight);
    skin.addFunc("updateSkinnedPositions", &AnimSkin::updateSkinnedPositions);
    skin.addFunc("hasSkinnedPositions", &AnimSkin::hasSkinnedPositions);
    skin.addFunc("getSkinnedPositionX", &AnimSkin::getSkinnedPositionX);
    skin.addFunc("getSkinnedPositionY", &AnimSkin::getSkinnedPositionY);
    skin.addFunc("getSkinnedPositionZ", &AnimSkin::getSkinnedPositionZ);
    skin.addFunc("getSkinnedPositions", &AnimSkin::getSkinnedPositions);
    skin.addFunc("updateSkinnedNormals", &AnimSkin::updateSkinnedNormals);
    skin.addFunc("hasSkinnedNormals", &AnimSkin::hasSkinnedNormals);
    skin.addFunc("getSkinnedNormals", &AnimSkin::getSkinnedNormals);
    skin.addFunc("applyToMesh",
                 std::function<bool(AnimSkin *, graphics::Graphics *,
                                    graphics::Mesh *, AnimPose *)>(
                     &AnimSkin::applyToMesh));

    auto lattice = table.addClass<AnimLattice>(
        "AnimLattice", std::function<AnimLattice *()>([]() -> AnimLattice * { return nullptr; }),
        true);
    lattice.addFunc("setDivisions", &AnimLattice::setDivisions);
    lattice.addFunc("getDivisionsX", &AnimLattice::getDivisionsX);
    lattice.addFunc("getDivisionsY", &AnimLattice::getDivisionsY);
    lattice.addFunc("getDivisionsZ", &AnimLattice::getDivisionsZ);
    lattice.addFunc("setSize", &AnimLattice::setSize);
    lattice.addFunc("getSizeX", &AnimLattice::getSizeX);
    lattice.addFunc("getSizeY", &AnimLattice::getSizeY);
    lattice.addFunc("getSizeZ", &AnimLattice::getSizeZ);
    lattice.addFunc("setOrigin", &AnimLattice::setOrigin);
    lattice.addFunc("getOriginX", &AnimLattice::getOriginX);
    lattice.addFunc("getOriginY", &AnimLattice::getOriginY);
    lattice.addFunc("getOriginZ", &AnimLattice::getOriginZ);
    lattice.addFunc("setClamp", &AnimLattice::setClamp);
    lattice.addFunc("getClamp", &AnimLattice::getClamp);
    lattice.addFunc("getPointCount", &AnimLattice::getPointCount);
    lattice.addFunc("setPointScale", &AnimLattice::setPointScale);
    lattice.addFunc("setPointOffset", &AnimLattice::setPointOffset);
    lattice.addFunc("setScale", &AnimLattice::setScale);
    lattice.addFunc("reset", &AnimLattice::reset);
    lattice.addFunc("getPointScaleX", &AnimLattice::getPointScaleX);
    lattice.addFunc("getPointScaleY", &AnimLattice::getPointScaleY);
    lattice.addFunc("getPointScaleZ", &AnimLattice::getPointScaleZ);
    lattice.addFunc("getPointOffsetX", &AnimLattice::getPointOffsetX);
    lattice.addFunc("getPointOffsetY", &AnimLattice::getPointOffsetY);
    lattice.addFunc("getPointOffsetZ", &AnimLattice::getPointOffsetZ);
    lattice.addFunc("bindModel", &AnimLattice::bindModel);
    lattice.addFunc("bindPositionsFromArray",
                    std::function<void(AnimLattice *, ssq::Array)>(bindPositionsFromArray));
    lattice.addFunc("clearBind", &AnimLattice::clearBind);
    lattice.addFunc("getVertexCount", &AnimLattice::getVertexCount);
    lattice.addFunc("getBindPositionX", &AnimLattice::getBindPositionX);
    lattice.addFunc("getBindPositionY", &AnimLattice::getBindPositionY);
    lattice.addFunc("getBindPositionZ", &AnimLattice::getBindPositionZ);
    lattice.addFunc(
        "updateDeformedPositions",
        static_cast<bool (AnimLattice::*)()>(&AnimLattice::updateDeformedPositions));
    lattice.addFunc(
        "updateDeformedPositionsFromArray",
        std::function<bool(AnimLattice *, ssq::Array)>(updateDeformedPositionsFromArray));
    lattice.addFunc(
        "updateDeformedNormalsFromArray",
        std::function<bool(AnimLattice *, ssq::Array, ssq::Array)>(
            updateDeformedNormalsFromArray));
    lattice.addFunc("hasDeformedPositions", &AnimLattice::hasDeformedPositions);
    lattice.addFunc("hasDeformedNormals", &AnimLattice::hasDeformedNormals);
    lattice.addFunc("getDeformedPositionX", &AnimLattice::getDeformedPositionX);
    lattice.addFunc("getDeformedPositionY", &AnimLattice::getDeformedPositionY);
    lattice.addFunc("getDeformedPositionZ", &AnimLattice::getDeformedPositionZ);
    lattice.addFunc("getDeformedPositions", &AnimLattice::getDeformedPositions);
    lattice.addFunc("getDeformedNormals", &AnimLattice::getDeformedNormals);

    auto player = table.addClass<AnimPlayer>(
        "AnimPlayer", std::function<AnimPlayer *()>([]() -> AnimPlayer * { return nullptr; }),
        true);
    player.addFunc("play", &AnimPlayer::play);
    player.addFunc("crossFade", &AnimPlayer::crossFade);
    player.addFunc("stop", &AnimPlayer::stop);
    player.addFunc("pause", &AnimPlayer::pause);
    player.addFunc("resume", &AnimPlayer::resume);
    player.addFunc("setSpeed", &AnimPlayer::setSpeed);
    player.addFunc("getSpeed", &AnimPlayer::getSpeed);
    player.addFunc("setTime", &AnimPlayer::setTime);
    player.addFunc("getTime", &AnimPlayer::getTime);
    player.addFunc("setLoop", &AnimPlayer::setLoop);
    player.addFunc("getLoop", &AnimPlayer::getLoop);
    player.addFunc("isPlaying", &AnimPlayer::isPlaying);
    player.addFunc("isPaused", &AnimPlayer::isPaused);
    player.addFunc("getPose", &AnimPlayer::getPose);
    player.addFunc("getEventCount", &AnimPlayer::getEventCount);
    player.addFunc("getEventName", &AnimPlayer::getEventName);
    player.addFunc("getEventPayload", &AnimPlayer::getEventPayload);
    player.addFunc("clearEvents", &AnimPlayer::clearEvents);
    player.addFunc("update", &AnimPlayer::update);

    auto mask = table.addClass<AnimBoneMask>(
        "AnimBoneMask", std::function<AnimBoneMask*()>([]() -> AnimBoneMask* { return nullptr; }), true);
    mask.addFunc("setAll", &AnimBoneMask::setAll);
    mask.addFunc("setBoneWeight", &AnimBoneMask::setBoneWeight);
    mask.addFunc("setBoneWeightByName", &AnimBoneMask::setBoneWeightByName);
    mask.addFunc("setBoneAndChildren", &AnimBoneMask::setBoneAndChildren);
    mask.addFunc("getBoneWeight", &AnimBoneMask::getBoneWeight);
    mask.addFunc("getBoneCount", &AnimBoneMask::getBoneCount);

    auto mixer = table.addClass<AnimLayerMixer>(
        "AnimLayerMixer", std::function<AnimLayerMixer*()>([]() -> AnimLayerMixer* { return nullptr; }), true);
    mixer.addFunc("setBasePlayer", &AnimLayerMixer::setBasePlayer);
    mixer.addFunc("getBasePlayer", &AnimLayerMixer::getBasePlayer);
    mixer.addFunc("addLayer", &AnimLayerMixer::addLayer);
    mixer.addFunc("removeLayer", &AnimLayerMixer::removeLayer);
    mixer.addFunc("setLayerWeight", &AnimLayerMixer::setLayerWeight);
    mixer.addFunc("setLayerEnabled", &AnimLayerMixer::setLayerEnabled);
    mixer.addFunc("getLayerCount", &AnimLayerMixer::getLayerCount);
    mixer.addFunc("getLayerName", &AnimLayerMixer::getLayerName);
    mixer.addFunc("update", &AnimLayerMixer::update);
    mixer.addFunc("getPose", &AnimLayerMixer::getPose);
    mixer.addFunc("getEventCount", &AnimLayerMixer::getEventCount);
    mixer.addFunc("getEventLayer", &AnimLayerMixer::getEventLayer);
    mixer.addFunc("getEventName", &AnimLayerMixer::getEventName);
    mixer.addFunc("getEventPayload", &AnimLayerMixer::getEventPayload);
    mixer.addFunc("clearEvents", &AnimLayerMixer::clearEvents);

    auto sm = table.addClass<AnimStateMachine>(
        "AnimStateMachine",
        std::function<AnimStateMachine *()>([]() -> AnimStateMachine * { return nullptr; }), true);
    sm.addFunc("addState", &AnimStateMachine::addState);
    sm.addFunc("setEntry", &AnimStateMachine::setEntry);
    sm.addFunc("hasState", &AnimStateMachine::hasState);
    sm.addFunc("getCurrentState", &AnimStateMachine::getCurrentState);
    sm.addFunc("getStateCount", &AnimStateMachine::getStateCount);
    sm.addFunc("addTransition", &AnimStateMachine::addTransition);
    sm.addFunc("addFloatCondition", &AnimStateMachine::addFloatCondition);
    sm.addFunc("addBoolCondition", &AnimStateMachine::addBoolCondition);
    sm.addFunc("addTriggerCondition", &AnimStateMachine::addTriggerCondition);
    sm.addFunc("setExitTime", &AnimStateMachine::setExitTime);
    sm.addFunc("setHasExitTime", &AnimStateMachine::setHasExitTime);
    sm.addFunc("setFloat", &AnimStateMachine::setFloat);
    sm.addFunc("getFloat", &AnimStateMachine::getFloat);
    sm.addFunc("setBool", &AnimStateMachine::setBool);
    sm.addFunc("getBool", &AnimStateMachine::getBool);
    sm.addFunc("setTrigger", &AnimStateMachine::setTrigger);
    sm.addFunc("resetTrigger", &AnimStateMachine::resetTrigger);
    sm.addFunc("getPose", &AnimStateMachine::getPose);
    sm.addFunc("getStateTime", &AnimStateMachine::getStateTime);
    sm.addFunc("isBlending", &AnimStateMachine::isBlending);
    sm.addFunc("update", &AnimStateMachine::update);

    auto db = table.addClass<MotionDatabase>(
        "MotionDatabase",
        std::function<MotionDatabase *()>([]() -> MotionDatabase * { return nullptr; }), true);
    db.addFunc("addFeatureBone", &MotionDatabase::addFeatureBone);
    db.addFunc("addFeatureBoneByName", &MotionDatabase::addFeatureBoneByName);
    db.addFunc("setRootBone", &MotionDatabase::setRootBone);
    db.addFunc("getRootBone", &MotionDatabase::getRootBone);
    db.addFunc("setRootBoneByName", &MotionDatabase::setRootBoneByName);
    db.addFunc("addClip", &MotionDatabase::addClip);
    db.addFunc("getClipCount", &MotionDatabase::getClipCount);
    db.addFunc("bake", &MotionDatabase::bake);
    db.addFunc("isBaked", &MotionDatabase::isBaked);
    db.addFunc("getFrameCount", &MotionDatabase::getFrameCount);
    db.addFunc("getFeatureSize", &MotionDatabase::getFeatureSize);
    db.addFunc("getFrameTime", &MotionDatabase::getFrameTime);
    db.addFunc("getFrameClipIndex", &MotionDatabase::getFrameClipIndex);
    db.addFunc("getFeatureBoneCount", &MotionDatabase::getFeatureBoneCount);
    db.addFunc("getFeatureBone", &MotionDatabase::getFeatureBone);

    auto mm = table.addClass<MotionMatcher>(
        "MotionMatcher",
        std::function<MotionMatcher *()>([]() -> MotionMatcher * { return nullptr; }), true);
    mm.addFunc("setDesiredVelocity", &MotionMatcher::setDesiredVelocity);
    mm.addFunc("getDesiredVelocityX", &MotionMatcher::getDesiredVelocityX);
    mm.addFunc("getDesiredVelocityZ", &MotionMatcher::getDesiredVelocityZ);
    mm.addFunc("setDesiredYaw", &MotionMatcher::setDesiredYaw);
    mm.addFunc("getDesiredYaw", &MotionMatcher::getDesiredYaw);
    mm.addFunc("setSearchInterval", &MotionMatcher::setSearchInterval);
    mm.addFunc("getSearchInterval", &MotionMatcher::getSearchInterval);
    mm.addFunc("setBlendTime", &MotionMatcher::setBlendTime);
    mm.addFunc("getBlendTime", &MotionMatcher::getBlendTime);
    mm.addFunc("setTrajectoryWeight", &MotionMatcher::setTrajectoryWeight);
    mm.addFunc("getTrajectoryWeight", &MotionMatcher::getTrajectoryWeight);
    mm.addFunc("setPoseWeight", &MotionMatcher::setPoseWeight);
    mm.addFunc("getPoseWeight", &MotionMatcher::getPoseWeight);
    mm.addFunc("setVelocityWeight", &MotionMatcher::setVelocityWeight);
    mm.addFunc("getVelocityWeight", &MotionMatcher::getVelocityWeight);
    mm.addFunc("setIgnoreRadius", &MotionMatcher::setIgnoreRadius);
    mm.addFunc("getIgnoreRadius", &MotionMatcher::getIgnoreRadius);
    mm.addFunc("getMatchedFrame", &MotionMatcher::getMatchedFrame);
    mm.addFunc("getMatchedClipIndex", &MotionMatcher::getMatchedClipIndex);
    mm.addFunc("getMatchedTime", &MotionMatcher::getMatchedTime);
    mm.addFunc("getLastSearchCost", &MotionMatcher::getLastSearchCost);
    mm.addFunc("getPose", &MotionMatcher::getPose);
    mm.addFunc("search", &MotionMatcher::search);
    mm.addFunc("update", &MotionMatcher::update);

    auto ca = table.addClass<ControlAnim>(
        "ControlAnim", std::function<ControlAnim *()>([]() -> ControlAnim * { return nullptr; }),
        true);
    ca.addFunc("setFrequency", &ControlAnim::setFrequency);
    ca.addFunc("getFrequency", &ControlAnim::getFrequency);
    ca.addFunc("setDamping", &ControlAnim::setDamping);
    ca.addFunc("getDamping", &ControlAnim::getDamping);
    ca.addFunc("setResponse", &ControlAnim::setResponse);
    ca.addFunc("getResponse", &ControlAnim::getResponse);
    ca.addFunc("setIntegrator", &ControlAnim::setIntegrator);
    ca.addFunc("getIntegrator", &ControlAnim::getIntegrator);
    ca.addFunc("set", &ControlAnim::set);
    ca.addFunc("setTarget", &ControlAnim::setTarget);
    ca.addFunc("setTargetVelocity", &ControlAnim::setTargetVelocity);
    ca.addFunc("impulse", &ControlAnim::impulse);
    ca.addFunc("has", &ControlAnim::has);
    ca.addFunc("get", &ControlAnim::get);
    ca.addFunc("getVelocity", &ControlAnim::getVelocity);
    ca.addFunc("getTarget", &ControlAnim::getTarget);
    ca.addFunc("clear", &ControlAnim::clear);
    ca.addFunc("remove", &ControlAnim::remove);
    ca.addFunc("getPropertyCount", &ControlAnim::getPropertyCount);
    ca.addFunc("getPropertyName", &ControlAnim::getPropertyName);
    ca.addFunc("update", &ControlAnim::update);

    auto cp = table.addClass<ControlPose>(
        "ControlPose", std::function<ControlPose *()>([]() -> ControlPose * { return nullptr; }),
        true);
    cp.addFunc("setFrequency", &ControlPose::setFrequency);
    cp.addFunc("getFrequency", &ControlPose::getFrequency);
    cp.addFunc("setDamping", &ControlPose::setDamping);
    cp.addFunc("getDamping", &ControlPose::getDamping);
    cp.addFunc("setResponse", &ControlPose::setResponse);
    cp.addFunc("getResponse", &ControlPose::getResponse);
    cp.addFunc("setIntegrator", &ControlPose::setIntegrator);
    cp.addFunc("getIntegrator", &ControlPose::getIntegrator);
    cp.addFunc("setBoneWeight", &ControlPose::setBoneWeight);
    cp.addFunc("getBoneWeight", &ControlPose::getBoneWeight);
    cp.addFunc("setTargetPose", &ControlPose::setTargetPose);
    cp.addFunc("snapToTarget", &ControlPose::snapToTarget);
    cp.addFunc("getPose", &ControlPose::getPose);
    cp.addFunc("getTargetPose", &ControlPose::getTargetPose);
    cp.addFunc("update", &ControlPose::update);

    auto trail = table.addClass<AnimTrail>(
        "AnimTrail", std::function<AnimTrail *()>([]() -> AnimTrail * { return nullptr; }), true);
    trail.addFunc("setCapacity", &AnimTrail::setCapacity);
    trail.addFunc("getCapacity", &AnimTrail::getCapacity);
    trail.addFunc("setDuration", &AnimTrail::setDuration);
    trail.addFunc("getDuration", &AnimTrail::getDuration);
    trail.addFunc("setMinDistance", &AnimTrail::setMinDistance);
    trail.addFunc("getMinDistance", &AnimTrail::getMinDistance);
    trail.addFunc("setWidth", &AnimTrail::setWidth);
    trail.addFunc("getWidth", &AnimTrail::getWidth);
    trail.addFunc("setColor", &AnimTrail::setColor);
    trail.addFunc("getColorR", &AnimTrail::getColorR);
    trail.addFunc("getColorG", &AnimTrail::getColorG);
    trail.addFunc("getColorB", &AnimTrail::getColorB);
    trail.addFunc("getColorA", &AnimTrail::getColorA);
    trail.addFunc("setFade", &AnimTrail::setFade);
    trail.addFunc("getFade", &AnimTrail::getFade);
    trail.addFunc("setStyle", &AnimTrail::setStyle);
    trail.addFunc("getStyle", &AnimTrail::getStyle);
    trail.addFunc("setDrawScale", &AnimTrail::setDrawScale);
    trail.addFunc("getDrawScaleX", &AnimTrail::getDrawScaleX);
    trail.addFunc("getDrawScaleY", &AnimTrail::getDrawScaleY);
    trail.addFunc("setDrawOffset", &AnimTrail::setDrawOffset);
    trail.addFunc("getDrawOffsetX", &AnimTrail::getDrawOffsetX);
    trail.addFunc("getDrawOffsetY", &AnimTrail::getDrawOffsetY);
    trail.addFunc("addPoint", &AnimTrail::addPoint);
    trail.addFunc("addPoint3", &AnimTrail::addPoint3);
    trail.addFunc("sampleBone", &AnimTrail::sampleBone);
    trail.addFunc("sampleBoneOffset", &AnimTrail::sampleBoneOffset);
    trail.addFunc("clear", &AnimTrail::clear);
    trail.addFunc("update", &AnimTrail::update);
    trail.addFunc("getPointCount", &AnimTrail::getPointCount);
    trail.addFunc("getPointX", &AnimTrail::getPointX);
    trail.addFunc("getPointY", &AnimTrail::getPointY);
    trail.addFunc("getPointZ", &AnimTrail::getPointZ);
    trail.addFunc("getPointAge", &AnimTrail::getPointAge);
    trail.addFunc("getPointAlpha", &AnimTrail::getPointAlpha);
    trail.addFunc("draw", &AnimTrail::draw);

    auto sheet = table.addClass<SpriteSheet>(
        "SpriteSheet", std::function<SpriteSheet *()>([]() -> SpriteSheet * { return nullptr; }),
        true);
    sheet.addFunc("addFrame", &SpriteSheet::addFrame);
    sheet.addFunc("setGrid", &SpriteSheet::setGrid);
    sheet.addFunc("clear", &SpriteSheet::clear);
    sheet.addFunc("setTexture", &SpriteSheet::setTexture);
    sheet.addFunc("getTexture", &SpriteSheet::getTexture);
    sheet.addFunc("getFrameCount", &SpriteSheet::getFrameCount);
    sheet.addFunc("findFrame", &SpriteSheet::findFrame);
    sheet.addFunc("getFrameName", &SpriteSheet::getFrameName);
    sheet.addFunc("getFrameX", &SpriteSheet::getFrameX);
    sheet.addFunc("getFrameY", &SpriteSheet::getFrameY);
    sheet.addFunc("getFrameWidth", &SpriteSheet::getFrameWidth);
    sheet.addFunc("getFrameHeight", &SpriteSheet::getFrameHeight);
    sheet.addFunc("getFrameSourceWidth", &SpriteSheet::getFrameSourceWidth);
    sheet.addFunc("getFrameSourceHeight", &SpriteSheet::getFrameSourceHeight);
    sheet.addFunc("getFrameOffsetX", &SpriteSheet::getFrameOffsetX);
    sheet.addFunc("getFrameOffsetY", &SpriteSheet::getFrameOffsetY);
    sheet.addFunc("applyToQuad", &SpriteSheet::applyToQuad);

    auto sclip = table.addClass<SpriteClip>(
        "SpriteClip", std::function<SpriteClip *()>([]() -> SpriteClip * { return nullptr; }),
        true);
    sclip.addFunc("setName", &SpriteClip::setName);
    sclip.addFunc("getName", &SpriteClip::getName);
    sclip.addFunc("setLoop", &SpriteClip::setLoop);
    sclip.addFunc("getLoop", &SpriteClip::getLoop);
    sclip.addFunc("addFrame", &SpriteClip::addFrame);
    sclip.addFunc("addFrameByName", &SpriteClip::addFrameByName);
    sclip.addFunc("addRange", &SpriteClip::addRange);
    sclip.addFunc("setFPS", &SpriteClip::setFPS);
    sclip.addFunc("getFPS", &SpriteClip::getFPS);
    sclip.addFunc("addEvent", &SpriteClip::addEvent);
    sclip.addFunc("getEvent", &SpriteClip::getEvent);
    sclip.addFunc("clear", &SpriteClip::clear);
    sclip.addFunc("getFrameCount", &SpriteClip::getFrameCount);
    sclip.addFunc("getSheetFrame", &SpriteClip::getSheetFrame);
    sclip.addFunc("getFrameDuration", &SpriteClip::getFrameDuration);
    sclip.addFunc("getDuration", &SpriteClip::getDuration);
    sclip.addFunc("frameAtTime", &SpriteClip::frameAtTime);
    sclip.addFunc("wrapTime", &SpriteClip::wrapTime);

    auto sanim = table.addClass<SpriteAnim>(
        "SpriteAnim", std::function<SpriteAnim *()>([]() -> SpriteAnim * { return nullptr; }),
        true);
    sanim.addFunc("setSheet", &SpriteAnim::setSheet);
    sanim.addFunc("getSheet", &SpriteAnim::getSheet);
    sanim.addFunc("play", &SpriteAnim::play);
    sanim.addFunc("playReverse", &SpriteAnim::playReverse);
    sanim.addFunc("stop", &SpriteAnim::stop);
    sanim.addFunc("pause", &SpriteAnim::pause);
    sanim.addFunc("resume", &SpriteAnim::resume);
    sanim.addFunc("setSpeed", &SpriteAnim::setSpeed);
    sanim.addFunc("getSpeed", &SpriteAnim::getSpeed);
    sanim.addFunc("addSpeedCurveKey", &SpriteAnim::addSpeedCurveKey);
    sanim.addFunc("clearSpeedCurve", &SpriteAnim::clearSpeedCurve);
    sanim.addFunc("resetSpeedCurve", &SpriteAnim::resetSpeedCurve);
    sanim.addFunc("setSpeedCurveLoop", &SpriteAnim::setSpeedCurveLoop);
    sanim.addFunc("setSpeedCurveInterpolation", &SpriteAnim::setSpeedCurveInterpolation);
    sanim.addFunc("getSpeedCurveValue", &SpriteAnim::getSpeedCurveValue);
    sanim.addFunc("setFrame", &SpriteAnim::setFrame);
    sanim.addFunc("step", &SpriteAnim::step);
    sanim.addFunc("playOnce", &SpriteAnim::playOnce);
    sanim.addFunc("queue", &SpriteAnim::queue);
    sanim.addFunc("consumeEvent", &SpriteAnim::consumeEvent);
    sanim.addFunc("setTime", &SpriteAnim::setTime);
    sanim.addFunc("getTime", &SpriteAnim::getTime);
    sanim.addFunc("setLoop", &SpriteAnim::setLoop);
    sanim.addFunc("getLoop", &SpriteAnim::getLoop);
    sanim.addFunc("isPlaying", &SpriteAnim::isPlaying);
    sanim.addFunc("isPaused", &SpriteAnim::isPaused);
    sanim.addFunc("isFinished", &SpriteAnim::isFinished);
    sanim.addFunc("getLoopCount", &SpriteAnim::getLoopCount);
    sanim.addFunc("consumeCompleted", &SpriteAnim::consumeCompleted);
    sanim.addFunc("consumeLooped", &SpriteAnim::consumeLooped);
    sanim.addFunc("getClip", &SpriteAnim::getClip);
    sanim.addFunc("getClipFrame", &SpriteAnim::getClipFrame);
    sanim.addFunc("getSheetFrame", &SpriteAnim::getSheetFrame);
    sanim.addFunc("bindQuad", &SpriteAnim::bindQuad);
    sanim.addFunc("unbindQuad", &SpriteAnim::unbindQuad);
    sanim.addFunc("getBoundQuad", &SpriteAnim::getBoundQuad);
    sanim.addFunc("bindSprite", &SpriteAnim::bindSprite);
    sanim.addFunc("applyToQuad", &SpriteAnim::applyToQuad);
    sanim.addFunc("update", &SpriteAnim::update);

    auto satlas = table.addClass<SpineAtlas>(
        "SpineAtlas", std::function<SpineAtlas *()>([]() -> SpineAtlas * { return nullptr; }),
        true);
    satlas.addFunc("loadFromText",
                   [](SpineAtlas *self, const std::string &text) -> bool {
                       return self->loadFromText(text, nullptr);
                   });
    satlas.addFunc("loadFromFile",
                   [](SpineAtlas *self, const std::string &path) -> bool {
                       return self->loadFromFile(path, nullptr);
                   });
    satlas.addFunc("clear", &SpineAtlas::clear);
    satlas.addFunc("getPageCount", &SpineAtlas::getPageCount);
    satlas.addFunc("getPageName", &SpineAtlas::getPageName);
    satlas.addFunc("getPageWidth", &SpineAtlas::getPageWidth);
    satlas.addFunc("getPageHeight", &SpineAtlas::getPageHeight);
    satlas.addFunc("getRegionCount", &SpineAtlas::getRegionCount);
    satlas.addFunc("findRegion", &SpineAtlas::findRegion);
    satlas.addFunc("getRegionName", &SpineAtlas::getRegionName);
    satlas.addFunc("getRegionPage", &SpineAtlas::getRegionPage);
    satlas.addFunc("getRegionX", &SpineAtlas::getRegionX);
    satlas.addFunc("getRegionY", &SpineAtlas::getRegionY);
    satlas.addFunc("getRegionWidth", &SpineAtlas::getRegionWidth);
    satlas.addFunc("getRegionHeight", &SpineAtlas::getRegionHeight);
    satlas.addFunc("getRegionRotate", &SpineAtlas::getRegionRotate);

    auto sdata = table.addClass<SpineSkeletonData>(
        "SpineSkeletonData",
        std::function<SpineSkeletonData *()>([]() -> SpineSkeletonData * { return nullptr; }),
        true);
    sdata.addFunc("loadFromJson",
                  [](SpineSkeletonData *self, const std::string &json) -> bool {
                      return self->loadFromJson(json, nullptr);
                  });
    sdata.addFunc("loadFromFile",
                  [](SpineSkeletonData *self, const std::string &path) -> bool {
                      return self->loadFromFile(path, nullptr);
                  });
    sdata.addFunc("clear", &SpineSkeletonData::clear);
    sdata.addFunc("getSpineVersion", &SpineSkeletonData::getSpineVersion);
    sdata.addFunc("getBoneCount", &SpineSkeletonData::getBoneCount);
    sdata.addFunc("findBone", &SpineSkeletonData::findBone);
    sdata.addFunc("getBoneName", &SpineSkeletonData::getBoneName);
    sdata.addFunc("getBoneParent", &SpineSkeletonData::getBoneParent);
    sdata.addFunc("getSlotCount", &SpineSkeletonData::getSlotCount);
    sdata.addFunc("findSlot", &SpineSkeletonData::findSlot);
    sdata.addFunc("getSlotName", &SpineSkeletonData::getSlotName);
    sdata.addFunc("getSlotBone", &SpineSkeletonData::getSlotBone);
    sdata.addFunc("getSkinCount", &SpineSkeletonData::getSkinCount);
    sdata.addFunc("findSkin", &SpineSkeletonData::findSkin);
    sdata.addFunc("getSkinName", &SpineSkeletonData::getSkinName);
    sdata.addFunc("getAnimationCount", &SpineSkeletonData::getAnimationCount);
    sdata.addFunc("findAnimation", &SpineSkeletonData::findAnimation);
    sdata.addFunc("getAnimationName", &SpineSkeletonData::getAnimationName);
    sdata.addFunc("getAnimationDuration", &SpineSkeletonData::getAnimationDuration);

    auto ssk = table.addClass<SpineSkeleton>(
        "SpineSkeleton",
        std::function<SpineSkeleton *()>([]() -> SpineSkeleton * { return nullptr; }), true);
    ssk.addFunc("getData", &SpineSkeleton::getData);
    ssk.addFunc("setSkin", &SpineSkeleton::setSkin);
    ssk.addFunc("getSkin", &SpineSkeleton::getSkin);
    ssk.addFunc("setToSetupPose", &SpineSkeleton::setToSetupPose);
    ssk.addFunc("updateWorldTransform", &SpineSkeleton::updateWorldTransform);
    ssk.addFunc("getBoneCount", &SpineSkeleton::getBoneCount);
    ssk.addFunc("getBoneWorldX", &SpineSkeleton::getBoneWorldX);
    ssk.addFunc("getBoneWorldY", &SpineSkeleton::getBoneWorldY);
    ssk.addFunc("getBoneWorldRotation", &SpineSkeleton::getBoneWorldRotation);
    ssk.addFunc("getSlotCount", &SpineSkeleton::getSlotCount);
    ssk.addFunc("getSlotAttachmentName", &SpineSkeleton::getSlotAttachmentName);
    ssk.addFunc("setSlotAttachmentName", &SpineSkeleton::setSlotAttachmentName);

    auto spanim = table.addClass<SpineAnim>(
        "SpineAnim", std::function<SpineAnim *()>([]() -> SpineAnim * { return nullptr; }), true);
    spanim.addFunc("getSkeleton", &SpineAnim::getSkeleton);
    spanim.addFunc("setAtlas", &SpineAnim::setAtlas);
    spanim.addFunc("getAtlas", &SpineAnim::getAtlas);
    spanim.addFunc("setPageTexture", &SpineAnim::setPageTexture);
    spanim.addFunc("setPageTextureByName", &SpineAnim::setPageTextureByName);
    spanim.addFunc("getPageTexture", &SpineAnim::getPageTexture);
    spanim.addFunc("play", &SpineAnim::play);
    spanim.addFunc("stop", &SpineAnim::stop);
    spanim.addFunc("pause", &SpineAnim::pause);
    spanim.addFunc("resume", &SpineAnim::resume);
    spanim.addFunc("setSpeed", &SpineAnim::setSpeed);
    spanim.addFunc("getSpeed", &SpineAnim::getSpeed);
    spanim.addFunc("setTime", &SpineAnim::setTime);
    spanim.addFunc("getTime", &SpineAnim::getTime);
    spanim.addFunc("setLoop", &SpineAnim::setLoop);
    spanim.addFunc("getLoop", &SpineAnim::getLoop);
    spanim.addFunc("setFlipY", &SpineAnim::setFlipY);
    spanim.addFunc("getFlipY", &SpineAnim::getFlipY);
    spanim.addFunc("setPosition", &SpineAnim::setPosition);
    spanim.addFunc("getX", &SpineAnim::getX);
    spanim.addFunc("getY", &SpineAnim::getY);
    spanim.addFunc("setScale", &SpineAnim::setScale);
    spanim.addFunc("getScaleX", &SpineAnim::getScaleX);
    spanim.addFunc("getScaleY", &SpineAnim::getScaleY);
    spanim.addFunc("setLayer", &SpineAnim::setLayer);
    spanim.addFunc("getLayer", &SpineAnim::getLayer);
    spanim.addFunc("setColor", &SpineAnim::setColor);
    spanim.addFunc("isPlaying", &SpineAnim::isPlaying);
    spanim.addFunc("isPaused", &SpineAnim::isPaused);
    spanim.addFunc("isFinished", &SpineAnim::isFinished);
    spanim.addFunc("getAnimation", &SpineAnim::getAnimation);
    spanim.addFunc("getAnimationDuration", &SpineAnim::getAnimationDuration);
    spanim.addFunc("apply", &SpineAnim::apply);
    spanim.addFunc("update", &SpineAnim::update);
    spanim.addFunc("draw", &SpineAnim::draw);
    // collectDrawItems is C++-only (shared DrawItem2D queue); scripts use getDrawSlot*.
    spanim.addFunc("getDrawSlotCount", &SpineAnim::getDrawSlotCount);
    spanim.addFunc("getDrawSlotX", &SpineAnim::getDrawSlotX);
    spanim.addFunc("getDrawSlotY", &SpineAnim::getDrawSlotY);
    spanim.addFunc("getDrawSlotWidth", &SpineAnim::getDrawSlotWidth);
    spanim.addFunc("getDrawSlotHeight", &SpineAnim::getDrawSlotHeight);
    spanim.addFunc("getDrawSlotRotation", &SpineAnim::getDrawSlotRotation);
    spanim.addFunc("getDrawSlotRegion", &SpineAnim::getDrawSlotRegion);
}

void Animation::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Animation::getName);
    cls.addFunc("newTween", &Animation::newTween);
    cls.addFunc("newSpriteSheet", &Animation::newSpriteSheet);
    cls.addFunc("newSpriteSheetFromSequence", &Animation::newSpriteSheetFromSequence);
    cls.addFunc("newSpriteSheetFromAtlasJson", &Animation::newSpriteSheetFromAtlasJson);
    cls.addFunc("getSpriteSequenceCacheCount", &Animation::getSpriteSequenceCacheCount);
    cls.addFunc("getSpriteSequenceCacheBytes", &Animation::getSpriteSequenceCacheBytes);
    cls.addFunc("clearSpriteSequenceCache", &Animation::clearSpriteSequenceCache);
    cls.addFunc("newSpriteClip", &Animation::newSpriteClip);
    cls.addFunc("newSpriteAnim", &Animation::newSpriteAnim);
    cls.addFunc("newSpineAtlas", &Animation::newSpineAtlas);
    cls.addFunc("newSpineSkeletonData", &Animation::newSpineSkeletonData);
    cls.addFunc("newSpineSkeleton", &Animation::newSpineSkeleton);
    cls.addFunc("newSpineAnim", &Animation::newSpineAnim);
    cls.addFunc("newSpineAtlasFromFile", &Animation::newSpineAtlasFromFile);
    cls.addFunc("newSpineAtlasFromText", &Animation::newSpineAtlasFromText);
    cls.addFunc("newSpineSkeletonDataFromFile", &Animation::newSpineSkeletonDataFromFile);
    cls.addFunc("newSpineSkeletonDataFromJson", &Animation::newSpineSkeletonDataFromJson);
    cls.addFunc("newSkeleton", &Animation::newSkeleton);
    cls.addFunc("newClip", &Animation::newClip);
    cls.addFunc("newPose", &Animation::newPose);
    cls.addFunc("newPlayer", &Animation::newPlayer);
    cls.addFunc("newBoneMask", &Animation::newBoneMask);
    cls.addFunc("newLayerMixer", &Animation::newLayerMixer);
    cls.addFunc("newStateMachine", &Animation::newStateMachine);
    cls.addFunc("newMotionDatabase", &Animation::newMotionDatabase);
    cls.addFunc("newMotionMatcher", &Animation::newMotionMatcher);
    cls.addFunc("newControlAnim", &Animation::newControlAnim);
    cls.addFunc("newControlPose", &Animation::newControlPose);
    cls.addFunc("newSkeletonFromModel", &Animation::newSkeletonFromModel);
    cls.addFunc("newClipFromModel", &Animation::newClipFromModel);
    cls.addFunc("newSkeletonFromEvaFile", &Animation::newSkeletonFromEvaFile);
    cls.addFunc("newClipFromEvaFile", &Animation::newClipFromEvaFile);
    cls.addFunc("newSkinFromModel", &Animation::newSkinFromModel);
    cls.addFunc("newLattice", &Animation::newLattice);
    cls.addFunc("newLatticeFromModel", &Animation::newLatticeFromModel);
    cls.addFunc("newTrail", &Animation::newTrail);
    cls.addFunc("update", &Animation::update);
    cls.addFunc("getTweenCount", &Animation::getTweenCount);
    cls.addFunc("getSpriteAnimCount", &Animation::getSpriteAnimCount);
    cls.addFunc("getSpineAnimCount", &Animation::getSpineAnimCount);
    cls.addFunc("getActiveCount", &Animation::getActiveCount);
    cls.addFunc("clearFinished", &Animation::clearFinished);
    cls.addFunc("clearAll", &Animation::clearAll);
}

}  // namespace eve::animation
