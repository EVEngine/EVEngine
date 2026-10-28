#include "agent/tensor/GpuGraph.h"

namespace eve::agent::detail {
namespace {
using tensor::OpType;
struct Builder {
    tensor::Graph g;
    int           node(OpType op, int rows, int cols, int a = -1, int b = -1) {
        tensor::GraphNode n;
        n.type    = op;
        n.rank    = 2;
        n.dims[0] = rows;
        n.dims[1] = cols;
        n.in0     = a;
        n.in1     = b;
        return g.addNode(std::move(n));
    }
    int input(int slot, int rows, int cols) {
        int id                     = node(OpType::Placeholder, rows, cols);
        g.node(id).placeholderSlot = slot;
        return id;
    }
    int unary(OpType op, int a, float s0 = 0, float s1 = 0) {
        auto shape    = g.node(a);
        int  id       = node(op, shape.dims[0], shape.dims[1], a);
        g.node(id).s0 = s0;
        g.node(id).s1 = s1;
        return id;
    }
    int binary(OpType op, int a, int b) {
        auto shape = g.node(a);
        return node(op, shape.dims[0], shape.dims[1], a, b);
    }
    int transpose(int a) {
        auto s              = g.node(a);
        int  id             = node(OpType::Permute, s.dims[1], s.dims[0], a);
        g.node(id).permRank = 2;
        g.node(id).perm[0]  = 1;
        g.node(id).perm[1]  = 0;
        return id;
    }
    int matmul(int a, int b) { return node(OpType::MatMul, g.node(a).dims[0], g.node(b).dims[1], a, b); }
    int slice(int a, int axis, int begin, int end) {
        auto s        = g.node(a);
        s.dims[axis]  = end - begin;
        int id        = node(OpType::Slice, s.dims[0], s.dims[1], a);
        g.node(id).i0 = axis;
        g.node(id).i1 = begin;
        g.node(id).i2 = end;
        return id;
    }
    int concat(int a, int b) {
        int id        = node(OpType::Concat, g.node(a).dims[0], g.node(a).dims[1] + g.node(b).dims[1], a, b);
        g.node(id).i0 = 1;
        g.node(id).i1 = 2;
        return id;
    }
    int biasInput(int a) {
        int one               = node(OpType::Const, 1, 1);
        g.node(one).constData = {1};
        return concat(a, one);
    }
};
}  // namespace
PolicyGraph makePolicyGraph(int f, int h, int a, bool training) {
    Builder   b;
    const int n1 = h * (f + 1), n2 = h * (h + 1), n3 = a * (h + 1);
    const int weights = b.input(0, 1, n1 + n2 + n3);
    const int x = b.input(1, 1, f), mask = b.input(2, 1, a);
    auto      matrix = [&](int start, int count, int rows, int cols) {
        int flat = b.slice(weights, 1, start, start + count);
        return b.node(OpType::Reshape, rows, cols, flat);
    };
    const int w1 = matrix(0, n1, h, f + 1), w2 = matrix(n1, n2, h, h + 1), w3 = matrix(n1 + n2, n3, a, h + 1);
    const int xb        = b.biasInput(x);
    const int h1        = b.unary(OpType::Tanh, b.matmul(xb, b.transpose(w1)));
    const int h1b       = b.biasInput(h1);
    const int h2        = b.unary(OpType::Tanh, b.matmul(h1b, b.transpose(w2)));
    const int h2b       = b.biasInput(h2);
    const int logits    = b.binary(OpType::Add, b.matmul(h2b, b.transpose(w3)), mask);
    int       output    = b.unary(OpType::Softmax, logits);
    b.g.node(output).i0 = 1;
    if (training) {
        const int target = b.input(3, 1, a), rate = b.input(4, 1, 1);
        const int d3    = b.binary(OpType::Sub, output, target);
        auto      delta = [&](int d, int w, int activation) {
            int propagated = b.matmul(d, b.slice(w, 1, 0, h));
            int derivative =
                b.unary(OpType::AddScalar, b.unary(OpType::Neg, b.binary(OpType::Multiply, activation, activation)), 1);
            return b.binary(OpType::Multiply, propagated, derivative);
        };
        const int d2 = delta(d3, w3, h2), d1 = delta(d2, w2, h1);
        auto      update = [&](int w, int d, int input, int count) {
            int gradient = b.unary(OpType::Clamp, b.matmul(b.transpose(d), input), -1, 1);
            int updated  = b.binary(OpType::Sub, w, b.binary(OpType::Multiply, gradient, rate));
            return b.node(OpType::Reshape, 1, count, updated);
        };
        int u1 = update(w1, d1, xb, n1), u2 = update(w2, d2, h1b, n2), u3 = update(w3, d3, h2b, n3);
        output = b.concat(b.concat(u1, u2), u3);
    }
    return {std::move(b.g), output};
}
}  // namespace eve::agent::detail
