#include "asset/SpriteAnimation.h"
#include <algorithm>
#include <cmath>
#include "asset/RuntimeDefinition.h"

namespace eve::asset {
namespace {
template <class T>
Result<T> bad(std::string message) {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::ParseError, std::move(message), {}, {}, "asset.sprite-animation"));
}
const Value* field(const Value& value, const char* name) { return value.isObject() ? value.find(name) : nullptr; }
bool         number(const Value* v, double& n) {
    if (!v || !v->isNumeric()) return false;
    n = v->isInt64() ? double(v->asInt()) : v->asDouble();
    return std::isfinite(n);
}
template <std::size_t N>
bool array(const Value* v, std::array<double, N>& out) {
    const auto* a = v ? v->getIf<Value::Array>() : nullptr;
    if (!a || a->size() != N) return false;
    for (std::size_t i = 0; i < N; ++i)
        if (!number(&(*a)[i], out[i])) return false;
    return true;
}
}  // namespace
Result<SpriteAnimationClip> SpriteAnimationClip::decode(const Value& value) {
    const auto*         schema  = field(value, "schema");
    const auto*         version = field(value, "schemaVersion");
    const auto*         loop    = field(value, "loop");
    const auto*         keys    = field(value, "frames");
    const auto*         frames  = keys ? keys->getIf<Value::Array>() : nullptr;
    SpriteAnimationClip out;
    if (!schema || !schema->isString() || schema->asString() != "eve.sprite-animation" || !version ||
        !version->isInt64() || version->asInt() != 1 || !loop || !loop->isBool() ||
        !number(field(value, "duration"), out.duration_) || out.duration_ <= 0 || !frames || frames->empty() ||
        frames->size() > 100000)
        return bad<SpriteAnimationClip>("invalid sprite timeline");
    out.loop_ = loop->asBool();
    for (const auto& key : *frames) {
        const auto* image  = field(key, "image");
        const auto* name   = field(key, "name");
        const auto* filter = field(key, "filter");
        if (!image || !image->isString()) return bad<SpriteAnimationClip>("missing sprite image reference");
        auto ref = AssetRef::parse(image->asString());
        if (!ref) return Result<SpriteAnimationClip>::failure(ref.status());
        SpriteAnimationFrame frame{std::move(ref).takeValue()};
        if (!image || !image->isString() || !name || !name->isString() || !filter || !filter->isString() ||
            (filter->asString() != "nearest" && filter->asString() != "linear") ||
            !number(field(key, "time"), frame.time) || !number(field(key, "pixelsPerUnit"), frame.pixelsPerUnit) ||
            !array(field(key, "rect"), frame.rect) || !array(field(key, "pivot"), frame.pivot) ||
            !array(field(key, "imageSize"), frame.imageSize))
            return bad<SpriteAnimationClip>("invalid sprite key fields");
        frame.name    = name->asString();
        frame.nearest = filter->asString() == "nearest";
        if (frame.time < 0 || frame.time >= out.duration_ ||
            (out.frames_.empty() ? frame.time != 0 : frame.time <= out.frames_.back().time) ||
            frame.pixelsPerUnit <= 0 || frame.rect[0] < 0 || frame.rect[1] < 0 || frame.rect[2] <= 0 ||
            frame.rect[3] <= 0 || frame.imageSize[0] <= 0 || frame.imageSize[1] <= 0 ||
            frame.rect[0] + frame.rect[2] > frame.imageSize[0] || frame.rect[1] + frame.rect[3] > frame.imageSize[1] ||
            frame.pivot[0] < 0 || frame.pivot[0] > 1 || frame.pivot[1] < 0 || frame.pivot[1] > 1)
            return bad<SpriteAnimationClip>("sprite key bounds or timing invalid");
        out.frames_.push_back(std::move(frame));
    }
    return Result<SpriteAnimationClip>::success(std::move(out));
}
Result<SpriteAnimationClip> SpriteAnimationClip::load(const EvpackResourceReader& reader, const AssetRef& ref,
                                                      const EvpackCapabilities& caps) {
    auto payload = reader.read(ref, "eve.sprite-animation/1", caps, 64 * 1024 * 1024);
    if (!payload) return Result<SpriteAnimationClip>::failure(payload.status());
    if (payload.value().chunks.size() != 1 || payload.value().chunks.front().kind != EvpackChunkKind::Definition)
        return bad<SpriteAnimationClip>("sprite timeline requires one definition");
    auto value = decodeRuntimeDefinition(payload.value().chunks.front().bytes);
    if (!value) return Result<SpriteAnimationClip>::failure(value.status());
    return decode(value.value());
}
Result<SpriteAnimationFrame> SpriteAnimationClip::sample(double seconds) const {
    if (!std::isfinite(seconds) || seconds < 0 || frames_.empty())
        return bad<SpriteAnimationFrame>("invalid sprite sample time or empty clip");
    const double time = loop_ ? std::fmod(seconds, duration_) : seconds;
    auto         it   = std::upper_bound(frames_.begin(), frames_.end(), time,
                                         [](double t, const auto& frame) { return t < frame.time; });
    return Result<SpriteAnimationFrame>::success(*(it == frames_.begin() ? it : std::prev(it)));
}
}  // namespace eve::asset
