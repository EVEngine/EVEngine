#include "tensor/Optimizer.h"
#include "tensor/CpuKernels.h"

#include "common/Exception.h"

#include <algorithm>
#include <queue>

namespace eve::tensor {
namespace {

int positionOf(const std::vector<int> &order, int id) {
    return int(std::find(order.begin(), order.end(), id) - order.begin());
}

std::vector<int> topoOrder(const Graph &g, const std::vector<char> &live, int outputNode) {
    const int n = g.nodeCount();
    std::vector<int> indeg(static_cast<size_t>(n), 0);
    std::vector<std::vector<int>> outs(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        if (!live[static_cast<size_t>(i)]) continue;
        const auto &nd = g.node(i);
        auto link = [&](int from) {
            if (from < 0 || !live[static_cast<size_t>(from)]) return;
            outs[static_cast<size_t>(from)].push_back(i);
            ++indeg[static_cast<size_t>(i)];
        };
        link(nd.in0);
        link(nd.in1);
        link(nd.in2);
        link(nd.in3);
        link(nd.in4);
    }
    std::queue<int> q;
    for (int i = 0; i < n; ++i)
        if (live[static_cast<size_t>(i)] && indeg[static_cast<size_t>(i)] == 0) q.push(i);
    std::vector<int> order;
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        order.push_back(u);
        for (int v : outs[static_cast<size_t>(u)])
            if (--indeg[static_cast<size_t>(v)] == 0) q.push(v);
    }
    const int liveCount = int(std::count(live.begin(), live.end(), char(1)));
    if (int(order.size()) != liveCount)
        throw eve::Exception("optimizeGraph: cycle in graph");
    (void)outputNode;
    return order;
}

/** Fold elementwise ops whose inputs are all Const into Const nodes. */
void constantFold(Graph &g, const std::vector<int> &order) {
    for (int id : order) {
        auto &nd = g.node(id);
        if (nd.type == OpType::Const || !kernels::isElementwiseOp(nd.type)) continue;
        const int inputs[5] = {nd.in0, nd.in1, nd.in2, nd.in3, nd.in4};
        bool allConst = true;
        for (int k = 0; k < 5; ++k)
            if (inputs[k] >= 0 &&
                (g.node(inputs[k]).type != OpType::Const ||
                 !g.node(inputs[k]).constBytes.empty()))  // quantized consts are opaque
                allConst = false;
        if (!allConst) continue;

        nd.constData.resize(static_cast<size_t>(nd.size));
        if (nd.type == OpType::Where) {
            const auto &c = g.node(nd.in0).constData;
            const auto &a = g.node(nd.in1).constData;
            const auto &b = g.node(nd.in2).constData;
            for (int i = 0; i < nd.size; ++i)
                nd.constData[static_cast<size_t>(i)] =
                    c[static_cast<size_t>(i)] > 0.5f ? a[static_cast<size_t>(i)]
                                                      : b[static_cast<size_t>(i)];
        } else if (nd.type == OpType::Add || nd.type == OpType::Sub ||
                   nd.type == OpType::Multiply || nd.type == OpType::Divide) {
            const auto &na = g.node(nd.in0);
            const auto &nb = g.node(nd.in1);
            kernels::binaryOp(nd.type, na.constData.data(), na.dims, na.rank,
                              nb.constData.data(), nb.dims, nb.rank, nd.constData.data(), nd.dims,
                              nd.rank);
        } else {
            kernels::unaryOp(nd.type, g.node(nd.in0).constData.data(), nd.size,
                             nd.constData.data(), nd.s0, nd.s1);
        }
        nd.type = OpType::Const;
        nd.in0 = nd.in1 = nd.in2 = nd.in3 = nd.in4 = -1;
    }
}

GroupKind kindForOp(OpType t) {
    switch (t) {
        case OpType::MatMul: return GroupKind::MatMul;
        case OpType::Conv1d: return GroupKind::Conv1d;
        case OpType::Conv2d: return GroupKind::Conv2d;
        case OpType::MaxPool2d: return GroupKind::MaxPool2d;
        case OpType::AvgPool2d: return GroupKind::AvgPool2d;
        case OpType::Softmax:
        case OpType::LogSoftmax: return GroupKind::Softmax;
        case OpType::LayerNorm: return GroupKind::LayerNorm;
        case OpType::RMSNorm: return GroupKind::RMSNorm;
        case OpType::ReduceSum:
        case OpType::ReduceMean:
        case OpType::ReduceMin:
        case OpType::ReduceMax: return GroupKind::Reduce;
        case OpType::ArgMax: return GroupKind::ArgMax;
        case OpType::Embedding: return GroupKind::Embedding;
        case OpType::Concat: return GroupKind::Concat;
        case OpType::Slice: return GroupKind::Slice;
        case OpType::Permute:
        case OpType::Transpose: return GroupKind::Permute;
        case OpType::ScaledDotProductAttention: return GroupKind::Sdpa;
        case OpType::Resize2d: return GroupKind::Resize2d;
        case OpType::Reshape:
        case OpType::Flatten:
        case OpType::Cast: return GroupKind::Alias;
        default: return GroupKind::Elementwise;
    }
}

void fillGroupAttrs(FusedGroup &g, const Graph &graph, int nodeId) {
    const auto &nd = graph.node(nodeId);
    g.s0 = nd.s0;
    g.s1 = nd.s1;
    g.s2 = nd.s2;
    g.s3 = nd.s3;
    g.i0 = nd.i0;
    g.i1 = nd.i1;
    g.i2 = nd.i2;
    g.i3 = nd.i3;
    for (int k = 0; k < Tensor::kMaxRank; ++k) g.perm[k] = nd.perm[k];
    g.permRank = nd.permRank;
    g.dtype = nd.dtype;
    g.op = nd.type;
    switch (nd.type) {
        case OpType::LogSoftmax: g.logMode = true; break;
        case OpType::LayerNorm:
            g.hasScale = nd.in1 >= 0;
            g.hasBias = nd.in2 >= 0;
            break;
        case OpType::RMSNorm: g.hasScale = nd.in1 >= 0; break;
        case OpType::ScaledDotProductAttention: g.masked = nd.in3 >= 0; break;
        default: break;
    }
}

bool isEpilogueOp(OpType t) {
    switch (t) {
        case OpType::Add:
        case OpType::AddScalar:
        case OpType::SubScalar:
        case OpType::MulScalar:
        case OpType::DivScalar:
        case OpType::Neg:
        case OpType::Abs:
        case OpType::Exp:
        case OpType::Sqrt:
        case OpType::Relu:
        case OpType::Sigmoid:
        case OpType::Tanh:
        case OpType::Gelu:
        case OpType::Silu:
        case OpType::Clamp:
        case OpType::MaximumScalar:
        case OpType::MinimumScalar:
        case OpType::PowScalar:
            return true;
        default:
            return false;
    }
}

}  // namespace

OptimizedGraph optimizeGraph(const Graph &graph, int outputNode) {
    const int n = graph.nodeCount();
    if (n <= 0 || outputNode < 0 || outputNode >= n)
        throw eve::Exception("optimizeGraph: invalid output");

    // 1. dead-code elimination
    std::vector<char> live(static_cast<size_t>(n), 0);
    {
        std::vector<int> stack = {outputNode};
        while (!stack.empty()) {
            int u = stack.back();
            stack.pop_back();
            if (u < 0 || u >= n || live[static_cast<size_t>(u)]) continue;
            live[static_cast<size_t>(u)] = 1;
            const auto &nd = graph.node(u);
            if (nd.in0 >= 0) stack.push_back(nd.in0);
            if (nd.in1 >= 0) stack.push_back(nd.in1);
            if (nd.in2 >= 0) stack.push_back(nd.in2);
            if (nd.in3 >= 0) stack.push_back(nd.in3);
            if (nd.in4 >= 0) stack.push_back(nd.in4);
        }
    }

    // 2. topological order (on a mutable copy; const folding may rewrite nodes)
    Graph g = graph;
    std::vector<int> order = topoOrder(g, live, outputNode);
    constantFold(g, order);
    // Re-topo after folding (folded nodes become roots; order is still valid but
    // recompute to keep positions consistent with the rewritten graph).
    order = topoOrder(g, live, outputNode);

    // 3. fusion
    std::vector<int> consumerCount(static_cast<size_t>(n), 0);
    for (int id : order) {
        const auto &nd = g.node(id);
        auto inc = [&](int p) {
            if (p >= 0 && live[static_cast<size_t>(p)]) ++consumerCount[static_cast<size_t>(p)];
        };
        inc(nd.in0);
        inc(nd.in1);
        inc(nd.in2);
        inc(nd.in3);
        inc(nd.in4);
    }

    std::vector<char> visited(static_cast<size_t>(n), 0);
    std::vector<int> groupOf(static_cast<size_t>(n), -1);
    std::vector<char> absorbed;
    std::vector<FusedGroup> groups;

    // 3a. elementwise chains (reverse topo, walking backward through
    //     single-consumer elementwise producers)
    for (int idx = int(order.size()) - 1; idx >= 0; --idx) {
        const int id = order[static_cast<size_t>(idx)];
        if (!live[static_cast<size_t>(id)] || visited[static_cast<size_t>(id)]) continue;
        if (!kernels::isElementwiseOp(g.node(id).type)) continue;
        FusedGroup grp;
        grp.kind = GroupKind::Elementwise;
        grp.outputNode = id;
        fillGroupAttrs(grp, g, id);
        std::vector<int> stack = {id};
        while (!stack.empty()) {
            int u = stack.back();
            stack.pop_back();
            if (!live[static_cast<size_t>(u)] || visited[static_cast<size_t>(u)]) continue;
            visited[static_cast<size_t>(u)] = 1;
            groupOf[static_cast<size_t>(u)] = int(groups.size());
            grp.nodes.push_back(u);
            const auto &nd = g.node(u);
            const int inputs[5] = {nd.in0, nd.in1, nd.in2, nd.in3, nd.in4};
            for (int k = 0; k < 5; ++k) {
                const int p = inputs[k];
                if (p >= 0 && live[static_cast<size_t>(p)] &&
                    kernels::isElementwiseOp(g.node(p).type) &&
                    consumerCount[static_cast<size_t>(p)] == 1) {
                    stack.push_back(p);
                }
            }
        }
        std::stable_sort(grp.nodes.begin(), grp.nodes.end(),
                         [&](int a, int b) { return positionOf(order, a) < positionOf(order, b); });
        absorbed.push_back(0);
        groups.push_back(std::move(grp));
    }

    // 3b. remaining nodes -> single-op groups
    for (int id : order) {
        if (!live[static_cast<size_t>(id)] || visited[static_cast<size_t>(id)]) continue;
        // Placeholders and consts are data nodes (group inputs), never groups.
        if (g.node(id).type == OpType::Placeholder || g.node(id).type == OpType::Const) continue;
        visited[static_cast<size_t>(id)] = 1;
        groupOf[static_cast<size_t>(id)] = int(groups.size());
        FusedGroup grp;
        grp.outputNode = id;
        grp.kind = kindForOp(g.node(id).type);
        fillGroupAttrs(grp, g, id);
        grp.nodes.push_back(id);
        absorbed.push_back(0);
        groups.push_back(std::move(grp));
    }

    // 3c. matmul / conv bias + activation epilogue fusion
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        FusedGroup &grp = groups[gi];
        if (grp.kind != GroupKind::MatMul && grp.kind != GroupKind::Conv1d &&
            grp.kind != GroupKind::Conv2d)
            continue;
        const int m = grp.outputNode;
        // exactly one consumer
        std::vector<int> consumers;
        for (int id : order) {
            const auto &nd = g.node(id);
            const int inputs[5] = {nd.in0, nd.in1, nd.in2, nd.in3, nd.in4};
            for (int k = 0; k < 5; ++k)
                if (inputs[k] == m) consumers.push_back(id);
        }
        if (consumers.size() != 1) continue;
        const int c = consumers[0];
        const size_t egIdx = static_cast<size_t>(groupOf[static_cast<size_t>(c)]);
        if (egIdx >= groups.size() || absorbed[egIdx]) continue;
        FusedGroup &eg = groups[egIdx];
        if (eg.kind != GroupKind::Elementwise) continue;
        if (consumerCount[static_cast<size_t>(c)] != 1 && c != outputNode) continue;

        // Validate the chain: single-consumer elementwise ops starting at m.
        bool ok = true;
        int biasNode = -1;
        int prev = m;
        const auto &mn = g.node(m);
        for (int u : eg.nodes) {
            const auto &nd = g.node(u);
            if (!isEpilogueOp(nd.type)) {
                ok = false;
                break;
            }
            if (nd.in0 != prev) {
                ok = false;
                break;
            }
            if (nd.type == OpType::Add) {
                const int other = nd.in1;
                if (other < 0 || g.node(other).type != OpType::Const) {
                    ok = false;
                    break;
                }
                const auto &bn = g.node(other);
                const bool biasOk = (bn.rank == 1 && bn.dims[0] == mn.dims[mn.rank - 1]) ||
                                    (bn.rank == 2 && bn.dims[0] == 1 &&
                                     bn.dims[1] == mn.dims[mn.rank - 1]);
                if (!biasOk) {
                    ok = false;
                    break;
                }
                if (biasNode == -1) biasNode = other;
            }
            if (nd.in1 >= 0 && nd.type != OpType::Add && nd.in1 != u && nd.in1 != prev) {
                ok = false;
                break;
            }
            prev = u;
        }
        if (!ok) continue;

        // Merge: absorb the elementwise group into the matmul/conv group.
        grp.nodes.insert(grp.nodes.end(), eg.nodes.begin(), eg.nodes.end());
        grp.epilogue = eg.nodes;
        grp.biasNode = biasNode;
        grp.outputNode = eg.outputNode;
        for (int u : eg.nodes) groupOf[static_cast<size_t>(u)] = int(gi);
        absorbed[egIdx] = 1;
    }

    // 4. collect external inputs per group (first-use order)
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        if (absorbed[gi]) continue;
        FusedGroup &grp = groups[gi];
        std::vector<char> inGroup(static_cast<size_t>(n), 0);
        for (int u : grp.nodes) inGroup[static_cast<size_t>(u)] = 1;
        grp.inputs.clear();
        for (int u : grp.nodes) {
            const auto &nd = g.node(u);
            const int inputs[5] = {nd.in0, nd.in1, nd.in2, nd.in3, nd.in4};
            for (int k = 0; k < 5; ++k) {
                const int p = inputs[k];
                if (p < 0 || inGroup[static_cast<size_t>(p)]) continue;
                // Keep one entry per input *slot* (not per node): kernels bind by
                // position, so the same node feeding two slots (q=k=v, where(a,a,b))
                // must appear twice.
                grp.inputs.push_back(p);
            }
        }
        std::stable_sort(grp.nodes.begin(), grp.nodes.end(),
                         [&](int a, int b) { return positionOf(order, a) < positionOf(order, b); });
    }

    // 5. memory planning
    OptimizedGraph opt;
    opt.order = order;
    opt.outputNode = outputNode;
    opt.nodeSlot.assign(static_cast<size_t>(n), -1);

    // execution order of groups
    std::vector<size_t> exec;
    for (size_t gi = 0; gi < groups.size(); ++gi)
        if (!absorbed[gi]) exec.push_back(gi);
    std::stable_sort(exec.begin(), exec.end(), [&](size_t a, size_t b) {
        return positionOf(order, groups[a].outputNode) < positionOf(order, groups[b].outputNode);
    });

    // group index of each node (for liveness)
    std::vector<int> groupIndexByNode(static_cast<size_t>(n), -1);
    for (size_t gi = 0; gi < groups.size(); ++gi)
        if (!absorbed[gi])
            for (int u : groups[gi].nodes) groupIndexByNode[static_cast<size_t>(u)] = int(gi);

    // Alias (reshape/flatten/cast) outputs share their producer's buffer. A
    // consumer of the alias keeps the *producer's* buffer alive, so liveness
    // must resolve through alias chains before computing last-use.
    std::vector<int> realProducer(static_cast<size_t>(n), -1);
    for (int id = 0; id < n; ++id) {
        int cur = id;
        while (cur >= 0 && (g.node(cur).type == OpType::Reshape ||
                            g.node(cur).type == OpType::Flatten ||
                            g.node(cur).type == OpType::Cast))
            cur = g.node(cur).in0;
        realProducer[static_cast<size_t>(id)] = cur;
    }

    // consumer group per group (last use)
    std::vector<int> lastUse(exec.size(), -1);
    for (size_t gi = 0; gi < exec.size(); ++gi) {
        const FusedGroup &grp = groups[exec[gi]];
        for (int p : grp.inputs) {
            const int real = realProducer[static_cast<size_t>(p)];
            const int prod = real >= 0 ? groupIndexByNode[static_cast<size_t>(real)] : -1;
            if (prod < 0) continue;
            const auto it = std::find(exec.begin(), exec.end(), size_t(prod));
            if (it == exec.end()) continue;
            const size_t pIdx = static_cast<size_t>(it - exec.begin());
            lastUse[pIdx] = std::max(lastUse[pIdx], int(gi));
        }
        if (grp.outputNode == outputNode && lastUse[gi] < 0) lastUse[gi] = int(gi);
    }

    struct Slot {
        int capacity = 0;
        int freeAt = 0;
    };
    std::vector<Slot> slots;
    std::vector<char> persistent(static_cast<size_t>(n), 0);

    auto allocate = [&](int nodeId, int currentTime, int freeAt) {
        if (opt.nodeSlot[static_cast<size_t>(nodeId)] >= 0) return;  // already allocated
        const int size = g.node(nodeId).size;
        int best = -1;
        for (size_t s = 0; s < slots.size(); ++s) {
            if (slots[s].capacity >= size && slots[s].freeAt <= currentTime) {
                best = int(s);
                break;
            }
        }
        if (best < 0) {
            best = int(slots.size());
            slots.push_back({size, freeAt});
        } else {
            slots[static_cast<size_t>(best)].freeAt = freeAt;
        }
        opt.nodeSlot[static_cast<size_t>(nodeId)] = best;
    };

    // persistent nodes: placeholders, consts, final output
    for (int id : order) {
        const auto &nd = g.node(id);
        if (nd.type == OpType::Placeholder || nd.type == OpType::Const) {
            persistent[static_cast<size_t>(id)] = 1;
            allocate(id, 0, 1 << 30);
        }
    }
    persistent[static_cast<size_t>(outputNode)] = 1;

    // group outputs
    for (size_t gi : exec) {
        FusedGroup &grp = groups[gi];
        const int out = grp.outputNode;
        if (grp.kind == GroupKind::Alias) {
            const auto &nd = g.node(out);
            if (nd.in0 < 0) throw eve::Exception("optimizeGraph: alias without input");
            const int srcSlot = opt.nodeSlot[static_cast<size_t>(nd.in0)];
            if (srcSlot < 0) throw eve::Exception("optimizeGraph: alias input without slot");
            opt.nodeSlot[static_cast<size_t>(out)] = srcSlot;
            continue;
        }
        if (persistent[static_cast<size_t>(out)]) {
            allocate(out, 0, 1 << 30);
        } else {
            const size_t giPos = static_cast<size_t>(std::find(exec.begin(), exec.end(), gi) -
                                                     exec.begin());
            allocate(out, int(giPos), lastUse[giPos] + 1);
        }
    }

    opt.slotSize.resize(slots.size());
    for (size_t s = 0; s < slots.size(); ++s) opt.slotSize[s] = slots[s].capacity;
    for (size_t s = 0; s < slots.size(); ++s)
        if (slots[s].freeAt >= (1 << 30)) opt.persistentSlots.push_back(int(s));

    // drop absorbed (fused-away) groups from the final program
    std::vector<FusedGroup> finalGroups;
    std::vector<int> oldToNew(groups.size(), -1);
    for (size_t i = 0; i < groups.size(); ++i)
        if (!absorbed[i]) {
            oldToNew[i] = int(finalGroups.size());
            finalGroups.push_back(std::move(groups[i]));
        }
    opt.groups = std::move(finalGroups);
    opt.groupOrder.reserve(exec.size());
    for (size_t gi : exec) {
        const int mapped = oldToNew[gi];
        if (mapped >= 0) opt.groupOrder.push_back(mapped);
    }
    return opt;
}

int groupKernelCount(const OptimizedGraph &opt) {
    int count = 0;
    for (const auto &g : opt.groups)
        if (g.kind != GroupKind::Alias) ++count;
    return count;
}

}  // namespace eve::tensor
