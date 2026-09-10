// Emit current production tensor kernels for an installed engine's real Gpgpu.
// This validates GPU mathematics, not current module boot/registration.
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include "agent/Learning.h"
#include "agent/tensor/GpuGraph.h"
#include "tensor/KernelGen.h"
#include "tensor/Optimizer.h"

using namespace eve;
static std::string quote(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else {
            if (c == '\\' || c == '\"') out += '\\';
            out += c;
        }
    }
    return out + "\"";
}
static void emit(std::ostream& out, agent::Policy p, agent::Observation o, bool training, int test) {
    auto               recipe = agent::detail::makePolicyGraph(p.featureCount, p.hiddenWidth, p.actionCount, training);
    auto               opt    = tensor::optimizeGraph(recipe.graph, recipe.output);
    std::vector<float> weights;
    weights.reserve(p.weights.size());
    for (double weight : p.weights) weights.push_back(static_cast<float>(weight));
    std::vector<std::vector<float>> feeds{std::move(weights),
                                          o.features,
                                          std::vector<float>(p.actionCount, -1e38f),
                                          std::vector<float>(p.actionCount, 0),
                                          {0.03f}};
    for (auto a : o.legalActions) feeds[2][a] = 0;
    feeds[3][o.legalActions.back()] = 1;
    auto expected                   = agent::detail::forward(p, o);
    if (training) {
        agent::detail::train(p, o, o.legalActions.back(), 0.03);
        expected = p.weights;
    }
    out << "{ local b = []; local shaders = [];\n";
    for (auto size : opt.slotSize) out << "b.append(gpgpu.newBuffer(" << size * 4 << ",\"storage\"));\n";
    for (int id = 0; id < recipe.graph.nodeCount(); ++id) {
        auto& node = recipe.graph.node(id);
        int   slot = opt.nodeSlot[id];
        if (slot < 0) continue;
        const std::vector<float>* values = nullptr;
        if (node.type == tensor::OpType::Placeholder) values = &feeds[node.placeholderSlot];
        if (node.type == tensor::OpType::Const) values = &node.constData;
        if (values)
            for (std::size_t i = 0; i < values->size(); ++i)
                out << "b[" << slot << "].writeFloat32(" << i << "," << (*values)[i] << ");\n";
    }
    for (int gi : opt.groupOrder) {
        auto& group = opt.groups[gi];
        if (group.kind == tensor::GroupKind::Alias) continue;
        tensor::KernelSpec spec;
        if (!tensor::generateKernel(recipe.graph, group, spec)) throw std::runtime_error("Unsupported GPU graph group");
        out << "{ local stats = [];\n";
        for (int i = 0; i < spec.statsCount; ++i)
            out << "stats.append(gpgpu.newBuffer(" << spec.statsSize * 4 << ",\"storage\"));\n";
        auto pass = [&](const std::string& source, bool first) {
            out << "{ local s = gpgpu.newShader(" << quote(source) << "); shaders.append(s);\n";
            int inputs = first ? spec.inputsReadPass1 : spec.inputCount;
            for (int i = 0; i < inputs; ++i)
                out << "s.bindBuffer(" << i << ",b[" << opt.nodeSlot[group.inputs[i]] << "]);\n";
            for (int i = 0; i < spec.statsCount; ++i) out << "s.bindBuffer(" << inputs + i << ",stats[" << i << "]);\n";
            if (!first)
                out << "s.bindBuffer(" << inputs + spec.statsCount << ",b[" << opt.nodeSlot[group.outputNode]
                    << "]);\n";
            out << "gpgpu.dispatch(s," << (first ? spec.groupsX1 : spec.groupsX2) << ","
                << (first ? spec.groupsY1 : spec.groupsY2) << "," << (first ? spec.groupsZ1 : spec.groupsZ2)
                << "); }\n";
        };
        if (spec.twoPass) pass(spec.pass1, true);
        pass(spec.pass2, false);
        out << "}\n";
    }
    out << "local maxError = 0.0;\n";
    for (std::size_t i = 0; i < expected.size(); ++i) {
        out << "{ local actual = b[" << opt.nodeSlot[recipe.output] << "].readFloat32(" << i
            << "); local error = fabs(actual-(" << expected[i]
            << ")); if (actual != actual || error > 0.00003) throw \"GPU parity failed case " << test << " index " << i
            << " actual=\"+actual+\" error=\"+error; if (error > maxError) maxError=error; }\n";
    }
    out << "print(\"AGENT_GPU_PASS case=" << test << " training=" << training << " maxError=\"+maxError+\"\\n\"); }\n";
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        std::ofstream out(argv[1]);
        if (!out) return 2;
        out << std::scientific << std::setprecision(9);
        out << "eve_init = function() { if(!gpgpu.isAvailable()) throw \"GPU unavailable\";\n";
        int test = 0;
        for (auto shape : {4, 16, 64}) {
            agent::Config c;
            c.featureCount       = 3;
            c.hiddenWidth        = shape;
            c.actionCount        = 5;
            auto               p = agent::detail::makePolicy(c);
            agent::Observation o;
            o.features     = {0.4f, -0.2f, 0.8f};
            o.legalActions = {0, 2, 4};
            emit(out, p, o, false, ++test);
            emit(out, p, o, true, ++test);
        }
        out << "print(\"AGENT_GPU_ALL_PASS\\n\"); };\n";
        std::cout << "Generated 6 GPU numerical checks\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
