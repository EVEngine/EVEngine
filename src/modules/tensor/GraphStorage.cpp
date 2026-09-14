#include "common/Exception.h"
#include "tensor/Graph.h"

namespace eve::tensor {
int Graph::product(const int* dims, int rank) {
    int n = 1;
    for (int i = 0; i < rank; ++i) {
        if (dims[i] <= 0) throw eve::Exception("Graph: dims must be > 0");
        n *= dims[i];
    }
    return n;
}

int Graph::addNode(GraphNode node) {
    if (node.rank > 0 && node.size <= 0) node.size = product(node.dims, node.rank);
    nodes_.push_back(std::move(node));
    return int(nodes_.size()) - 1;
}
}  // namespace eve::tensor
