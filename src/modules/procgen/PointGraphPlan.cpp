#include "procgen/PointGraph.h"

#include <algorithm>
#include <functional>

namespace eve::procgen {

void PointGraph::invalidateTopology(const std::string& changedNode) {
    for (auto it = executionPlans_.begin(); it != executionPlans_.end();) {
        const auto& nodes = it->second->topologicalOrder;
        if (std::find(nodes.begin(), nodes.end(), changedNode) == nodes.end()) {
            ++it;
            continue;
        }
        if (executionPlan_ == it->second) executionPlan_.reset();
        std::erase(executionPlanRecency_, it->first);
        it = executionPlans_.erase(it);
    }
}

void PointGraph::compileExecutionPlan(const std::string& outputId) {
    if (const auto found = executionPlans_.find(outputId); found != executionPlans_.end()) {
        executionPlan_ = found->second;
        std::erase(executionPlanRecency_, outputId);
        executionPlanRecency_.push_back(outputId);
        return;
    }
    executionPlan_.reset();

    ExecutionPlan                           plan;
    std::unordered_map<std::string, int>    states;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        if (!error_.empty() || states[id] == 2) return;
        if (states[id] == 1) {
            error_ = "cycle at node: " + id;
            return;
        }
        const auto found = nodes_.find(id);
        if (found == nodes_.end()) {
            error_ = "unknown node: " + id;
            return;
        }
        states[id]           = 1;
        const int inputCount = getOperationInputCount(found->second.operation);
        for (int input = 0; input < inputCount; ++input) {
            if (found->second.inputs[input].empty()) {
                error_ = found->second.operation + " requires input " + std::to_string(input) + ": " + id;
                return;
            }
            visit(found->second.inputs[input]);
        }
        if (!error_.empty()) return;
        states[id] = 2;
        plan.topologicalOrder.push_back(id);
    };
    visit(outputId);
    if (!error_.empty()) return;

    std::unordered_map<std::string, std::vector<std::string>> consumers;
    for (const std::string& id : plan.topologicalOrder) {
        const Node& node = nodes_.at(id);
        for (const std::string& input : node.inputs)
            if (!input.empty() && states[input] == 2) consumers[input].push_back(id);
    }
    std::unordered_map<std::string, bool> assigned;
    for (const std::string& id : plan.topologicalOrder) {
        if (assigned[id]) continue;
        ExecutionSegment segment;
        segment.nodes.push_back(id);
        assigned[id]       = true;
        std::string cursor = id;
        while (segment.nodes.size() < 4 && nodes_.at(cursor).operation == "transform" &&
               consumers[cursor].size() == 1) {
            const std::string& next = consumers[cursor].front();
            if (assigned[next] || nodes_.at(next).operation != "transform") break;
            segment.nodes.push_back(next);
            assigned[next] = true;
            cursor         = next;
        }
        segment.gpuTransformChain      = segment.nodes.size() >= 2 && nodes_.at(id).operation == "transform";
        const std::size_t segmentIndex = plan.segments.size();
        if (segment.gpuTransformChain) plan.segmentByOutput.emplace(segment.nodes.back(), segmentIndex);
        plan.segments.push_back(std::move(segment));
    }
    executionPlan_ = std::make_shared<const ExecutionPlan>(std::move(plan));
    executionPlans_.emplace(outputId, executionPlan_);
    executionPlanRecency_.push_back(outputId);
    constexpr std::size_t maximumCachedPlans = 16;
    if (executionPlanRecency_.size() > maximumCachedPlans) {
        executionPlans_.erase(executionPlanRecency_.front());
        executionPlanRecency_.erase(executionPlanRecency_.begin());
    }
    ++executionPlanBuildCount_;
}

}  // namespace eve::procgen
