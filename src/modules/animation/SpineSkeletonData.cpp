#include "animation/SpineSkeletonData.h"

#include "common/Exception.h"
#include "common/Module.h"
#include "data/DataModule.h"
#include "data/JsonDocument.h"
#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace eve::animation {
namespace {

float asFloat(const Poco::Dynamic::Var &v, float fallback = 0.f) {
    try {
        if (v.isEmpty()) return fallback;
        return v.convert<float>();
    } catch (...) {
        return fallback;
    }
}

std::string asString(const Poco::Dynamic::Var &v, const std::string &fallback = {}) {
    try {
        if (v.isEmpty()) return fallback;
        return v.convert<std::string>();
    } catch (...) {
        return fallback;
    }
}

bool keyStepped(Poco::JSON::Object::Ptr obj) {
    if (!obj || !obj->has("curve")) return false;
    try {
        return asString(obj->get("curve")) == "stepped";
    } catch (...) {
        return false;
    }
}

}  // namespace


void SpineSkeletonData::checkBone(int index) const {
    if (index < 0 || index >= getBoneCount())
        throw Exception("SpineSkeletonData: bone index %d out of range", index);
}
void SpineSkeletonData::checkSlot(int index) const {
    if (index < 0 || index >= getSlotCount())
        throw Exception("SpineSkeletonData: slot index %d out of range", index);
}
void SpineSkeletonData::checkSkin(int index) const {
    if (index < 0 || index >= getSkinCount())
        throw Exception("SpineSkeletonData: skin index %d out of range", index);
}
void SpineSkeletonData::checkAnim(int index) const {
    if (index < 0 || index >= getAnimationCount())
        throw Exception("SpineSkeletonData: animation index %d out of range", index);
}

void SpineSkeletonData::clear() {
    spineVersion_.clear();
    bones_.clear();
    slots_.clear();
    skins_.clear();
    anims_.clear();
    boneByName_.clear();
    slotByName_.clear();
    skinByName_.clear();
    animByName_.clear();
    defaultSkin_ = -1;
}

int SpineSkeletonData::findBone(const std::string &name) const {
    auto it = boneByName_.find(name);
    return it == boneByName_.end() ? -1 : it->second;
}
int SpineSkeletonData::findSlot(const std::string &name) const {
    auto it = slotByName_.find(name);
    return it == slotByName_.end() ? -1 : it->second;
}
int SpineSkeletonData::findSkin(const std::string &name) const {
    auto it = skinByName_.find(name);
    return it == skinByName_.end() ? -1 : it->second;
}
int SpineSkeletonData::findAnimation(const std::string &name) const {
    auto it = animByName_.find(name);
    return it == animByName_.end() ? -1 : it->second;
}

std::string SpineSkeletonData::getBoneName(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].name;
}
int SpineSkeletonData::getBoneParent(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].parent;
}
float SpineSkeletonData::getBoneX(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].x;
}
float SpineSkeletonData::getBoneY(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].y;
}
float SpineSkeletonData::getBoneRotation(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].rotation;
}
float SpineSkeletonData::getBoneScaleX(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].scaleX;
}
float SpineSkeletonData::getBoneScaleY(int index) const {
    checkBone(index);
    return bones_[static_cast<size_t>(index)].scaleY;
}

std::string SpineSkeletonData::getSlotName(int index) const {
    checkSlot(index);
    return slots_[static_cast<size_t>(index)].name;
}
int SpineSkeletonData::getSlotBone(int index) const {
    checkSlot(index);
    return slots_[static_cast<size_t>(index)].bone;
}
std::string SpineSkeletonData::getSlotAttachment(int index) const {
    checkSlot(index);
    return slots_[static_cast<size_t>(index)].attachment;
}

std::string SpineSkeletonData::getSkinName(int index) const {
    checkSkin(index);
    return skins_[static_cast<size_t>(index)].name;
}
std::string SpineSkeletonData::getAnimationName(int index) const {
    checkAnim(index);
    return anims_[static_cast<size_t>(index)].name;
}
float SpineSkeletonData::getAnimationDuration(int index) const {
    checkAnim(index);
    return anims_[static_cast<size_t>(index)].duration;
}

const SpineSkeletonData::RegionAttachment *SpineSkeletonData::findAttachment(
    int skinIndex, int slotIndex, const std::string &name) const {
    if (skinIndex < 0 || skinIndex >= getSkinCount()) return nullptr;
    if (name.empty()) return nullptr;
    const SkinData &sk = skins_[static_cast<size_t>(skinIndex)];
    auto sit           = sk.attachments.find(slotIndex);
    if (sit == sk.attachments.end()) return nullptr;
    auto ait = sit->second.find(name);
    if (ait == sit->second.end()) return nullptr;
    return &ait->second;
}

bool SpineSkeletonData::loadFromFile(const std::string &path, std::string *error) {
    if (path.empty()) {
        if (error) *error = "empty path";
        return false;
    }
    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();
    std::unique_ptr<eve::filesystem::FileData> data;
    try {
        data.reset(fs->read(path));
    } catch (...) {
        if (error) *error = "read failed: " + path;
        return false;
    }
    if (!data || data->getSize() == 0) {
        if (error) *error = "empty file: " + path;
        return false;
    }
    std::string text(static_cast<const char *>(data->getData()),
                     static_cast<size_t>(data->getSize()));
    return loadFromJson(text, error);
}

bool SpineSkeletonData::loadFromJson(const std::string &json, std::string *error) {
    clear();
    auto *dm = eve::data::DataModule::create();
    std::string err;
    std::unique_ptr<data::JsonDocument> doc(dm->decodeJson(json, &err));
    if (!doc || !doc->isObject()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return false;
    }
    Poco::JSON::Object::Ptr root = doc->object();

    try {
        if (root->has("skeleton")) {
            auto skel = root->getObject("skeleton");
            if (skel) spineVersion_ = asString(skel->get("spine"));
        }

        // Bones
        if (root->has("bones")) {
            auto arr = root->getArray("bones");
            if (arr) {
                for (size_t i = 0; i < arr->size(); ++i) {
                    auto obj = arr->getObject(static_cast<unsigned>(i));
                    if (!obj) continue;
                    BoneData b;
                    b.name = asString(obj->get("name"));
                    if (b.name.empty()) continue;
                    if (obj->has("parent")) {
                        std::string parent = asString(obj->get("parent"));
                        b.parent           = findBone(parent);
                        if (b.parent < 0) {
                            if (error) *error = "unknown bone parent: " + parent;
                            clear();
                            return false;
                        }
                    }
                    b.x        = asFloat(obj->get("x"));
                    b.y        = asFloat(obj->get("y"));
                    b.rotation = asFloat(obj->get("rotation"));
                    b.scaleX   = obj->has("scaleX") ? asFloat(obj->get("scaleX"), 1.f) : 1.f;
                    b.scaleY   = obj->has("scaleY") ? asFloat(obj->get("scaleY"), 1.f) : 1.f;
                    boneByName_[b.name] = static_cast<int>(bones_.size());
                    bones_.push_back(std::move(b));
                }
            }
        }
        if (bones_.empty()) {
            BoneData rootBone;
            rootBone.name = "root";
            boneByName_["root"] = 0;
            bones_.push_back(rootBone);
        }

        // Slots
        if (root->has("slots")) {
            auto arr = root->getArray("slots");
            if (arr) {
                for (size_t i = 0; i < arr->size(); ++i) {
                    auto obj = arr->getObject(static_cast<unsigned>(i));
                    if (!obj) continue;
                    SlotData s;
                    s.name = asString(obj->get("name"));
                    std::string boneName = asString(obj->get("bone"));
                    s.bone               = findBone(boneName);
                    if (s.bone < 0) s.bone = 0;
                    if (obj->has("attachment")) s.attachment = asString(obj->get("attachment"));
                    slotByName_[s.name] = static_cast<int>(slots_.size());
                    slots_.push_back(std::move(s));
                }
            }
        }

        auto parseRegion = [&](const std::string &attName, Poco::JSON::Object::Ptr att)
            -> RegionAttachment {
            RegionAttachment r;
            r.name     = attName;
            r.path     = att->has("path") ? asString(att->get("path")) : attName;
            r.x        = asFloat(att->get("x"));
            r.y        = asFloat(att->get("y"));
            r.rotation = asFloat(att->get("rotation"));
            r.width    = asFloat(att->get("width"));
            r.height   = asFloat(att->get("height"));
            r.scaleX   = att->has("scaleX") ? asFloat(att->get("scaleX"), 1.f) : 1.f;
            r.scaleY   = att->has("scaleY") ? asFloat(att->get("scaleY"), 1.f) : 1.f;
            return r;
        };

        auto addSkinAttachments = [&](SkinData &skin, Poco::JSON::Object::Ptr attachmentsRoot) {
            if (!attachmentsRoot) return;
            // Format A (3.8+): attachments: { slot: { att: {...} } }
            // Format B (older skins object): same without wrapper
            for (const auto &slotName : attachmentsRoot->getNames()) {
                int slotIndex = findSlot(slotName);
                if (slotIndex < 0) continue;
                auto slotObj = attachmentsRoot->getObject(slotName);
                if (!slotObj) continue;
                for (const auto &attName : slotObj->getNames()) {
                    auto attObj = slotObj->getObject(attName);
                    if (!attObj) continue;
                    // Skip non-region types (mesh, boundingbox, ...)
                    std::string type = attObj->has("type") ? asString(attObj->get("type")) : "region";
                    if (!type.empty() && type != "region") continue;
                    skin.attachments[slotIndex][attName] = parseRegion(attName, attObj);
                }
            }
        };

        // Skins — array form (3.8+) or object form
        if (root->has("skins")) {
            if (root->isArray("skins")) {
                auto arr = root->getArray("skins");
                for (size_t i = 0; i < arr->size(); ++i) {
                    auto obj = arr->getObject(static_cast<unsigned>(i));
                    if (!obj) continue;
                    SkinData skin;
                    skin.name = asString(obj->get("name"));
                    if (skin.name.empty()) skin.name = "default";
                    if (obj->has("attachments"))
                        addSkinAttachments(skin, obj->getObject("attachments"));
                    skinByName_[skin.name] = static_cast<int>(skins_.size());
                    skins_.push_back(std::move(skin));
                }
            } else {
                auto obj = root->getObject("skins");
                if (obj) {
                    for (const auto &skinName : obj->getNames()) {
                        SkinData skin;
                        skin.name = skinName;
                        addSkinAttachments(skin, obj->getObject(skinName));
                        skinByName_[skin.name] = static_cast<int>(skins_.size());
                        skins_.push_back(std::move(skin));
                    }
                }
            }
        }
        if (skins_.empty()) {
            SkinData skin;
            skin.name = "default";
            skinByName_["default"] = 0;
            skins_.push_back(skin);
        }
        defaultSkin_ = findSkin("default");
        if (defaultSkin_ < 0) defaultSkin_ = 0;

        // Animations
        if (root->has("animations")) {
            auto animsObj = root->getObject("animations");
            if (animsObj) {
                for (const auto &animName : animsObj->getNames()) {
                    auto animObj = animsObj->getObject(animName);
                    if (!animObj) continue;
                    AnimationData anim;
                    anim.name = animName;
                    float maxT = 0.f;

                    if (animObj->has("bones")) {
                        auto bonesObj = animObj->getObject("bones");
                        if (bonesObj) {
                            for (const auto &boneName : bonesObj->getNames()) {
                                int bi = findBone(boneName);
                                if (bi < 0) continue;
                                auto btObj = bonesObj->getObject(boneName);
                                if (!btObj) continue;
                                BoneTimeline bt;
                                bt.boneIndex = bi;

                                if (btObj->has("rotate")) {
                                    auto arr = btObj->getArray("rotate");
                                    if (arr) {
                                        for (size_t i = 0; i < arr->size(); ++i) {
                                            auto k = arr->getObject(static_cast<unsigned>(i));
                                            if (!k) continue;
                                            FloatKey fk;
                                            fk.time    = asFloat(k->get("time"));
                                            fk.value   = asFloat(k->get("value"),
                                                               asFloat(k->get("angle")));
                                            fk.stepped = keyStepped(k);
                                            maxT       = std::max(maxT, fk.time);
                                            bt.rotate.push_back(fk);
                                        }
                                    }
                                }
                                if (btObj->has("translate")) {
                                    auto arr = btObj->getArray("translate");
                                    if (arr) {
                                        for (size_t i = 0; i < arr->size(); ++i) {
                                            auto k = arr->getObject(static_cast<unsigned>(i));
                                            if (!k) continue;
                                            TranslateKey tk;
                                            tk.time    = asFloat(k->get("time"));
                                            tk.x       = asFloat(k->get("x"));
                                            tk.y       = asFloat(k->get("y"));
                                            tk.stepped = keyStepped(k);
                                            maxT       = std::max(maxT, tk.time);
                                            bt.translate.push_back(tk);
                                        }
                                    }
                                }
                                if (btObj->has("scale")) {
                                    auto arr = btObj->getArray("scale");
                                    if (arr) {
                                        for (size_t i = 0; i < arr->size(); ++i) {
                                            auto k = arr->getObject(static_cast<unsigned>(i));
                                            if (!k) continue;
                                            ScaleKey sk;
                                            sk.time    = asFloat(k->get("time"));
                                            sk.x = k->has("x") ? asFloat(k->get("x"), 1.f) : 1.f;
                                            sk.y = k->has("y") ? asFloat(k->get("y"), 1.f) : 1.f;
                                            sk.stepped = keyStepped(k);
                                            maxT       = std::max(maxT, sk.time);
                                            bt.scale.push_back(sk);
                                        }
                                    }
                                }
                                anim.bones.push_back(std::move(bt));
                            }
                        }
                    }

                    if (animObj->has("slots")) {
                        auto slotsObj = animObj->getObject("slots");
                        if (slotsObj) {
                            for (const auto &slotName : slotsObj->getNames()) {
                                int si = findSlot(slotName);
                                if (si < 0) continue;
                                auto stObj = slotsObj->getObject(slotName);
                                if (!stObj) continue;
                                SlotTimeline st;
                                st.slotIndex = si;
                                if (stObj->has("attachment")) {
                                    auto arr = stObj->getArray("attachment");
                                    if (arr) {
                                        for (size_t i = 0; i < arr->size(); ++i) {
                                            auto k = arr->getObject(static_cast<unsigned>(i));
                                            if (!k) continue;
                                            AttachmentKey ak;
                                            ak.time = asFloat(k->get("time"));
                                            ak.name = k->has("name") ? asString(k->get("name")) : "";
                                            maxT    = std::max(maxT, ak.time);
                                            st.attachment.push_back(ak);
                                        }
                                    }
                                }
                                anim.slots.push_back(std::move(st));
                            }
                        }
                    }

                    anim.duration               = maxT;
                    animByName_[anim.name]      = static_cast<int>(anims_.size());
                    anims_.push_back(std::move(anim));
                }
            }
        }
    } catch (const std::exception &ex) {
        if (error) *error = ex.what();
        clear();
        return false;
    }

    return true;
}

}  // namespace eve::animation
