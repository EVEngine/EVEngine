#include "scene/ArtifactProvider.h"

#include "common/Capability.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace eve::scene {

eve::Result<SceneArtifactRecord> failureRecord(eve::DiagnosticCode code, std::string message) {
    return eve::Result<SceneArtifactRecord>::failure(
        eve::Diagnostic::error(code, std::move(message), "scene.artifact"));
}

eve::Result<eve::Value> failureValue(eve::DiagnosticCode code, std::string message) {
    return eve::Result<eve::Value>::failure(
        eve::Diagnostic::error(code, std::move(message), "scene.artifact.snapshot"));
}

bool validBounds(const eve::artifact::Bounds& bounds) noexcept {
    return bounds.valid && std::isfinite(bounds.minX) && std::isfinite(bounds.minY) && std::isfinite(bounds.minZ) &&
           std::isfinite(bounds.maxX) && std::isfinite(bounds.maxY) && std::isfinite(bounds.maxZ) &&
           bounds.minX <= bounds.maxX && bounds.minY <= bounds.maxY && bounds.minZ <= bounds.maxZ;
}

const eve::Value* member(const eve::Value::Object& object, const char* name) noexcept {
    const auto found = object.find(name);
    return found == object.end() ? nullptr : &found->second;
}

bool readString(const eve::Value::Object& object, const char* name, std::string& output) {
    const auto* value = member(object, name);
    const auto* text  = value ? value->getIf<std::string>() : nullptr;
    if (!text || text->empty()) return false;
    output = *text;
    return true;
}

bool readUint64String(const eve::Value::Object& object, const char* name, std::uint64_t& output) {
    std::string text;
    if (!readString(object, name, text)) return false;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), output);
    return error == std::errc{} && end == text.data() + text.size();
}

bool hasSupportedVersion(const eve::Value::Object& object) noexcept {
    const auto* encoded = member(object, "version");
    const auto* version = encoded ? encoded->getIf<std::int64_t>() : nullptr;
    return version && *version == 1;
}

bool readFloat(const eve::Value::Object& object, const char* name, float& output) {
    const auto* value = member(object, name);
    if (!value) return false;
    if (const auto* number = value->getIf<double>()) {
        if (!std::isfinite(*number)) return false;
        output = static_cast<float>(*number);
        return std::isfinite(output);
    }
    if (const auto* integer = value->getIf<std::int64_t>()) {
        output = static_cast<float>(*integer);
        return std::isfinite(output);
    }
    return false;
}

bool readBounds(const eve::Value::Object& object, eve::artifact::Bounds& bounds) {
    const auto* encoded = member(object, "bounds");
    const auto* value   = encoded ? encoded->getIf<eve::Value::Object>() : nullptr;
    return value && readFloat(*value, "minX", bounds.minX) && readFloat(*value, "minY", bounds.minY) &&
           readFloat(*value, "minZ", bounds.minZ) && readFloat(*value, "maxX", bounds.maxX) &&
           readFloat(*value, "maxY", bounds.maxY) && readFloat(*value, "maxZ", bounds.maxZ) &&
           (bounds.valid = true, validBounds(bounds));
}

eve::Value boundsValue(const eve::artifact::Bounds& bounds) {
    eve::Value::Object value;
    value.emplace("minX", eve::Value(bounds.minX));
    value.emplace("minY", eve::Value(bounds.minY));
    value.emplace("minZ", eve::Value(bounds.minZ));
    value.emplace("maxX", eve::Value(bounds.maxX));
    value.emplace("maxY", eve::Value(bounds.maxY));
    value.emplace("maxZ", eve::Value(bounds.maxZ));
    return eve::Value(std::move(value));
}

class SceneArtifactStage final : public eve::artifact::PreparedPublication {
public:
    SceneArtifactStage(SceneArtifactProvider& owner, SceneArtifactRecord record)
        : owner_(&owner), record_(std::move(record)) {}

    void commit() noexcept override {
        if (!owner_) return;
        owner_->commit(std::move(record_));
        owner_ = nullptr;
    }

    void rollback() noexcept override {
        owner_  = nullptr;
        record_ = {};
    }

private:
    SceneArtifactProvider* owner_ = nullptr;
    SceneArtifactRecord    record_;
};

eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>> SceneArtifactProvider::prepare(
    const eve::artifact::PublicationView& publication) {
    if (failPrepare_)
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "scene artifact prepare failure was injected"));
    if (publication.id.isNil() || publication.buildKey.empty() || publication.parts.empty())
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "scene publication requires identity, build key and parts"));
    if (find(publication.id) != nullptr)
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "scene artifact identity is already published"));
    if (nextIndex_ == SceneArtifactHandle::invalidIndex)
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "scene artifact handle index exhausted"));

    SceneArtifactRecord record;
    record.id       = publication.id;
    record.buildKey = std::string(publication.buildKey);
    record.handle   = SceneArtifactHandle(nextIndex_, 1);
    record.nodes.reserve(publication.parts.size());
    for (const auto& part : publication.parts) {
        if (part.id.isNil() || part.role.empty() || !validBounds(part.bounds))
            return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "scene artifact part has invalid identity, role or bounds"));
        record.nodes.push_back({std::string(part.role), part.bounds});
    }
    try {
        records_.reserve(records_.size() + 1);
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::success(
            std::make_unique<SceneArtifactStage>(*this, std::move(record)));
    } catch (...) {
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "scene artifact prepare allocation failed"));
    }
}

void SceneArtifactProvider::commit(SceneArtifactRecord record) noexcept {
    records_.push_back(std::move(record));
    ++nextIndex_;
}

const SceneArtifactRecord* SceneArtifactProvider::find(eve::PersistentId id) const noexcept {
    const auto found =
        std::find_if(records_.begin(), records_.end(), [id](const auto& record) { return record.id == id; });
    return found == records_.end() ? nullptr : &*found;
}

void SceneArtifactProvider::clear() noexcept {
    records_.clear();
    nextIndex_   = 0;
    failPrepare_ = false;
}

eve::Result<eve::Value> SceneArtifactProvider::snapshotState() const {
    try {
        eve::Value::Array records;
        records.reserve(records_.size());
        for (const auto& record : records_) {
            eve::Value::Array nodes;
            nodes.reserve(record.nodes.size());
            for (const auto& node : record.nodes) {
                eve::Value::Object item;
                item.emplace("role", eve::Value(node.role));
                item.emplace("bounds", boundsValue(node.bounds));
                nodes.emplace_back(std::move(item));
            }
            eve::Value::Object item;
            item.emplace("artifactId", eve::Value(record.id.format()));
            item.emplace("buildKey", eve::Value(record.buildKey));
            item.emplace("handle", eve::Value(std::to_string(record.handle.packed())));
            item.emplace("nodes", eve::Value(std::move(nodes)));
            records.emplace_back(std::move(item));
        }
        eve::Value::Object state;
        state.emplace("provider", eve::Value("scene.cpu-registry"));
        state.emplace("version", eve::Value(std::int64_t(1)));
        state.emplace("records", eve::Value(std::move(records)));
        return eve::Result<eve::Value>::success(eve::Value(std::move(state)));
    } catch (...) {
        return failureValue(eve::DiagnosticCode::Failed, "scene artifact snapshot allocation failed");
    }
}

eve::Result<void> SceneArtifactProvider::restoreState(const eve::Value& state) {
    if (!records_.empty())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "scene artifact restore requires an empty registry"));
    const auto* object       = state.getIf<eve::Value::Object>();
    const auto* provider     = object ? member(*object, "provider") : nullptr;
    const auto* providerName = provider ? provider->getIf<std::string>() : nullptr;
    const auto* records      = object ? member(*object, "records") : nullptr;
    const auto* array        = records ? records->getIf<eve::Value::Array>() : nullptr;
    if (!object || !providerName || *providerName != "scene.cpu-registry" || !hasSupportedVersion(*object) || !array)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "invalid scene artifact provider state"));

    std::vector<SceneArtifactRecord>      candidate;
    std::unordered_set<eve::PersistentId> identities;
    std::uint32_t                         next = 0;
    try {
        candidate.reserve(array->size());
        for (const eve::Value& encoded : *array) {
            const auto*   item = encoded.getIf<eve::Value::Object>();
            std::string   idText;
            std::string   buildKey;
            std::uint64_t packed = 0;
            if (!item || !readString(*item, "artifactId", idText) || !readString(*item, "buildKey", buildKey) ||
                !readUint64String(*item, "handle", packed))
                return eve::Result<void>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "invalid scene artifact record"));
            const auto  parsedId     = eve::PersistentId::parse(idText);
            const auto  handle       = SceneArtifactHandle::fromPacked(packed);
            const auto* encodedNodes = member(*item, "nodes");
            const auto* nodes        = encodedNodes ? encodedNodes->getIf<eve::Value::Array>() : nullptr;
            if (!parsedId || parsedId->isNil() || handle.isInvalid() || !nodes ||
                !identities.emplace(*parsedId).second || handle.index() == SceneArtifactHandle::invalidIndex)
                return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                                                         "invalid scene artifact identity or handle"));
            SceneArtifactRecord record{*parsedId, std::move(buildKey), handle, {}};
            record.nodes.reserve(nodes->size());
            for (const eve::Value& encodedNode : *nodes) {
                const auto* node = encodedNode.getIf<eve::Value::Object>();
                std::string role;
                const auto* boundsEncoded = node ? member(*node, "bounds") : nullptr;
                const auto* boundsObject  = boundsEncoded ? boundsEncoded->getIf<eve::Value::Object>() : nullptr;
                if (!node || !readString(*node, "role", role) || !boundsObject)
                    return eve::Result<void>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "invalid scene artifact node"));
                eve::artifact::Bounds bounds;
                if (!readBounds(*node, bounds))
                    return eve::Result<void>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "invalid scene artifact node bounds"));
                record.nodes.push_back({std::move(role), bounds});
            }
            next = std::max(next, handle.index() == std::numeric_limits<std::uint32_t>::max() ? handle.index()
                                                                                              : handle.index() + 1u);
            candidate.push_back(std::move(record));
        }
    } catch (...) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "scene artifact restore allocation failed"));
    }
    records_.swap(candidate);
    nextIndex_ = next;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

SceneArtifactProvider& sceneArtifactProvider() noexcept {
    static SceneArtifactProvider provider;
    return provider;
}

void registerSceneArtifactProvider() {
    eve::cap::provide<eve::artifact::ISceneArtifactAdapter>(&sceneArtifactProvider());
}

}  // namespace eve::scene
