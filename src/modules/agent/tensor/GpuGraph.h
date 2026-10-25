#pragma once

#include "tensor/Graph.h"

namespace eve::agent::detail {
// Private graph recipe. Feeds: packed weights, features, additive legal mask,
// one-hot target, learning rate. Output: probabilities or updated packed weights.
/** @brief PolicyGraph public API. */
struct PolicyGraph {
    tensor::Graph graph;
    int           output = -1;
};
/** @brief Make policy graph. */
PolicyGraph makePolicyGraph(int features, int hidden, int actions, bool training);
}  // namespace eve::agent::detail
