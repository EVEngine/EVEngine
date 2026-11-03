#include "common/ActionGuidePack.h"

#include "common/Diagnostic.h"

#include <cmath>
#include <set>
#include <string>
#include <utility>

namespace eve {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<void> invalidVoid(std::string message, std::string path) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

const Value* field(const Value& value, std::string_view name) {
    if (!value.isObject()) return nullptr;
    return value.find(std::string(name));
}

Result<void> rejectUnknown(const Value& value, const std::set<std::string, std::less<>>& known, std::string path) {
    if (!value.isObject()) return invalidVoid("expected object", std::move(path));
    for (const auto& [name, item] : *value.getIf<Value::Object>()) {
        (void)item;
        if (!known.contains(name)) return invalidVoid("unknown field", path + "." + name);
    }
    return Result<void>::success();
}

Result<std::string> text(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || !item->isString()) return invalid<std::string>("expected text field", std::move(path));
    return Result<std::string>::success(item->asString());
}

Result<std::int64_t> integer(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || !item->isInt64()) return invalid<std::int64_t>("expected integer field", std::move(path));
    return Result<std::int64_t>::success(item->asInt());
}

Result<bool> boolean(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || !item->isBool()) return invalid<bool>("expected boolean field", std::move(path));
    return Result<bool>::success(item->asBool());
}

Result<double> number(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || (!item->isDouble() && !item->isInt64()))
        return invalid<double>("expected numeric field", std::move(path));
    const double result = item->isDouble() ? item->asDouble() : static_cast<double>(item->asInt());
    if (!std::isfinite(result)) return invalid<double>("numeric field must be finite", std::move(path));
    return Result<double>::success(result);
}

Result<std::array<double, 3>> vec3(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || !item->isArray() || item->getIf<Value::Array>()->size() != 3)
        return invalid<std::array<double, 3>>("expected three-component array", std::move(path));
    std::array<double, 3> result{};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& component = item->getIf<Value::Array>()->at(i);
        if (!component.isDouble() && !component.isInt64())
            return invalid<std::array<double, 3>>("vector component must be numeric", path);
        result[i] = component.isDouble() ? component.asDouble() : static_cast<double>(component.asInt());
        if (!std::isfinite(result[i])) return invalid<std::array<double, 3>>("vector component must be finite", path);
    }
    return Result<std::array<double, 3>>::success(result);
}

Result<std::array<double, 4>> vec4(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || !item->isArray() || item->getIf<Value::Array>()->size() != 4)
        return invalid<std::array<double, 4>>("expected four-component array", std::move(path));
    std::array<double, 4> result{};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& component = item->getIf<Value::Array>()->at(i);
        if (!component.isDouble() && !component.isInt64())
            return invalid<std::array<double, 4>>("vector component must be numeric", path);
        result[i] = component.isDouble() ? component.asDouble() : static_cast<double>(component.asInt());
        if (!std::isfinite(result[i])) return invalid<std::array<double, 4>>("vector component must be finite", path);
    }
    return Result<std::array<double, 4>>::success(result);
}

Result<std::vector<std::string>> stringArray(const Value& parent, std::string_view name, std::string path) {
    const auto* source = field(parent, name);
    if (!source || !source->isArray()) return invalid<std::vector<std::string>>("expected text array", path);
    std::vector<std::string> result;
    result.reserve(source->getIf<Value::Array>()->size());
    for (std::size_t i = 0; i < source->getIf<Value::Array>()->size(); ++i) {
        const auto& item = source->getIf<Value::Array>()->at(i);
        if (!item.isString())
            return invalid<std::vector<std::string>>("expected text item", path + "[" + std::to_string(i) + "]");
        result.push_back(item.asString());
    }
    return Result<std::vector<std::string>>::success(std::move(result));
}

Value::Array strings(const std::vector<std::string>& values) {
    Value::Array result;
    result.reserve(values.size());
    for (const auto& value : values) result.emplace_back(value);
    return result;
}

Value::Array array3(const std::array<double, 3>& value) { return Value::Array{value[0], value[1], value[2]}; }

Value::Array array4(const std::array<double, 4>& value) { return Value::Array{value[0], value[1], value[2], value[3]}; }

std::string_view rootModeName(ActionGuideRootMode mode) {
    switch (mode) {
        case ActionGuideRootMode::InPlace: return "inPlace";
        case ActionGuideRootMode::WorldRoot: return "worldRoot";
    }
    return "inPlace";
}

Result<ActionGuideRootMode> parseRootMode(std::string_view text) {
    if (text == "inPlace") return Result<ActionGuideRootMode>::success(ActionGuideRootMode::InPlace);
    if (text == "worldRoot") return Result<ActionGuideRootMode>::success(ActionGuideRootMode::WorldRoot);
    return invalid<ActionGuideRootMode>("unknown rootMode", "rootMode");
}

std::string_view projectionName(ActionGuideProjection projection) {
    switch (projection) {
        case ActionGuideProjection::Orthographic: return "orthographic";
        case ActionGuideProjection::Perspective: return "perspective";
    }
    return "orthographic";
}

Result<ActionGuideProjection> parseProjection(std::string_view text) {
    if (text == "orthographic") return Result<ActionGuideProjection>::success(ActionGuideProjection::Orthographic);
    if (text == "perspective") return Result<ActionGuideProjection>::success(ActionGuideProjection::Perspective);
    return invalid<ActionGuideProjection>("unknown projection", "camera.projection");
}

bool allowedAov(std::string_view name) {
    return name == "silhouette" || name == "depth" || name == "normal" || name == "bone" || name == "partId" ||
           name == "final";
}

Result<void> validatePack(const ActionGuidePack& pack) {
    if (pack.revision == 0) return invalidVoid("revision must be positive", "revision");
    if (pack.subject.domain.empty()) return invalidVoid("subject.domain is required", "subject.domain");
    if (pack.skeletonRef.empty()) return invalidVoid("skeletonRef is required", "skeletonRef");
    if (pack.clipRef.empty()) return invalidVoid("clipRef is required", "clipRef");
    if (pack.canvas.width <= 0 || pack.canvas.height <= 0) return invalidVoid("canvas size must be positive", "canvas");
    if (!(pack.canvas.pixelsPerUnit > 0.0) || !std::isfinite(pack.canvas.pixelsPerUnit))
        return invalidVoid("canvas.pixelsPerUnit must be finite and > 0", "canvas.pixelsPerUnit");
    for (double channel : pack.canvas.backgroundRgba) {
        if (!std::isfinite(channel) || channel < 0.0 || channel > 1.0)
            return invalidVoid("canvas.backgroundRgba channels must be in [0,1]", "canvas.backgroundRgba");
    }
    if (!(pack.camera.nearPlane > 0.0) || !(pack.camera.farPlane > pack.camera.nearPlane) ||
        !std::isfinite(pack.camera.nearPlane) || !std::isfinite(pack.camera.farPlane))
        return invalidVoid("camera near/far must be finite with far > near > 0", "camera");
    if (pack.camera.projection == ActionGuideProjection::Orthographic) {
        if (!(pack.camera.orthoSize > 0.0) || !std::isfinite(pack.camera.orthoSize))
            return invalidVoid("camera.orthoSize must be finite and > 0", "camera.orthoSize");
    } else if (!(pack.camera.fovYDegrees > 0.0) || !(pack.camera.fovYDegrees < 180.0) ||
               !std::isfinite(pack.camera.fovYDegrees)) {
        return invalidVoid("camera.fovYDegrees must be in (0,180)", "camera.fovYDegrees");
    }
    const auto toEye =
        std::array{pack.camera.target[0] - pack.camera.eye[0], pack.camera.target[1] - pack.camera.eye[1],
                   pack.camera.target[2] - pack.camera.eye[2]};
    const double eyeLen2 = toEye[0] * toEye[0] + toEye[1] * toEye[1] + toEye[2] * toEye[2];
    if (!(eyeLen2 > 1e-12)) return invalidVoid("camera eye and target must differ", "camera");
    if (pack.shots.empty()) return invalidVoid("shots must be non-empty", "shots");
    std::set<std::string, std::less<>> shotIds;
    for (std::size_t i = 0; i < pack.shots.size(); ++i) {
        const auto&       shot = pack.shots[i];
        const std::string path = "shots[" + std::to_string(i) + "]";
        if (shot.shotId.empty()) return invalidVoid("shotId is required", path + ".shotId");
        if (!shotIds.insert(shot.shotId).second) return invalidVoid("duplicate shotId", path + ".shotId");
        if (!std::isfinite(shot.timeSeconds) || shot.timeSeconds < 0.0)
            return invalidVoid("timeSeconds must be finite and >= 0", path + ".timeSeconds");
        if (shot.sampleIndex && *shot.sampleIndex < 0)
            return invalidVoid("sampleIndex must be >= 0", path + ".sampleIndex");
        if (shot.view.empty()) return invalidVoid("view is required", path + ".view");
        if (shot.aovs.empty()) return invalidVoid("aovs must be non-empty", path + ".aovs");
        for (std::size_t a = 0; a < shot.aovs.size(); ++a) {
            if (!allowedAov(shot.aovs[a]))
                return invalidVoid("unsupported aov", path + ".aovs[" + std::to_string(a) + "]");
        }
    }
    if (pack.actionBinding.abilityId.empty())
        return invalidVoid("actionBinding.abilityId is required", "actionBinding.abilityId");
    if (pack.actionBinding.clipName.empty())
        return invalidVoid("actionBinding.clipName is required", "actionBinding.clipName");
    if (!(pack.actionBinding.fps > 0.0) || !std::isfinite(pack.actionBinding.fps))
        return invalidVoid("actionBinding.fps must be finite and > 0", "actionBinding.fps");
    for (std::size_t i = 0; i < pack.actionBinding.phaseMarks.size(); ++i) {
        const auto&       mark = pack.actionBinding.phaseMarks[i];
        const std::string path = "actionBinding.phaseMarks[" + std::to_string(i) + "]";
        if (mark.name.empty()) return invalidVoid("phase mark name is required", path + ".name");
        if (!std::isfinite(mark.timeSeconds) || mark.timeSeconds < 0.0)
            return invalidVoid("phase mark timeSeconds must be finite and >= 0", path + ".timeSeconds");
    }
    if (!std::isfinite(pack.qc.minSilhouetteIoU) || pack.qc.minSilhouetteIoU < 0.0 || pack.qc.minSilhouetteIoU > 1.0)
        return invalidVoid("qc.minSilhouetteIoU must be in [0,1]", "qc.minSilhouetteIoU");
    if (!std::isfinite(pack.qc.maxJointErrorPx) || pack.qc.maxJointErrorPx < 0.0)
        return invalidVoid("qc.maxJointErrorPx must be finite and >= 0", "qc.maxJointErrorPx");
    if (pack.qc.maxRetries < 0) return invalidVoid("qc.maxRetries must be >= 0", "qc.maxRetries");
    return Result<void>::success();
}

Result<ActionGuideSubject> decodeSubject(const Value& value) {
    auto unknown = rejectUnknown(value, {"domain", "id", "generation"}, "subject");
    if (!unknown) return Result<ActionGuideSubject>::failure(unknown.status());
    auto domain     = text(value, "domain", "subject.domain");
    auto id         = integer(value, "id", "subject.id");
    auto generation = integer(value, "generation", "subject.generation");
    if (!domain) return Result<ActionGuideSubject>::failure(domain.status());
    if (!id) return Result<ActionGuideSubject>::failure(id.status());
    if (!generation) return Result<ActionGuideSubject>::failure(generation.status());
    if (id.value() < 0 || generation.value() < 0)
        return invalid<ActionGuideSubject>("subject id/generation must be >= 0", "subject");
    ActionGuideSubject subject;
    subject.domain     = std::move(domain).takeValue();
    subject.id         = static_cast<std::uint64_t>(id.value());
    subject.generation = static_cast<std::uint64_t>(generation.value());
    return Result<ActionGuideSubject>::success(std::move(subject));
}

Result<ActionGuideCanvas> decodeCanvas(const Value& value) {
    auto unknown =
        rejectUnknown(value, {"width", "height", "pixelsPerUnit", "backgroundRgba", "transparent"}, "canvas");
    if (!unknown) return Result<ActionGuideCanvas>::failure(unknown.status());
    auto width       = integer(value, "width", "canvas.width");
    auto height      = integer(value, "height", "canvas.height");
    auto ppu         = number(value, "pixelsPerUnit", "canvas.pixelsPerUnit");
    auto rgba        = vec4(value, "backgroundRgba", "canvas.backgroundRgba");
    auto transparent = boolean(value, "transparent", "canvas.transparent");
    if (!width) return Result<ActionGuideCanvas>::failure(width.status());
    if (!height) return Result<ActionGuideCanvas>::failure(height.status());
    if (!ppu) return Result<ActionGuideCanvas>::failure(ppu.status());
    if (!rgba) return Result<ActionGuideCanvas>::failure(rgba.status());
    if (!transparent) return Result<ActionGuideCanvas>::failure(transparent.status());
    ActionGuideCanvas canvas;
    canvas.width          = width.value();
    canvas.height         = height.value();
    canvas.pixelsPerUnit  = ppu.value();
    canvas.backgroundRgba = rgba.value();
    canvas.transparent    = transparent.value();
    return Result<ActionGuideCanvas>::success(std::move(canvas));
}

Result<ActionGuideCamera> decodeCamera(const Value& value) {
    auto unknown = rejectUnknown(
        value, {"projection", "fovYDegrees", "orthoSize", "nearPlane", "farPlane", "eye", "target", "up"}, "camera");
    if (!unknown) return Result<ActionGuideCamera>::failure(unknown.status());
    auto projectionText = text(value, "projection", "camera.projection");
    auto fov            = number(value, "fovYDegrees", "camera.fovYDegrees");
    auto ortho          = number(value, "orthoSize", "camera.orthoSize");
    auto nearPlane      = number(value, "nearPlane", "camera.nearPlane");
    auto farPlane       = number(value, "farPlane", "camera.farPlane");
    auto eye            = vec3(value, "eye", "camera.eye");
    auto target         = vec3(value, "target", "camera.target");
    auto up             = vec3(value, "up", "camera.up");
    if (!projectionText) return Result<ActionGuideCamera>::failure(projectionText.status());
    if (!fov) return Result<ActionGuideCamera>::failure(fov.status());
    if (!ortho) return Result<ActionGuideCamera>::failure(ortho.status());
    if (!nearPlane) return Result<ActionGuideCamera>::failure(nearPlane.status());
    if (!farPlane) return Result<ActionGuideCamera>::failure(farPlane.status());
    if (!eye) return Result<ActionGuideCamera>::failure(eye.status());
    if (!target) return Result<ActionGuideCamera>::failure(target.status());
    if (!up) return Result<ActionGuideCamera>::failure(up.status());
    auto projection = parseProjection(projectionText.value());
    if (!projection) return Result<ActionGuideCamera>::failure(projection.status());
    ActionGuideCamera camera;
    camera.projection  = projection.value();
    camera.fovYDegrees = fov.value();
    camera.orthoSize   = ortho.value();
    camera.nearPlane   = nearPlane.value();
    camera.farPlane    = farPlane.value();
    camera.eye         = eye.value();
    camera.target      = target.value();
    camera.up          = up.value();
    return Result<ActionGuideCamera>::success(std::move(camera));
}

Result<ActionGuideShot> decodeShot(const Value& value, std::string path) {
    auto unknown = rejectUnknown(value, {"shotId", "timeSeconds", "sampleIndex", "view", "aovs", "tags"}, path);
    if (!unknown) return Result<ActionGuideShot>::failure(unknown.status());
    auto shotId      = text(value, "shotId", path + ".shotId");
    auto timeSeconds = number(value, "timeSeconds", path + ".timeSeconds");
    auto view        = text(value, "view", path + ".view");
    auto aovs        = stringArray(value, "aovs", path + ".aovs");
    auto tags        = stringArray(value, "tags", path + ".tags");
    if (!shotId) return Result<ActionGuideShot>::failure(shotId.status());
    if (!timeSeconds) return Result<ActionGuideShot>::failure(timeSeconds.status());
    if (!view) return Result<ActionGuideShot>::failure(view.status());
    if (!aovs) return Result<ActionGuideShot>::failure(aovs.status());
    if (!tags) return Result<ActionGuideShot>::failure(tags.status());
    ActionGuideShot shot;
    shot.shotId      = std::move(shotId).takeValue();
    shot.timeSeconds = timeSeconds.value();
    shot.view        = std::move(view).takeValue();
    shot.aovs        = std::move(aovs).takeValue();
    shot.tags        = std::move(tags).takeValue();
    if (const auto* sample = field(value, "sampleIndex")) {
        if (!sample->isInt64()) return invalid<ActionGuideShot>("expected integer field", path + ".sampleIndex");
        shot.sampleIndex = sample->asInt();
    }
    return Result<ActionGuideShot>::success(std::move(shot));
}

Result<ActionGuidePhaseMark> decodePhaseMark(const Value& value, std::string path) {
    auto unknown = rejectUnknown(value, {"name", "timeSeconds"}, path);
    if (!unknown) return Result<ActionGuidePhaseMark>::failure(unknown.status());
    auto name        = text(value, "name", path + ".name");
    auto timeSeconds = number(value, "timeSeconds", path + ".timeSeconds");
    if (!name) return Result<ActionGuidePhaseMark>::failure(name.status());
    if (!timeSeconds) return Result<ActionGuidePhaseMark>::failure(timeSeconds.status());
    ActionGuidePhaseMark mark;
    mark.name        = std::move(name).takeValue();
    mark.timeSeconds = timeSeconds.value();
    return Result<ActionGuidePhaseMark>::success(std::move(mark));
}

Result<ActionGuideBinding> decodeBinding(const Value& value) {
    auto unknown = rejectUnknown(value, {"abilityId", "clipName", "fps", "loop", "phaseMarks"}, "actionBinding");
    if (!unknown) return Result<ActionGuideBinding>::failure(unknown.status());
    auto abilityId = text(value, "abilityId", "actionBinding.abilityId");
    auto clipName  = text(value, "clipName", "actionBinding.clipName");
    auto fps       = number(value, "fps", "actionBinding.fps");
    auto loop      = boolean(value, "loop", "actionBinding.loop");
    if (!abilityId) return Result<ActionGuideBinding>::failure(abilityId.status());
    if (!clipName) return Result<ActionGuideBinding>::failure(clipName.status());
    if (!fps) return Result<ActionGuideBinding>::failure(fps.status());
    if (!loop) return Result<ActionGuideBinding>::failure(loop.status());
    const auto* marksValue = field(value, "phaseMarks");
    if (!marksValue || !marksValue->isArray())
        return invalid<ActionGuideBinding>("expected phaseMarks array", "actionBinding.phaseMarks");
    ActionGuideBinding binding;
    binding.abilityId = std::move(abilityId).takeValue();
    binding.clipName  = std::move(clipName).takeValue();
    binding.fps       = fps.value();
    binding.loop      = loop.value();
    for (std::size_t i = 0; i < marksValue->getIf<Value::Array>()->size(); ++i) {
        const auto& item = marksValue->getIf<Value::Array>()->at(i);
        auto        mark = decodePhaseMark(item, "actionBinding.phaseMarks[" + std::to_string(i) + "]");
        if (!mark) return Result<ActionGuideBinding>::failure(mark.status());
        binding.phaseMarks.push_back(std::move(mark).takeValue());
    }
    return Result<ActionGuideBinding>::success(std::move(binding));
}

Result<ActionGuideQc> decodeQc(const Value& value) {
    auto unknown = rejectUnknown(value, {"minSilhouetteIoU", "maxJointErrorPx", "maxRetries"}, "qc");
    if (!unknown) return Result<ActionGuideQc>::failure(unknown.status());
    auto iou     = number(value, "minSilhouetteIoU", "qc.minSilhouetteIoU");
    auto joint   = number(value, "maxJointErrorPx", "qc.maxJointErrorPx");
    auto retries = integer(value, "maxRetries", "qc.maxRetries");
    if (!iou) return Result<ActionGuideQc>::failure(iou.status());
    if (!joint) return Result<ActionGuideQc>::failure(joint.status());
    if (!retries) return Result<ActionGuideQc>::failure(retries.status());
    ActionGuideQc qc;
    qc.minSilhouetteIoU = iou.value();
    qc.maxJointErrorPx  = joint.value();
    qc.maxRetries       = retries.value();
    return Result<ActionGuideQc>::success(std::move(qc));
}

struct Vec3d {
    double x = 0, y = 0, z = 0;
};

Vec3d         sub(Vec3d a, Vec3d b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3d         cross(Vec3d a, Vec3d b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double        dot(Vec3d a, Vec3d b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double        length(Vec3d v) { return std::sqrt(dot(v, v)); }
Result<Vec3d> normalize(Vec3d v, std::string path) {
    const double len = length(v);
    if (!(len > 1e-12)) return invalid<Vec3d>("degenerate camera basis", std::move(path));
    return Result<Vec3d>::success(Vec3d{v.x / len, v.y / len, v.z / len});
}

struct Mat4d {
    double m[16]{};
};

Mat4d mul(const Mat4d& a, const Mat4d& b) {
    Mat4d out{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            out.m[column * 4 + row] = a.m[0 * 4 + row] * b.m[column * 4 + 0] + a.m[1 * 4 + row] * b.m[column * 4 + 1] +
                                      a.m[2 * 4 + row] * b.m[column * 4 + 2] + a.m[3 * 4 + row] * b.m[column * 4 + 3];
        }
    }
    return out;
}

std::array<double, 4> transform(const Mat4d& matrix, double x, double y, double z, double w) {
    return {matrix.m[0] * x + matrix.m[4] * y + matrix.m[8] * z + matrix.m[12] * w,
            matrix.m[1] * x + matrix.m[5] * y + matrix.m[9] * z + matrix.m[13] * w,
            matrix.m[2] * x + matrix.m[6] * y + matrix.m[10] * z + matrix.m[14] * w,
            matrix.m[3] * x + matrix.m[7] * y + matrix.m[11] * z + matrix.m[15] * w};
}

Result<Mat4d> lookAt(const ActionGuideCamera& camera) {
    Vec3d eye{camera.eye[0], camera.eye[1], camera.eye[2]};
    Vec3d target{camera.target[0], camera.target[1], camera.target[2]};
    Vec3d up{camera.up[0], camera.up[1], camera.up[2]};
    auto  forward = normalize(sub(target, eye), "camera");
    if (!forward) return Result<Mat4d>::failure(forward.status());
    auto right = normalize(cross(forward.value(), up), "camera.up");
    if (!right) return Result<Mat4d>::failure(right.status());
    auto  trueUp = cross(right.value(), forward.value());
    Mat4d view{};
    view.m[0]  = right.value().x;
    view.m[4]  = right.value().y;
    view.m[8]  = right.value().z;
    view.m[1]  = trueUp.x;
    view.m[5]  = trueUp.y;
    view.m[9]  = trueUp.z;
    view.m[2]  = -forward.value().x;
    view.m[6]  = -forward.value().y;
    view.m[10] = -forward.value().z;
    view.m[15] = 1.0;
    view.m[12] = -dot(right.value(), eye);
    view.m[13] = -dot(trueUp, eye);
    view.m[14] = dot(forward.value(), eye);
    return Result<Mat4d>::success(view);
}

Result<Mat4d> projectionMatrix(const ActionGuidePack& pack) {
    const double aspect = static_cast<double>(pack.canvas.width) / static_cast<double>(pack.canvas.height);
    Mat4d        proj{};
    if (pack.camera.projection == ActionGuideProjection::Orthographic) {
        const double halfH = pack.camera.orthoSize;
        const double halfW = halfH * aspect;
        const double nearP = pack.camera.nearPlane;
        const double farP  = pack.camera.farPlane;
        proj.m[0]          = 1.0 / halfW;
        proj.m[5]          = 1.0 / halfH;
        proj.m[10]         = -2.0 / (farP - nearP);
        proj.m[14]         = -(farP + nearP) / (farP - nearP);
        proj.m[15]         = 1.0;
        return Result<Mat4d>::success(proj);
    }
    const double f     = 1.0 / std::tan((pack.camera.fovYDegrees * 0.5) * 3.14159265358979323846 / 180.0);
    const double nearP = pack.camera.nearPlane;
    const double farP  = pack.camera.farPlane;
    proj.m[0]          = f / aspect;
    proj.m[5]          = f;
    proj.m[10]         = (farP + nearP) / (nearP - farP);
    proj.m[11]         = -1.0;
    proj.m[14]         = (2.0 * farP * nearP) / (nearP - farP);
    return Result<Mat4d>::success(proj);
}

}  // namespace

Result<Value> encodeActionGuidePack(const ActionGuidePack& pack) {
    auto valid = validatePack(pack);
    if (!valid) return Result<Value>::failure(valid.status());

    Value::Array shots;
    shots.reserve(pack.shots.size());
    for (const auto& shot : pack.shots) {
        Value::Object object{{"shotId", shot.shotId},
                             {"timeSeconds", shot.timeSeconds},
                             {"view", shot.view},
                             {"aovs", strings(shot.aovs)},
                             {"tags", strings(shot.tags)}};
        if (shot.sampleIndex) object["sampleIndex"] = *shot.sampleIndex;
        shots.emplace_back(std::move(object));
    }

    Value::Array phaseMarks;
    phaseMarks.reserve(pack.actionBinding.phaseMarks.size());
    for (const auto& mark : pack.actionBinding.phaseMarks) {
        phaseMarks.emplace_back(Value::Object{{"name", mark.name}, {"timeSeconds", mark.timeSeconds}});
    }

    Value::Object root{
        {"schema", std::string(kActionGuidePackSchemaId)},
        {"schemaVersion", static_cast<std::int64_t>(kActionGuidePackSchemaVersion)},
        {"revision", static_cast<std::int64_t>(pack.revision)},
        {"subject", Value::Object{{"domain", pack.subject.domain},
                                  {"id", static_cast<std::int64_t>(pack.subject.id)},
                                  {"generation", static_cast<std::int64_t>(pack.subject.generation)}}},
        {"skeletonRef", pack.skeletonRef},
        {"clipRef", pack.clipRef},
        {"rootMode", std::string(rootModeName(pack.rootMode))},
        {"canvas", Value::Object{{"width", pack.canvas.width},
                                 {"height", pack.canvas.height},
                                 {"pixelsPerUnit", pack.canvas.pixelsPerUnit},
                                 {"backgroundRgba", array4(pack.canvas.backgroundRgba)},
                                 {"transparent", pack.canvas.transparent}}},
        {"camera", Value::Object{{"projection", std::string(projectionName(pack.camera.projection))},
                                 {"fovYDegrees", pack.camera.fovYDegrees},
                                 {"orthoSize", pack.camera.orthoSize},
                                 {"nearPlane", pack.camera.nearPlane},
                                 {"farPlane", pack.camera.farPlane},
                                 {"eye", array3(pack.camera.eye)},
                                 {"target", array3(pack.camera.target)},
                                 {"up", array3(pack.camera.up)}}},
        {"shots", std::move(shots)},
        {"actionBinding", Value::Object{{"abilityId", pack.actionBinding.abilityId},
                                        {"clipName", pack.actionBinding.clipName},
                                        {"fps", pack.actionBinding.fps},
                                        {"loop", pack.actionBinding.loop},
                                        {"phaseMarks", std::move(phaseMarks)}}},
        {"qc", Value::Object{{"minSilhouetteIoU", pack.qc.minSilhouetteIoU},
                             {"maxJointErrorPx", pack.qc.maxJointErrorPx},
                             {"maxRetries", pack.qc.maxRetries}}},
    };
    return Result<Value>::success(Value(std::move(root)));
}

Result<ActionGuidePack> decodeActionGuidePack(const Value& value) {
    auto unknown = rejectUnknown(value,
                                 {"schema", "schemaVersion", "revision", "subject", "skeletonRef", "clipRef",
                                  "rootMode", "canvas", "camera", "shots", "actionBinding", "qc"},
                                 "$");
    if (!unknown) return Result<ActionGuidePack>::failure(unknown.status());
    auto schema       = text(value, "schema", "schema");
    auto version      = integer(value, "schemaVersion", "schemaVersion");
    auto revision     = integer(value, "revision", "revision");
    auto skeletonRef  = text(value, "skeletonRef", "skeletonRef");
    auto clipRef      = text(value, "clipRef", "clipRef");
    auto rootModeText = text(value, "rootMode", "rootMode");
    if (!schema) return Result<ActionGuidePack>::failure(schema.status());
    if (!version) return Result<ActionGuidePack>::failure(version.status());
    if (!revision) return Result<ActionGuidePack>::failure(revision.status());
    if (!skeletonRef) return Result<ActionGuidePack>::failure(skeletonRef.status());
    if (!clipRef) return Result<ActionGuidePack>::failure(clipRef.status());
    if (!rootModeText) return Result<ActionGuidePack>::failure(rootModeText.status());
    if (schema.value() != kActionGuidePackSchemaId)
        return invalid<ActionGuidePack>("unexpected guide-pack schema", "schema");
    if (version.value() != static_cast<std::int64_t>(kActionGuidePackSchemaVersion))
        return invalid<ActionGuidePack>("unsupported guide-pack schema version", "schemaVersion");
    if (revision.value() <= 0) return invalid<ActionGuidePack>("revision must be positive", "revision");

    const auto* subjectValue = field(value, "subject");
    const auto* canvasValue  = field(value, "canvas");
    const auto* cameraValue  = field(value, "camera");
    const auto* shotsValue   = field(value, "shots");
    const auto* bindingValue = field(value, "actionBinding");
    const auto* qcValue      = field(value, "qc");
    if (!subjectValue) return invalid<ActionGuidePack>("subject is required", "subject");
    if (!canvasValue) return invalid<ActionGuidePack>("canvas is required", "canvas");
    if (!cameraValue) return invalid<ActionGuidePack>("camera is required", "camera");
    if (!shotsValue || !shotsValue->isArray()) return invalid<ActionGuidePack>("shots must be an array", "shots");
    if (!bindingValue) return invalid<ActionGuidePack>("actionBinding is required", "actionBinding");
    if (!qcValue) return invalid<ActionGuidePack>("qc is required", "qc");

    auto subject = decodeSubject(*subjectValue);
    if (!subject) return Result<ActionGuidePack>::failure(subject.status());
    auto canvas = decodeCanvas(*canvasValue);
    if (!canvas) return Result<ActionGuidePack>::failure(canvas.status());
    auto camera = decodeCamera(*cameraValue);
    if (!camera) return Result<ActionGuidePack>::failure(camera.status());
    auto binding = decodeBinding(*bindingValue);
    if (!binding) return Result<ActionGuidePack>::failure(binding.status());
    auto qc = decodeQc(*qcValue);
    if (!qc) return Result<ActionGuidePack>::failure(qc.status());
    auto rootMode = parseRootMode(rootModeText.value());
    if (!rootMode) return Result<ActionGuidePack>::failure(rootMode.status());

    ActionGuidePack pack;
    pack.revision      = static_cast<std::uint64_t>(revision.value());
    pack.subject       = std::move(subject).takeValue();
    pack.skeletonRef   = std::move(skeletonRef).takeValue();
    pack.clipRef       = std::move(clipRef).takeValue();
    pack.rootMode      = rootMode.value();
    pack.canvas        = std::move(canvas).takeValue();
    pack.camera        = std::move(camera).takeValue();
    pack.actionBinding = std::move(binding).takeValue();
    pack.qc            = std::move(qc).takeValue();
    for (std::size_t i = 0; i < shotsValue->getIf<Value::Array>()->size(); ++i) {
        auto shot = decodeShot(shotsValue->getIf<Value::Array>()->at(i), "shots[" + std::to_string(i) + "]");
        if (!shot) return Result<ActionGuidePack>::failure(shot.status());
        pack.shots.push_back(std::move(shot).takeValue());
    }

    auto valid = validatePack(pack);
    if (!valid) return Result<ActionGuidePack>::failure(valid.status());
    return Result<ActionGuidePack>::success(std::move(pack));
}

Result<std::vector<ActionGuideLandmark>> projectActionGuideLandmarks(const ActionGuidePack&                pack,
                                                                     std::span<const ActionGuideBoneWorld> bones) {
    auto valid = validatePack(pack);
    if (!valid) return Result<std::vector<ActionGuideLandmark>>::failure(valid.status());
    auto view = lookAt(pack.camera);
    if (!view) return Result<std::vector<ActionGuideLandmark>>::failure(view.status());
    auto proj = projectionMatrix(pack);
    if (!proj) return Result<std::vector<ActionGuideLandmark>>::failure(proj.status());
    const Mat4d viewProj = mul(proj.value(), view.value());

    std::vector<ActionGuideLandmark> landmarks;
    landmarks.reserve(bones.size());
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const auto& bone = bones[i];
        if (bone.boneId.empty())
            return invalid<std::vector<ActionGuideLandmark>>("boneId is required",
                                                             "bones[" + std::to_string(i) + "].boneId");
        if (!std::isfinite(bone.position[0]) || !std::isfinite(bone.position[1]) || !std::isfinite(bone.position[2]))
            return invalid<std::vector<ActionGuideLandmark>>("bone position must be finite",
                                                             "bones[" + std::to_string(i) + "].position");
        const auto clip = transform(viewProj, bone.position[0], bone.position[1], bone.position[2], 1.0);
        if (!(std::abs(clip[3]) > 1e-12) || !std::isfinite(clip[3]))
            return invalid<std::vector<ActionGuideLandmark>>("projected bone has non-finite w",
                                                             "bones[" + std::to_string(i) + "]");
        const double invW = 1.0 / clip[3];
        const double ndcX = clip[0] * invW;
        const double ndcY = clip[1] * invW;
        if (!std::isfinite(ndcX) || !std::isfinite(ndcY))
            return invalid<std::vector<ActionGuideLandmark>>("projected bone has non-finite ndc",
                                                             "bones[" + std::to_string(i) + "]");
        ActionGuideLandmark landmark;
        landmark.boneId = bone.boneId;
        landmark.name   = bone.name.empty() ? bone.boneId : bone.name;
        landmark.x      = (ndcX * 0.5 + 0.5) * static_cast<double>(pack.canvas.width);
        // Top-left origin: NDC +Y maps to smaller pixel Y.
        landmark.y         = (1.0 - (ndcY * 0.5 + 0.5)) * static_cast<double>(pack.canvas.height);
        const auto viewPos = transform(view.value(), bone.position[0], bone.position[1], bone.position[2], 1.0);
        landmark.depth     = -viewPos[2];
        landmark.parentId  = bone.parentId;
        landmarks.push_back(std::move(landmark));
    }
    return Result<std::vector<ActionGuideLandmark>>::success(std::move(landmarks));
}

}  // namespace eve
