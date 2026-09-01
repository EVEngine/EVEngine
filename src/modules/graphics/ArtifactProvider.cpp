#include "graphics/ArtifactProvider.h"

#include "common/Capability.h"
#include "graphics/Graphics.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace eve::graphics {
namespace {

const eve::Value* member(const eve::Value::Object& object, const char* name) noexcept {
    const auto found = object.find(name);
    return found == object.end() ? nullptr : &found->second;
}

bool readString(const eve::Value::Object& object, const char* name, std::string& output, bool allowEmpty = false) {
    const auto* value = member(object, name);
    const auto* text  = value ? value->getIf<std::string>() : nullptr;
    if (!text || (!allowEmpty && text->empty())) return false;
    output = *text;
    return true;
}

bool readU64(const eve::Value::Object& object, const char* name, std::uint64_t& output) {
    std::string text;
    if (!readString(object, name, text)) return false;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), output);
    return error == std::errc{} && end == text.data() + text.size();
}

bool readBool(const eve::Value::Object& object, const char* name, bool& output) {
    const auto* value   = member(object, name);
    const auto* boolean = value ? value->getIf<bool>() : nullptr;
    if (!boolean) return false;
    output = *boolean;
    return true;
}

bool readOptionalBool(const eve::Value::Object& object, const char* name, bool& output) {
    const auto* value = member(object, name);
    if (!value) return true;
    return readBool(object, name, output);
}

bool hasSupportedVersion(const eve::Value::Object& object) noexcept {
    const auto* encoded = member(object, "version");
    const auto* version = encoded ? encoded->getIf<std::int64_t>() : nullptr;
    return version && (*version == 1 || *version == 2);
}

eve::Value floatArray(const std::vector<float>& values) {
    eve::Value::Array encoded;
    encoded.reserve(values.size());
    for (const float value : values) encoded.emplace_back(value);
    return eve::Value(std::move(encoded));
}

eve::Value uintArray(const std::vector<std::uint32_t>& values) {
    eve::Value::Array encoded;
    encoded.reserve(values.size());
    for (const auto value : values) encoded.emplace_back(std::int64_t(value));
    return eve::Value(std::move(encoded));
}

template <class T>
bool readNumericArray(const eve::Value* encoded, std::vector<T>& output) {
    const auto* array = encoded ? encoded->getIf<eve::Value::Array>() : nullptr;
    if (!array) return false;
    output.clear();
    output.reserve(array->size());
    for (const eve::Value& value : *array) {
        if constexpr (std::is_same_v<T, float>) {
            if (const auto* number = value.getIf<double>()) {
                if (!std::isfinite(*number) || *number < -std::numeric_limits<float>::max() ||
                    *number > std::numeric_limits<float>::max())
                    return false;
                output.push_back(static_cast<float>(*number));
            } else if (const auto* integer = value.getIf<std::int64_t>()) {
                const float converted = static_cast<float>(*integer);
                if (!std::isfinite(converted)) return false;
                output.push_back(converted);
            } else {
                return false;
            }
        } else {
            const auto* integer = value.getIf<std::int64_t>();
            if (!integer || *integer < 0 || static_cast<std::uint64_t>(*integer) > std::numeric_limits<T>::max())
                return false;
            output.push_back(static_cast<T>(*integer));
        }
    }
    return true;
}

std::uint64_t hashStreams(std::span<const float> positions, std::span<const float> normals, std::span<const float> uvs,
                          std::span<const std::uint32_t> indices) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    const auto    mix  = [&hash](const auto* data, std::size_t count) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < count * sizeof(*data); ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    };
    mix(positions.data(), positions.size());
    mix(normals.data(), normals.size());
    mix(uvs.data(), uvs.size());
    mix(indices.data(), indices.size());
    return hash;
}

std::uint64_t hashDescriptor(const GraphicsArtifactResource& resource) noexcept {
    return hashStreams(resource.positions, resource.normals, resource.uvs, resource.indices);
}

const eve::artifact::PartView* selectedMesh(const eve::artifact::PublicationView& publication) noexcept {
    const eve::artifact::PartView* mesh = nullptr;
    for (const auto& part : publication.parts) {
        if (part.kind == eve::artifact::PartKind::MeshData && (part.role == "mesh" || mesh == nullptr)) mesh = &part;
    }
    return mesh;
}

bool validMeshView(const eve::artifact::PartView& mesh) noexcept {
    if (mesh.positions.empty() || mesh.positions.size() % 3u != 0u || mesh.indices.empty() ||
        mesh.indices.size() % 3u != 0u)
        return false;
    const std::size_t vertices = mesh.positions.size() / 3u;
    if ((!mesh.normals.empty() && mesh.normals.size() != mesh.positions.size()) ||
        (!mesh.uvs.empty() && mesh.uvs.size() != vertices * 2u))
        return false;
    for (const float value : mesh.positions)
        if (!std::isfinite(value)) return false;
    for (const float value : mesh.normals)
        if (!std::isfinite(value)) return false;
    for (const float value : mesh.uvs)
        if (!std::isfinite(value)) return false;
    for (const auto index : mesh.indices)
        if (index >= vertices) return false;
    return true;
}

}  // namespace

void GraphicsArtifactProvider::uploadIfAvailable(Graphics* graphics, RuntimeResource& runtime) noexcept {
    auto& descriptor     = runtime.descriptor;
    runtime.uploadedMesh = nullptr;
    if (!graphics) {
        descriptor.backendName = "cpu";
        descriptor.uploadState = "not-attempted";
        descriptor.gpuResident = false;
        return;
    }
    descriptor.gpuResident = false;
    descriptor.uploadState = "cpu-fallback";
    try {
        descriptor.backendName = graphics->getBackendName();
        runtime.uploadedMesh   = graphics->newMeshFromArrays(
            descriptor.positions.data(), descriptor.normals.empty() ? nullptr : descriptor.normals.data(),
            descriptor.uvs.empty() ? nullptr : descriptor.uvs.data(),
            static_cast<int>(descriptor.positions.size() / 3u), descriptor.indices.data(),
            static_cast<int>(descriptor.indices.size()));
        if (runtime.uploadedMesh) {
            const auto backend = graphics->describeMesh(runtime.uploadedMesh);
            if (!backend || backend->vertexCount != descriptor.positions.size() / 3u ||
                backend->indexCount != descriptor.indices.size() || backend->vertexStride == 0u ||
                (backend->indexElementSize != 2u && backend->indexElementSize != 4u)) {
                (void)graphics->releaseMesh(runtime.uploadedMesh);
                runtime.uploadedMesh   = nullptr;
                descriptor.uploadState = "cpu-fallback";
                descriptor.gpuResident = false;
                return;
            }
            descriptor.backendVertexStride     = backend->vertexStride;
            descriptor.backendIndexElementSize = backend->indexElementSize;
            descriptor.uploadState             = "uploaded";
            descriptor.gpuResident             = true;
        }
    } catch (...) {
        if (runtime.uploadedMesh) {
            try {
                (void)graphics->releaseMesh(runtime.uploadedMesh);
            } catch (...) {
            }
        }
        runtime.uploadedMesh   = nullptr;
        descriptor.uploadState = "cpu-fallback";
    }
}

class GraphicsArtifactStage final : public eve::artifact::PreparedPublication {
public:
    GraphicsArtifactStage(GraphicsArtifactProvider& owner, GraphicsArtifactResource resource, Mesh* uploadedMesh)
        : owner_(&owner), resource_{std::move(resource), uploadedMesh} {}
    ~GraphicsArtifactStage() override { rollback(); }
    void commit() noexcept override {
        if (!owner_) return;
        owner_->commit(std::move(resource_));
        owner_ = nullptr;
    }
    void rollback() noexcept override {
        if (owner_) owner_->release(resource_);
        owner_    = nullptr;
        resource_ = {};
    }

private:
    GraphicsArtifactProvider*                 owner_ = nullptr;
    GraphicsArtifactProvider::RuntimeResource resource_;
};

std::optional<GraphicsArtifactDescriptor> WebGpuArtifactDescriptorAdapter::describe(
    const eve::artifact::PublicationView& publication) {
    const auto* mesh = selectedMesh(publication);
    if (!mesh || !validMeshView(*mesh)) return std::nullopt;
    GraphicsArtifactDescriptor result;
    result.id          = mesh->id;
    result.buildKey    = std::string(mesh->buildKey);
    result.role        = std::string(mesh->role);
    result.vertexCount = mesh->positions.size() / 3u;
    result.indexCount  = mesh->indices.size();
    result.checksum    = hashStreams(mesh->positions, mesh->normals, mesh->uvs, mesh->indices);
    return result;
}

eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>> GraphicsArtifactProvider::prepare(
    const eve::artifact::PublicationView& publication) {
    if (failPrepare_)
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "graphics artifact prepare failure was injected"));
    if (publication.id.isNil() || publication.buildKey.empty())
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "graphics publication requires identity and build key"));
    if (find(publication.id) != nullptr)
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "graphics artifact identity is already published"));
    const auto* mesh = selectedMesh(publication);
    if (!mesh || !validMeshView(*mesh))
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "graphics publication requires a complete mesh leaf"));
    if (mesh->positions.size() / 3u > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        mesh->indices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "graphics mesh exceeds backend integer limits"));
    if (nextIndex_ == GraphicsArtifactHandle::invalidIndex)
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "graphics artifact handle index exhausted"));

    GraphicsArtifactResource resource;
    resource.id       = publication.id;
    resource.buildKey = std::string(publication.buildKey);
    resource.handle   = GraphicsArtifactHandle(nextIndex_, 1);
    resource.role     = std::string(mesh->role);
    RuntimeResource runtime;
    runtime.descriptor = std::move(resource);
    try {
        runtime.descriptor.positions.assign(mesh->positions.begin(), mesh->positions.end());
        runtime.descriptor.normals.assign(mesh->normals.begin(), mesh->normals.end());
        runtime.descriptor.uvs.assign(mesh->uvs.begin(), mesh->uvs.end());
        runtime.descriptor.indices.assign(mesh->indices.begin(), mesh->indices.end());
        uploadIfAvailable(graphics_, runtime);
        resources_.reserve(resources_.size() + 1);
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::success(
            std::make_unique<GraphicsArtifactStage>(*this, std::move(runtime.descriptor), runtime.uploadedMesh));
    } catch (...) {
        if (runtime.uploadedMesh && graphics_) {
            try {
                (void)graphics_->releaseMesh(runtime.uploadedMesh);
            } catch (...) {
            }
        }
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "graphics artifact prepare allocation failed"));
    }
}

void GraphicsArtifactProvider::commit(RuntimeResource resource) noexcept {
    resources_.push_back(std::move(resource));
    ++nextIndex_;
}

void GraphicsArtifactProvider::release(RuntimeResource& resource) noexcept {
    if (resource.uploadedMesh && graphics_) {
        try {
            (void)graphics_->releaseMesh(resource.uploadedMesh);
        } catch (...) {
        }
    }
    if (resource.uploadedMesh) {
        resource.descriptor.gpuResident             = false;
        resource.descriptor.uploadState             = "detached";
        resource.descriptor.backendVertexStride     = 0;
        resource.descriptor.backendIndexElementSize = 0;
        resource.uploadedMesh                       = nullptr;
    }
}

const GraphicsArtifactResource* GraphicsArtifactProvider::find(eve::PersistentId id) const noexcept {
    const auto found = std::find_if(resources_.begin(), resources_.end(),
                                    [id](const auto& resource) { return resource.descriptor.id == id; });
    return found == resources_.end() ? nullptr : &found->descriptor;
}

std::uint64_t GraphicsArtifactProvider::checksum(eve::PersistentId id) const noexcept {
    const auto* resource = find(id);
    return resource ? hashDescriptor(*resource) : 0;
}

std::optional<GraphicsArtifactDescriptor> GraphicsArtifactProvider::descriptor(eve::PersistentId id) const {
    const auto* resource = find(id);
    if (!resource) return std::nullopt;
    return GraphicsArtifactDescriptor{id,
                                      resource->buildKey,
                                      resource->role,
                                      resource->positions.size() / 3u,
                                      resource->indices.size(),
                                      hashDescriptor(*resource),
                                      resource->backendVertexStride,
                                      resource->backendIndexElementSize};
}

bool GraphicsArtifactProvider::isGpuResident(eve::PersistentId id) const noexcept {
    const auto* resource = find(id);
    return resource && resource->gpuResident;
}

void GraphicsArtifactProvider::clear() noexcept {
    for (auto& resource : resources_) release(resource);
    resources_.clear();
    nextIndex_   = 0;
    failPrepare_ = false;
}

void GraphicsArtifactProvider::bindGraphics(Graphics* graphics) noexcept {
    if (graphics_ == graphics) return;
    if (graphics_) {
        for (auto& resource : resources_) release(resource);
    }
    graphics_ = graphics;
}

void GraphicsArtifactProvider::detachGraphics(Graphics* graphics) noexcept {
    if (graphics_ != graphics) return;
    for (auto& resource : resources_) release(resource);
    graphics_ = nullptr;
}

eve::Result<eve::Value> GraphicsArtifactProvider::snapshotState() const {
    try {
        eve::Value::Array resources;
        resources.reserve(resources_.size());
        for (const auto& runtime : resources_) {
            const auto&        resource = runtime.descriptor;
            eve::Value::Object item;
            item.emplace("artifactId", eve::Value(resource.id.format()));
            item.emplace("buildKey", eve::Value(resource.buildKey));
            item.emplace("handle", eve::Value(std::to_string(resource.handle.packed())));
            item.emplace("role", eve::Value(resource.role));
            item.emplace("positions", floatArray(resource.positions));
            item.emplace("normals", floatArray(resource.normals));
            item.emplace("uvs", floatArray(resource.uvs));
            item.emplace("indices", uintArray(resource.indices));
            item.emplace("backend", eve::Value(resource.backendName));
            item.emplace("uploadState", eve::Value(resource.uploadState));
            item.emplace("gpuResident", eve::Value(resource.gpuResident));
            resources.emplace_back(std::move(item));
        }
        eve::Value::Object state;
        state.emplace("provider", eve::Value("graphics.resource-provider"));
        state.emplace("version", eve::Value(std::int64_t(2)));
        state.emplace("unknownFields", eve::Value("ignore"));
        state.emplace("resources", eve::Value(std::move(resources)));
        return eve::Result<eve::Value>::success(eve::Value(std::move(state)));
    } catch (...) {
        return eve::Result<eve::Value>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "graphics artifact snapshot allocation failed"));
    }
}

eve::Result<void> GraphicsArtifactProvider::restoreState(const eve::Value& state) {
    if (!resources_.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "graphics artifact restore requires an empty registry"));
    const auto* object           = state.getIf<eve::Value::Object>();
    const auto* provider         = object ? member(*object, "provider") : nullptr;
    const auto* providerName     = provider ? provider->getIf<std::string>() : nullptr;
    const auto* versionValue     = object ? member(*object, "version") : nullptr;
    const auto* version          = versionValue ? versionValue->getIf<std::int64_t>() : nullptr;
    const auto* encodedResources = object ? member(*object, "resources") : nullptr;
    const auto* resources        = encodedResources ? encodedResources->getIf<eve::Value::Array>() : nullptr;
    if (!object || !providerName || *providerName != "graphics.resource-provider" || !hasSupportedVersion(*object) ||
        !version || !resources)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::UnknownVersion, "invalid graphics artifact provider state"));

    std::vector<RuntimeResource>          candidate;
    std::unordered_set<eve::PersistentId> identities;
    std::uint32_t                         next    = 0;
    const auto                            cleanup = [&]() noexcept {
        for (auto& resource : candidate) release(resource);
        candidate.clear();
    };
    const auto fail = [&](eve::Diagnostic diagnostic) -> eve::Result<void> {
        cleanup();
        return eve::Result<void>::failure(std::move(diagnostic));
    };
    try {
        candidate.reserve(resources->size());
        for (const eve::Value& encoded : *resources) {
            const auto*     item = encoded.getIf<eve::Value::Object>();
            std::string     idText;
            std::string     buildKey;
            std::string     role;
            std::string     backend;
            std::string     uploadState;
            std::uint64_t   packed      = 0;
            bool            gpuResident = false;
            RuntimeResource runtime;
            auto&           resource = runtime.descriptor;
            if (!item || !readString(*item, "artifactId", idText) || !readString(*item, "buildKey", buildKey) ||
                !readString(*item, "role", role) || !readU64(*item, "handle", packed) ||
                !readNumericArray(member(*item, "positions"), resource.positions) ||
                !readNumericArray(member(*item, "normals"), resource.normals) ||
                !readNumericArray(member(*item, "uvs"), resource.uvs) ||
                !readNumericArray(member(*item, "indices"), resource.indices))
                return fail(
                    eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "invalid graphics artifact resource"));
            const auto parsedId = eve::PersistentId::parse(idText);
            resource.handle     = GraphicsArtifactHandle::fromPacked(packed);
            if (!parsedId || parsedId->isNil() || resource.handle.isInvalid() || role.empty() ||
                !identities.emplace(*parsedId).second || resource.positions.empty() ||
                resource.positions.size() % 3u != 0u || resource.indices.empty() ||
                resource.indices.size() % 3u != 0u ||
                (!resource.normals.empty() && resource.normals.size() != resource.positions.size()) ||
                (!resource.uvs.empty() && resource.uvs.size() != resource.positions.size() / 3u * 2u))
                return fail(eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                                   "invalid graphics artifact streams or identity"));
            for (const auto index : resource.indices)
                if (index >= resource.positions.size() / 3u)
                    return fail(
                        eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "invalid graphics artifact index"));
            if (*version >= 2) {
                if (!readString(*item, "backend", backend) || !readString(*item, "uploadState", uploadState) ||
                    !readBool(*item, "gpuResident", gpuResident))
                    return fail(
                        eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "invalid graphics upload state"));
            }
            resource.id       = *parsedId;
            resource.buildKey = std::move(buildKey);
            resource.role     = std::move(role);
            if (*version >= 2) {
                resource.backendName = std::move(backend);
                resource.uploadState = std::move(uploadState);
                resource.gpuResident = gpuResident;
            }
            uploadIfAvailable(graphics_, runtime);
            next = std::max(next, resource.handle.index() + 1u);
            candidate.push_back(std::move(runtime));
        }
    } catch (...) {
        cleanup();
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "graphics artifact restore allocation failed"));
    }
    resources_.swap(candidate);
    nextIndex_ = next;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

GraphicsArtifactProvider& graphicsArtifactProvider() noexcept {
    static GraphicsArtifactProvider provider;
    return provider;
}

void registerGraphicsArtifactProvider(Graphics* graphics) {
    graphicsArtifactProvider().bindGraphics(graphics);
    eve::cap::provide<eve::artifact::IGraphicsArtifactAdapter>(&graphicsArtifactProvider());
}

void detachGraphicsArtifactProvider(Graphics* graphics) noexcept {
    graphicsArtifactProvider().detachGraphics(graphics);
}

}  // namespace eve::graphics
