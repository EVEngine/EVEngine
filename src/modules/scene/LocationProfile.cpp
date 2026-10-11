#include "scene/LocationProfile.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace eve::scene {
namespace {
bool finite(const LocationPose& pose) {
    return std::isfinite(pose.x) && std::isfinite(pose.y) && std::isfinite(pose.z) &&
           std::isfinite(pose.qx) && std::isfinite(pose.qy) && std::isfinite(pose.qz) &&
           std::isfinite(pose.qw);
}
template <class T>
Result<T> invalid(std::string message, DiagnosticCode code = DiagnosticCode::InvalidArgument) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), "scene.locationProfile"));
}
bool validSnapshot(const LocationSnapshot& value) {
    return finite(value.camera) && (!value.player || finite(*value.player));
}
Value poseValue(const LocationPose& pose) {
    return Value::Object{{"x", pose.x}, {"y", pose.y}, {"z", pose.z}, {"qx", pose.qx},
                         {"qy", pose.qy}, {"qz", pose.qz}, {"qw", pose.qw}};
}
Value snapshotValue(const LocationSnapshot& snapshot) {
    return Value::Object{{"camera", poseValue(snapshot.camera)},
                         {"player", snapshot.player ? poseValue(*snapshot.player) : Value()}};
}
bool onlyFields(const Value::Object& object, std::initializer_list<std::string_view> fields) {
    for (const auto& [name, value] : object) {
        (void)value;
        if (std::find(fields.begin(), fields.end(), name) == fields.end()) return false;
    }
    return true;
}
bool number(const Value::Object& object, const char* name, float& output) {
    const auto found = object.find(name);
    if (found == object.end() || !found->second.isNumeric()) return false;
    output = static_cast<float>(found->second.isDouble() ? found->second.asDouble() : found->second.asInt());
    return std::isfinite(output);
}
bool parsePose(const Value& value, LocationPose& output) {
    const auto* object = value.getIf<Value::Object>();
    if (!object || !onlyFields(*object, {"x", "y", "z", "qx", "qy", "qz", "qw"})) return false;
    LocationPose candidate;
    if (!number(*object,"x",candidate.x) || !number(*object,"y",candidate.y) ||
        !number(*object,"z",candidate.z) || !number(*object,"qx",candidate.qx) ||
        !number(*object,"qy",candidate.qy) || !number(*object,"qz",candidate.qz) ||
        !number(*object,"qw",candidate.qw)) return false;
    output = candidate;
    return true;
}
bool parseSnapshot(const Value& value, LocationSnapshot& output) {
    const auto* object = value.getIf<Value::Object>();
    if (!object || !onlyFields(*object, {"camera", "player"})) return false;
    const auto camera = object->find("camera"), player = object->find("player");
    if (camera == object->end() || player == object->end()) return false;
    LocationSnapshot candidate;
    if (!parsePose(camera->second, candidate.camera)) return false;
    if (!player->second.isNull()) {
        LocationPose pose;
        if (!parsePose(player->second, pose)) return false;
        candidate.player = pose;
    }
    output = candidate;
    return true;
}
}  // namespace

Result<void> LocationProfile::saveLocation(const LocationPose& camera,
                                           const std::optional<LocationPose>& player) {
    LocationSnapshot candidate{camera, player};
    if (!validSnapshot(candidate)) return invalid<void>("location poses must be finite");
    saved_ = candidate;
    return Result<void>::success();
}

Result<LocationSnapshot> LocationProfile::loadLocation() {
    if (!saved_) return invalid<LocationSnapshot>("no saved startup location", DiagnosticCode::NotFound);
    auto result = *saved_;
    saved_.reset();
    return Result<LocationSnapshot>::success(std::move(result));
}

Result<int> LocationProfile::addBookmark(LocationBookmark bookmark) {
    if (bookmark.name.empty() || !finite(bookmark.camera) || (bookmark.player && !finite(*bookmark.player)))
        return invalid<int>("bookmark requires a name and finite poses");
    if (std::any_of(bookmarks_.begin(), bookmarks_.end(),
                    [&](const LocationBookmark& value) { return value.name == bookmark.name; }))
        return invalid<int>("bookmark name already exists", DiagnosticCode::Conflict);
    bookmarks_.push_back(std::move(bookmark));
    return Result<int>::success(static_cast<int>(bookmarks_.size() - 1));
}

Result<void> LocationProfile::overrideBookmark(int index, const LocationSnapshot& snapshot,
                                               std::string controller, std::string scene) {
    if (index < 0 || index >= getBookmarkCount())
        return invalid<void>("bookmark index is out of range");
    if (!validSnapshot(snapshot)) return invalid<void>("bookmark poses must be finite");
    auto candidate = bookmarks_[static_cast<std::size_t>(index)];
    candidate.camera = snapshot.camera;
    candidate.player = snapshot.player;
    candidate.controller = std::move(controller);
    candidate.scene = std::move(scene);
    bookmarks_[static_cast<std::size_t>(index)] = std::move(candidate);
    return Result<void>::success();
}

Result<int> LocationProfile::removeBookmark(int index, int selectedIndex) {
    if (index < 0 || index >= getBookmarkCount() || selectedIndex < 0 || selectedIndex >= getBookmarkCount())
        return invalid<int>("bookmark or selection index is out of range");
    const int oldLast = getBookmarkCount() - 1;
    bookmarks_.erase(bookmarks_.begin() + index);
    int adjusted = selectedIndex == oldLast ? selectedIndex - 1 : selectedIndex;
    if (!bookmarks_.empty()) adjusted = std::clamp(adjusted, 0, getBookmarkCount() - 1);
    return Result<int>::success(adjusted);
}

Result<LocationSnapshot> LocationProfile::loadBookmark(int index) const {
    const auto* bookmark = bookmarkAt(index);
    if (!bookmark) return invalid<LocationSnapshot>("bookmark index is out of range");
    return Result<LocationSnapshot>::success(LocationSnapshot{bookmark->camera, bookmark->player});
}

Result<int> LocationProfile::previousBookmark(int selectedIndex) const {
    if (bookmarks_.empty()) return invalid<int>("location profile has no bookmarks", DiagnosticCode::NotFound);
    if (selectedIndex < 0 || selectedIndex >= getBookmarkCount())
        return invalid<int>("selection index is out of range");
    return Result<int>::success(selectedIndex == 0 ? getBookmarkCount() - 1 : selectedIndex - 1);
}

Result<int> LocationProfile::nextBookmark(int selectedIndex) const {
    if (bookmarks_.empty()) return invalid<int>("location profile has no bookmarks", DiagnosticCode::NotFound);
    if (selectedIndex < 0 || selectedIndex >= getBookmarkCount())
        return invalid<int>("selection index is out of range");
    return Result<int>::success(selectedIndex == getBookmarkCount() - 1 ? 0 : selectedIndex + 1);
}

const LocationBookmark* LocationProfile::bookmarkAt(int index) const noexcept {
    return index >= 0 && index < getBookmarkCount() ? &bookmarks_[static_cast<std::size_t>(index)] : nullptr;
}

Result<std::string> LocationProfile::getBookmarkName(int index) const {
    const auto* value = bookmarkAt(index);
    return value ? Result<std::string>::success(value->name)
                 : invalid<std::string>("bookmark index is out of range");
}
Result<std::string> LocationProfile::getBookmarkController(int index) const {
    const auto* value = bookmarkAt(index);
    return value ? Result<std::string>::success(value->controller)
                 : invalid<std::string>("bookmark index is out of range");
}
Result<std::string> LocationProfile::getBookmarkScene(int index) const {
    const auto* value = bookmarkAt(index);
    return value ? Result<std::string>::success(value->scene)
                 : invalid<std::string>("bookmark index is out of range");
}

Result<std::string> LocationProfile::serializeJson() const {
    Value::Array bookmarks;
    bookmarks.reserve(bookmarks_.size());
    for (const auto& bookmark : bookmarks_) {
        bookmarks.emplace_back(Value::Object{{"name", bookmark.name}, {"camera", poseValue(bookmark.camera)},
            {"player", bookmark.player ? poseValue(*bookmark.player) : Value()},
            {"controller", bookmark.controller}, {"scene", bookmark.scene}});
    }
    Value root(Value::Object{{"schemaId", "eve.scene.location-profile"}, {"schemaVersion", 1},
        {"saved", saved_ ? snapshotValue(*saved_) : Value()}, {"bookmarks", std::move(bookmarks)}});
    return root.toJson();
}

Result<void> LocationProfile::restoreJson(std::string_view json) {
    auto decoded = Value::fromJson(json);
    if (!decoded) return Result<void>::failure(decoded.status());
    const auto* root = decoded.value().getIf<Value::Object>();
    const auto fail = [] { return invalid<void>("invalid location profile schema", DiagnosticCode::ParseError); };
    if (!root || !onlyFields(*root, {"schemaId", "schemaVersion", "saved", "bookmarks"})) return fail();
    const auto schema = root->find("schemaId"), version = root->find("schemaVersion");
    const auto saved = root->find("saved"), bookmarks = root->find("bookmarks");
    if (schema == root->end() || !schema->second.isString() ||
        schema->second.asString() != "eve.scene.location-profile" || version == root->end() ||
        !version->second.isInt64() || version->second.asInt() != 1 || saved == root->end() ||
        bookmarks == root->end() || !bookmarks->second.isArray()) return fail();
    LocationProfile candidate;
    if (!saved->second.isNull()) {
        LocationSnapshot snapshot;
        if (!parseSnapshot(saved->second, snapshot)) return fail();
        candidate.saved_ = snapshot;
    }
    for (const auto& value : *bookmarks->second.getIf<Value::Array>()) {
        const auto* object = value.getIf<Value::Object>();
        if (!object || !onlyFields(*object, {"name", "camera", "player", "controller", "scene"})) return fail();
        const auto name=object->find("name"), camera=object->find("camera"), player=object->find("player");
        const auto controller=object->find("controller"), scene=object->find("scene");
        if (name==object->end() || !name->second.isString() || camera==object->end() ||
            player==object->end() || controller==object->end() || !controller->second.isString() ||
            scene==object->end() || !scene->second.isString()) return fail();
        LocationBookmark bookmark;
        bookmark.name=name->second.asString(); bookmark.controller=controller->second.asString();
        bookmark.scene=scene->second.asString();
        if (!parsePose(camera->second,bookmark.camera)) return fail();
        if (!player->second.isNull()) { LocationPose pose; if (!parsePose(player->second,pose)) return fail(); bookmark.player=pose; }
        auto added=candidate.addBookmark(std::move(bookmark));
        if (!added) return Result<void>::failure(added.status());
    }
    *this = std::move(candidate);
    return Result<void>::success();
}
}  // namespace eve::scene
