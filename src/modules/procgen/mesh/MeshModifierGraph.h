#pragma once

#include "common/Result.h"
#include "procgen/MeshBuild.h"
#include "procgen/spline/SplinePath.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

/** @brief Per-node telemetry captured by the latest successful mesh graph execution. */
struct MeshModifierNodeMetric {
    std::string id;
    int         vertexCount   = 0;
    int         triangleCount = 0;
    float       milliseconds  = 0.f;
    bool        cacheHit      = false;
    bool        fused         = false;
};

/** @brief One vertex of an owning two-dimensional spline extrusion profile. */
struct SplineProfilePoint {
    float x = 0.f;
    float y = 0.f;
};

/** @brief Owning cross-section snapshot used by mesh.splineExtrude. */
struct SplineProfile {
    std::vector<SplineProfilePoint> points;
    bool                            closed = true;
};

/**
 * @brief Deterministic CPU data-flow graph for procedural mesh deformation and composition.
 *
 * The graph owns immutable copies of input meshes and publishes owning output snapshots.
 * Mutations invalidate derived caches. Execution is graph-owning-thread only and is not
 * reentrant. Consecutive topology-preserving per-vertex nodes are evaluated in one vertex
 * traversal; topology-changing nodes form explicit fusion barriers.
 */
class MeshModifierGraph {
public:
    /** @brief Add a node from the reflected operation catalogue. */
    [[nodiscard]] Result<void> addNode(std::string id, std::string operation);
    /** @brief Remove a node and all incoming edges that reference it. */
    [[nodiscard]] Result<void> removeNode(std::string_view id);
    /** @brief Connect one node output to a numbered mesh input. */
    [[nodiscard]] Result<void> connect(std::string_view fromId, std::string_view toId, int inputIndex = 0);
    /** @brief Disconnect a numbered mesh input. */
    [[nodiscard]] Result<void> disconnect(std::string_view toId, int inputIndex = 0);
    /** @brief Copy an owning mesh snapshot into an input node. */
    [[nodiscard]] Result<void> setNodeMesh(std::string_view id, const MeshBuild& mesh);
    /** @brief Copy an owning path snapshot into a spline deformation or generation node. */
    [[nodiscard]] Result<void> setNodeSplinePath(std::string_view id, const SplinePath& path);
    /** @brief Copy an owning cross-section snapshot into a spline extrusion node. */
    [[nodiscard]] Result<void> setNodeSplineProfile(std::string_view id, const SplineProfile& profile);
    /** @brief Set a finite floating-point node parameter. */
    [[nodiscard]] Result<void> setNodeFloat(std::string_view id, std::string key, float value);
    /** @brief Set an integer node parameter. */
    [[nodiscard]] Result<void> setNodeInt(std::string_view id, std::string key, int value);
    /** @brief Set a string node parameter. */
    [[nodiscard]] Result<void> setNodeString(std::string_view id, std::string key, std::string value);

    /**
     * @brief Validate topology, inputs, parameters, and mesh stream invariants.
     * @return Success only when every node can be executed without publishing partial state.
     */
    [[nodiscard]] Result<void> validateResult() const;
    /**
     * @brief Execute one output and return an independent owning mesh snapshot.
     * @param outputId Requested output node.
     * @return Mesh output or a structured diagnostic. Failure preserves prior node caches.
     */
    [[nodiscard]] Result<MeshBuild> executeResult(std::string_view outputId);

    /** @brief Clear all derived outputs without changing graph topology or inputs. */
    void clearCache();
    /** @brief Return the monotonic graph mutation revision. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    /** @brief Return how many execution plans have been compiled. */
    [[nodiscard]] std::uint64_t executionPlanBuildCount() const noexcept { return executionPlanBuildCount_; }
    /** @brief Return segment count in the latest successful execution plan. */
    [[nodiscard]] int compiledSegmentCount() const noexcept { return compiledSegmentCount_; }
    /** @brief Return logical operations evaluated by fused vertex traversals in the latest run. */
    [[nodiscard]] int fusedOperationCount() const noexcept { return fusedOperationCount_; }
    /** @brief Return metrics from the latest successful execution. */
    [[nodiscard]] const std::vector<MeshModifierNodeMetric>& metrics() const noexcept { return metrics_; }

    /** @brief Report whether a node exists. */
    [[nodiscard]] bool hasNode(std::string_view id) const;
    /** @brief Return stable insertion-order node count. */
    [[nodiscard]] int nodeCount() const noexcept { return static_cast<int>(nodeOrder_.size()); }
    /** @brief Return a node id by insertion order, or an empty string when out of range. */
    [[nodiscard]] std::string nodeId(int index) const;
    /** @brief Return a node operation, or an empty string when missing. */
    [[nodiscard]] std::string nodeOperation(std::string_view id) const;

    /** @brief Return reflected operation count. */
    [[nodiscard]] static int operationCount();
    /** @brief Return reflected operation id, or an empty string for an invalid index. */
    [[nodiscard]] static std::string operationId(int index);
    /** @brief Return required mesh input count, or -1 for an unknown operation. */
    [[nodiscard]] static int operationInputCount(std::string_view operation);
    /** @brief Return reflected parameter count. */
    [[nodiscard]] static int operationParamCount(std::string_view operation);
    /** @brief Return reflected parameter key. */
    [[nodiscard]] static std::string operationParamKey(std::string_view operation, int index);
    /** @brief Return reflected parameter kind: float, int, or string. */
    [[nodiscard]] static std::string operationParamKind(std::string_view operation, int index);
    /** @brief Return encoded reflected parameter default. */
    [[nodiscard]] static std::string operationParamDefault(std::string_view operation, int index);

private:
    struct Node {
        std::string                                  id;
        std::string                                  operation;
        std::vector<std::string>                     inputs;
        std::unordered_map<std::string, float>       floats;
        std::unordered_map<std::string, int>         ints;
        std::unordered_map<std::string, std::string> strings;
        MeshBuild                                    inputMesh;
        bool                                         hasInputMesh = false;
        SplinePath                                   splinePath;
        bool                                         hasSplinePath = false;
        SplineProfile                                splineProfile;
        bool                                         hasSplineProfile = false;
        MeshBuild                                    cache;
        bool                                         cacheValid = false;
    };
    struct Segment {
        std::vector<std::string> nodes;
        bool                     fusedVertexTraversal = false;
    };

    [[nodiscard]] Result<std::vector<Segment>> compilePlan(std::string_view outputId) const;
    [[nodiscard]] Result<MeshBuild> executeNode(const Node&                                       node,
                                                const std::unordered_map<std::string, MeshBuild>& outputs) const;
    [[nodiscard]] Result<MeshBuild> executeFused(const Segment&                                    segment,
                                                 const std::unordered_map<std::string, MeshBuild>& outputs) const;
    [[nodiscard]] Result<void> validateNode(std::string_view id, std::unordered_map<std::string, int>& states) const;
    void                       invalidate();

    std::unordered_map<std::string, Node> nodes_;
    std::vector<std::string>              nodeOrder_;
    std::vector<MeshModifierNodeMetric>   metrics_;
    std::uint64_t                         revision_                = 0;
    std::uint64_t                         executionPlanBuildCount_ = 0;
    int                                   compiledSegmentCount_    = 0;
    int                                   fusedOperationCount_     = 0;
};

}  // namespace eve::procgen
