#pragma once

/**
 * @file ArtifactProvider.h
 * @brief Graphics-owned CPU resource provider for generated mesh artifacts.
 */

#include "common/ArtifactPublication.h"
#include "common/RuntimeHandle.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eve::graphics {

class Graphics;
class Mesh;

/** @brief Owner tag for graphics artifact runtime handles. */
struct GraphicsArtifactHandleTag {};
/** @brief Generation-qualified handle for a graphics artifact resource. */
using GraphicsArtifactHandle = eve::RuntimeHandle<GraphicsArtifactHandleTag>;

/** @brief Backend-neutral mesh descriptor shared by Vulkan and WebGPU adapters. */
struct GraphicsArtifactDescriptor {
    eve::PersistentId id;
    std::string buildKey;
    std::string role;
    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
    std::uint64_t checksum = 0;
    /** @brief Actual backend vertex stride when a live upload exists. */
    std::uint32_t backendVertexStride = 0;
    /** @brief Actual backend index element size when a live upload exists. */
    std::uint32_t backendIndexElementSize = 0;

    friend bool operator==(const GraphicsArtifactDescriptor&,
                           const GraphicsArtifactDescriptor&) = default;
};

/** @brief Queryable backend-neutral mesh resource owned by the graphics module. */
struct GraphicsArtifactResource {
    eve::PersistentId id;
    std::string buildKey;
    GraphicsArtifactHandle handle;
    std::string role;
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<std::uint32_t> indices;
    /** @brief Backend name observed when upload was attempted, or `cpu`. */
    std::string backendName = "cpu";
    /** @brief Explicit upload outcome: `not-attempted`, `uploaded`, or capability-degraded. */
    std::string uploadState = "not-attempted";
    /** @brief Whether the provider currently owns a backend resource for this descriptor. */
    bool gpuResident = false;
    /** @brief Actual backend vertex stride, zero when no upload exists. */
    std::uint32_t backendVertexStride = 0;
    /** @brief Actual backend index element size, zero when no upload exists. */
    std::uint32_t backendIndexElementSize = 0;
};

/**
 * @brief Computes the same descriptor a WebGPU backend would expose without a device.
 *
 * This adapter intentionally consumes only the common publication view. It is
 * used for Vulkan/WebGPU parity tests and does not include either backend SDK.
 */
class WebGpuArtifactDescriptorAdapter final {
public:
    /** @brief Describe the selected mesh part, or return empty for invalid input. */
    [[nodiscard]] static std::optional<GraphicsArtifactDescriptor> describe(
        const eve::artifact::PublicationView& publication);
};

/**
 * @brief Real graphics-module provider for generated mesh publication.
 *
 * The provider always owns the backend-neutral descriptor. When a live Graphics
 * backend is bound, prepare also calls its normal newMeshFromArrays upload
 * boundary; an unavailable/uninitialized backend is an explicit observable CPU
 * capability-degraded path rather than a hidden mock.
 */
class GraphicsArtifactProvider final : public eve::artifact::IGraphicsArtifactAdapter {
public:
    /** @brief Stage and copy the mesh leaf from a generated publication. */
    [[nodiscard]] eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>> prepare(
        const eve::artifact::PublicationView& publication) override;
    /** @brief Export resource identity, build keys, handles and CPU descriptors. */
    [[nodiscard]] eve::Result<eve::Value> snapshotState() const override;
    /** @brief Replace resources from validated backend-neutral CPU descriptors. */
    [[nodiscard]] eve::Result<void> restoreState(const eve::Value& state) override;
    /** @brief Return whether the graphics registry has no committed resources. */
    [[nodiscard]] bool emptyState() const noexcept override { return resources_.empty(); }
    /** @brief Clear provider state through the common restore-cleanup hook. */
    void clearState() noexcept override { clear(); }

    /**
     * @brief Find a committed resource; the pointer is borrowed.
     * @ownership Borrowed from this provider; callers must not delete or retain it.
     * @nullable Null when no committed resource has the requested identity.
     * @lifetime Until provider clear/restore/destruction or any later commit that
     *            reallocates the provider's record vector; callers must not cache it.
     * @thread Graphics-provider owner thread; external synchronization is required.
     * @reentrancy No callbacks are made.
     */
    [[nodiscard]] const GraphicsArtifactResource* find(eve::PersistentId id) const noexcept;
    /** @brief Return the number of committed graphics resources. */
    [[nodiscard]] std::size_t size() const noexcept { return resources_.size(); }
    /** @brief Return a deterministic checksum of one resource's CPU descriptor. */
    [[nodiscard]] std::uint64_t checksum(eve::PersistentId id) const noexcept;
    /** @brief Return the backend-neutral descriptor for one resource. */
    [[nodiscard]] std::optional<GraphicsArtifactDescriptor> descriptor(
        eve::PersistentId id) const;
    /** @brief Return whether a resource was backed by the live Graphics upload boundary. */
    [[nodiscard]] bool isGpuResident(eve::PersistentId id) const noexcept;
    /** @brief Remove all resources and reset the local handle allocator. */
    void clear() noexcept;
    /** @brief Inject a prepare failure for composition tests. */
    void setPrepareFailure(bool enabled) noexcept { failPrepare_ = enabled; }

    /** @brief Bind a live Graphics resource factory without transferring ownership. */
    void bindGraphics(Graphics* graphics) noexcept;
    /** @brief Detach the expected backend and release its provider-owned meshes. */
    void detachGraphics(Graphics* graphics) noexcept;

private:
    friend class GraphicsArtifactStage;
    struct RuntimeResource {
        GraphicsArtifactResource descriptor;
        Mesh* uploadedMesh = nullptr;
    };
    static void uploadIfAvailable(Graphics* graphics, RuntimeResource& resource) noexcept;
    void commit(RuntimeResource resource) noexcept;
    void release(RuntimeResource& resource) noexcept;
    std::vector<RuntimeResource> resources_;
    Graphics* graphics_ = nullptr;
    std::uint32_t nextIndex_ = 0;
    bool failPrepare_ = false;
};

/** @brief Return the process-owned graphics artifact provider singleton. */
[[nodiscard]] GraphicsArtifactProvider& graphicsArtifactProvider() noexcept;
/** @brief Register the graphics provider in the common capability registry. */
void registerGraphicsArtifactProvider(Graphics* graphics = nullptr);
/** @brief Detach a Graphics instance before its derived backend is destroyed. */
void detachGraphicsArtifactProvider(Graphics* graphics) noexcept;

}  // namespace eve::graphics
