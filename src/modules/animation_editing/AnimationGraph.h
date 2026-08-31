#pragma once

#include "animation_editing/AnimationEditingTypes.h"
#include "editing/EditingGraph.h"

#include <string>
#include <functional>
#include <vector>

namespace eve::animation {
class AnimClip;
class AnimSkeleton;
class AnimStateMachine;
}

namespace eve::animation_editing {

using editing::GraphConnectionDecision;
using editing::GraphDocumentData;
using editing::GraphEdgeRecord;
using editing::GraphNodeId;
using editing::GraphNodeRecord;
using editing::GraphPinDirection;
using editing::GraphPinId;
using editing::GraphPinRecord;
using editing::IGraphDomainProvider;

/** @brief Compilation result for a stable-reference animation state graph asset. */
struct AnimationGraphCompileResult {
    EditorStatus status = EditorStatus::Failed;
    Revision documentRevision = 0;
    std::string definition;
    std::vector<EditorDiagnostic> diagnostics;
};

/**
 * @brief `animation.state` graph domain matching AnimStateMachine semantics.
 *
 * State nodes store clip asset references; transition nodes store blend/exit
 * metadata and one optional float/bool/trigger condition. Runtime pointers are
 * deliberately resolved only by the animation host after compilation.
 */
class AnimationStateGraphDomain final : public IGraphDomainProvider {
public:
    std::string domain() const override { return "animation.state"; }
    GraphConnectionDecision canConnect(const GraphPinRecord& from,
                                       const GraphPinRecord& to) const override;
    /** @brief Construct a state node referencing one animation clip asset. */
    EditorResult<GraphNodeRecord> makeStateNode(const GraphNodeId& id,
                                                const std::string& clipAsset) const;
    /** @brief Construct a transition node with default blend metadata. */
    EditorResult<GraphNodeRecord> makeTransitionNode(const GraphNodeId& id) const;
    /** @brief Validate and compile a graph into a deterministic runtime-neutral definition. */
    AnimationGraphCompileResult compile(const GraphDocumentData& graph) const;
};

/** @brief Optional bridge constructing the real animation runtime from a validated graph. */
class AnimationStateGraphRuntimeBuilder {
public:
    using ClipResolver = std::function<animation::AnimClip*(const std::string& asset)>;
    /**
     * @brief Build an AnimStateMachine; caller owns the returned machine.
     * @param graph Immutable animation state graph.
     * @param skeleton Non-owning runtime skeleton.
     * @param clips Host-owned stable asset-reference resolver.
     */
    EditorResult<animation::AnimStateMachine*> build(const GraphDocumentData& graph,
                                                     animation::AnimSkeleton* skeleton,
                                                     const ClipResolver& clips) const;
};

}  // namespace eve::animation_editing
