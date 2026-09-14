#include <cmath>
#include <memory>
#include <vector>
#include "tensor/Graph.h"
#include "tensor/KernelGen.h"
#include "tensor/Optimizer.h"
#include "tensor/TF.h"
#include "tensor/Tensor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::tensor;

TEST_CASE("tensor.optimizer.residualAttentionDependenciesAndSlots") {
    auto                   *tf = TF::create();
    const int               T = 4, D = 8;
    std::unique_ptr<Tensor> W(tf->fill2(D, D, -0.1f));
    for (int i = 0; i < D; ++i) W->set2(i, i, 0.7f + float(i) * 0.1f);
    std::unique_ptr<Func> fn(tf->func());
    Tensor               *in = fn->input3(1, T, D);
    Tensor               *l  = tf->layernorm(in, 1e-5f);
    Tensor               *m  = tf->reshape2(l, T, D);
    Tensor               *mm = tf->matmul(m, W.get());
    Tensor               *q  = tf->reshape4(mm, 1, 1, T, D);
    Tensor               *a  = tf->sdpa(q, q, q, 0.353553f);
    Tensor               *a3 = tf->reshape3(a, 1, T, D);
    fn->setOutput(tf->add(in, a3));

    OptimizedGraph opt = optimizeGraph(fn->graph(), fn->outputNode());
    REQUIRE(!opt.order.empty());
    REQUIRE(opt.outputNode == fn->outputNode());
    REQUIRE(opt.nodeSlot.size() == size_t(fn->graph().nodeCount()));
    std::vector<bool> visited(size_t(fn->graph().nodeCount()), false);
    for (int id : opt.order) {
        REQUIRE(id >= 0);
        REQUIRE(id < fn->graph().nodeCount());
        REQUIRE(!visited[size_t(id)]);
        const auto &node = fn->graph().node(id);
        for (int input : {node.in0, node.in1, node.in2, node.in3, node.in4}) {
            if (input >= 0) REQUIRE(visited[size_t(input)]);
        }
        visited[size_t(id)] = true;
    }
    for (const auto &group : opt.groups) {
        REQUIRE(group.outputNode >= 0);
        const int slot = opt.nodeSlot[size_t(group.outputNode)];
        REQUIRE(slot >= 0);
        REQUIRE(size_t(slot) < opt.slotSize.size());
        REQUIRE(opt.slotSize[size_t(slot)] >= fn->graph().node(group.outputNode).size);
        if (group.kind == GroupKind::Elementwise) {
            KernelSpec spec;
            generateKernel(fn->graph(), group, spec);
            REQUIRE(!spec.pass2.empty());
        }
    }
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    std::unique_ptr<Tensor>           input(tf->zeros3(1, T, D));
    for (int i = 0; i < input->getSize(); ++i) input->set(i, float(i % 7) * 0.2f);
    std::unique_ptr<Tensor> normalized(tf->layernorm(input.get(), 1e-5f));
    std::unique_ptr<Tensor> matrix(tf->reshape2(normalized.get(), T, D));
    std::unique_ptr<Tensor> multiplied(tf->matmul(matrix.get(), W.get()));
    std::unique_ptr<Tensor> query(tf->reshape4(multiplied.get(), 1, 1, T, D));
    std::unique_ptr<Tensor> attended(tf->sdpa(query.get(), query.get(), query.get(), 0.353553f));
    std::unique_ptr<Tensor> reshaped(tf->reshape3(attended.get(), 1, T, D));
    std::unique_ptr<Tensor> expected(tf->add(input.get(), reshaped.get()));
    std::unique_ptr<Tensor> actual(compiled->run1(input.get()));
    REQUIRE(actual->getSize() == expected->getSize());
    for (int i = 0; i < actual->getSize(); ++i) REQUIRE(std::fabs(actual->get(i) - expected->get(i)) < 1e-4f);
}
