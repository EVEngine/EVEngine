#include "map/ArtifactProvider.h"

#include "common/Capability.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <memory>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace eve::map {

const eve::Value* member(const eve::Value::Object& object, const char* name) noexcept {
    const auto found = object.find(name);
    return found == object.end() ? nullptr : &found->second;
}

bool readString(const eve::Value::Object& object, const char* name, std::string& output) {
    const auto* value = member(object, name);
    const auto* text = value ? value->getIf<std::string>() : nullptr;
    if (!text || text->empty()) return false;
    output = *text;
    return true;
}

bool readU64(const eve::Value::Object& object, const char* name, std::uint64_t& output) {
    std::string text;
    if (!readString(object, name, text)) return false;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), output);
    return error == std::errc{} && end == text.data() + text.size();
}

bool readI64(const eve::Value::Object& object, const char* name, std::int64_t& output) {
    const auto* value = member(object, name);
    if (!value) return false;
    if (const auto* integer = value->getIf<std::int64_t>()) {
        output = *integer;
        return true;
    }
    return false;
}

bool hasSupportedVersion(const eve::Value::Object& object) noexcept {
    const auto* encoded = member(object, "version");
    const auto* version = encoded ? encoded->getIf<std::int64_t>() : nullptr;
    return version && *version == 1;
}

class MapArtifactStage final : public eve::artifact::PreparedPublication {
public:
    MapArtifactStage(MapArtifactProvider& owner, MapArtifactRecord record)
        : owner_(&owner), record_(std::move(record)) {}
    void commit() noexcept override {
        if (!owner_) return;
        owner_->commit(std::move(record_));
        owner_ = nullptr;
    }
    void rollback() noexcept override {
        owner_ = nullptr;
        record_ = {};
    }

private:
    MapArtifactProvider* owner_ = nullptr;
    MapArtifactRecord record_;
};
eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>> MapArtifactProvider::prepare(
    const eve::artifact::PublicationView& publication) {
    if (failPrepare_)
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                   "map artifact prepare failure was injected"));
    if (publication.id.isNil() || publication.buildKey.empty())
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "map publication requires identity and build key"));
    if (find(publication.id) != nullptr)
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                   "map artifact identity is already published"));

    const eve::artifact::PartView* topology = nullptr;
    for (const auto& part : publication.parts) {
        if (part.kind == eve::artifact::PartKind::Grid &&
            (part.role == "topology" || topology == nullptr))
            topology = &part;
    }
    if (!topology || topology->width <= 0 || topology->height <= 0 ||
        topology->cells.size() != static_cast<std::size_t>(topology->width) *
                                       static_cast<std::size_t>(topology->height))
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                   "map publication requires a complete grid topology"));
    if (nextIndex_ == MapArtifactHandle::invalidIndex)
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                   "map artifact handle index exhausted"));

    MapArtifactRecord record;
    record.id = publication.id;
    record.buildKey = std::string(publication.buildKey);
    record.handle = MapArtifactHandle(nextIndex_, 1);
    record.width = topology->width;
    record.height = topology->height;
    try {
        record.cells.assign(topology->cells.begin(), topology->cells.end());
        records_.reserve(records_.size() + 1);
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::success(
            std::make_unique<MapArtifactStage>(*this, std::move(record)));
    } catch (...) {
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                   "map artifact prepare allocation failed"));
    }
}

void MapArtifactProvider::commit(MapArtifactRecord record) noexcept {
    records_.push_back(std::move(record));
    ++nextIndex_;
}

const MapArtifactRecord* MapArtifactProvider::find(eve::PersistentId id) const noexcept {
    const auto found = std::find_if(records_.begin(), records_.end(),
                                   [id](const auto& record) { return record.id == id; });
    return found == records_.end() ? nullptr : &*found;
}

std::optional<std::uint32_t> MapArtifactProvider::cell(eve::PersistentId id, std::int32_t x,
                                                        std::int32_t y) const noexcept {
    const auto* record = find(id);
    if (!record || x < 0 || y < 0 || x >= record->width || y >= record->height) return std::nullopt;
    return record->cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(record->width) +
                         static_cast<std::size_t>(x)];
}

void MapArtifactProvider::clear() noexcept {
    records_.clear();
    nextIndex_ = 0;
    failPrepare_ = false;
}

eve::Result<eve::Value> MapArtifactProvider::snapshotState() const {
    try {
        eve::Value::Array records;
        records.reserve(records_.size());
        for (const auto& record : records_) {
            eve::Value::Array cells;
            cells.reserve(record.cells.size());
            for (const auto value : record.cells) cells.emplace_back(std::int64_t(value));
            eve::Value::Object item;
            item.emplace("artifactId", eve::Value(record.id.format()));
            item.emplace("buildKey", eve::Value(record.buildKey));
            item.emplace("handle", eve::Value(std::to_string(record.handle.packed())));
            item.emplace("width", eve::Value(std::int64_t(record.width)));
            item.emplace("height", eve::Value(std::int64_t(record.height)));
            item.emplace("cells", eve::Value(std::move(cells)));
            records.emplace_back(std::move(item));
        }
        eve::Value::Object state;
        state.emplace("provider", eve::Value("map.cpu-registry"));
        state.emplace("version", eve::Value(std::int64_t(1)));
        state.emplace("records", eve::Value(std::move(records)));
        return eve::Result<eve::Value>::success(eve::Value(std::move(state)));
    } catch (...) {
        return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "map artifact snapshot allocation failed"));
    }
}

eve::Result<void> MapArtifactProvider::restoreState(const eve::Value& state) {
    if (!records_.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "map artifact restore requires an empty registry"));
    const auto* object = state.getIf<eve::Value::Object>();
    const auto* provider = object ? member(*object, "provider") : nullptr;
    const auto* providerName = provider ? provider->getIf<std::string>() : nullptr;
    const auto* encodedRecords = object ? member(*object, "records") : nullptr;
    const auto* records = encodedRecords ? encodedRecords->getIf<eve::Value::Array>() : nullptr;
    if (!object || !providerName || *providerName != "map.cpu-registry" ||
        !hasSupportedVersion(*object) || !records)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "invalid map artifact provider state"));

    std::vector<MapArtifactRecord> candidate;
    std::unordered_set<eve::PersistentId> identities;
    std::uint32_t next = 0;
    try {
        candidate.reserve(records->size());
        for (const eve::Value& encoded : *records) {
            const auto* item = encoded.getIf<eve::Value::Object>();
            std::string idText;
            std::string buildKey;
            std::uint64_t packed = 0;
            std::int64_t width = 0;
            std::int64_t height = 0;
            const auto* encodedCells = item ? member(*item, "cells") : nullptr;
            const auto* cells = encodedCells ? encodedCells->getIf<eve::Value::Array>() : nullptr;
            if (!item || !readString(*item, "artifactId", idText) ||
                !readString(*item, "buildKey", buildKey) || !readU64(*item, "handle", packed) ||
                !readI64(*item, "width", width) || !readI64(*item, "height", height) ||
                width <= 0 || height <= 0 || !cells)
                return eve::Result<void>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::ParseError, "invalid map artifact record"));
            const auto parsedId = eve::PersistentId::parse(idText);
            const auto handle = MapArtifactHandle::fromPacked(packed);
            if (!parsedId || parsedId->isNil() || handle.isInvalid() ||
                width > std::numeric_limits<std::int32_t>::max() ||
                height > std::numeric_limits<std::int32_t>::max() ||
                !identities.emplace(*parsedId).second ||
                static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) != cells->size())
                return eve::Result<void>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::ParseError, "invalid map artifact dimensions or identity"));
            MapArtifactRecord record;
            record.id = *parsedId;
            record.buildKey = std::move(buildKey);
            record.handle = handle;
            record.width = static_cast<std::int32_t>(width);
            record.height = static_cast<std::int32_t>(height);
            record.cells.reserve(cells->size());
            for (const eve::Value& value : *cells) {
                const auto* cell = value.getIf<std::int64_t>();
                if (!cell || *cell < 0 || *cell > std::numeric_limits<std::uint32_t>::max())
                    return eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::ParseError, "invalid map topology cell"));
                record.cells.push_back(static_cast<std::uint32_t>(*cell));
            }
            next = std::max(next, handle.index() + 1u);
            candidate.push_back(std::move(record));
        }
    } catch (...) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "map artifact restore allocation failed"));
    }
    records_.swap(candidate);
    nextIndex_ = next;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

MapArtifactProvider& mapArtifactProvider() noexcept {
    static MapArtifactProvider provider;
    return provider;
}

void registerMapArtifactProvider() {
    eve::cap::provide<eve::artifact::IMapArtifactAdapter>(&mapArtifactProvider());
}

}  // namespace eve::map
