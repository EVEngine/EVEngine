#include "tensor/GpuBackend.h"
#include "tensor/Graph.h"
#include "tensor/KernelGen.h"
#include "tensor/Optimizer.h"

#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"

#include "common/Exception.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <vector>

namespace eve::tensor {
namespace {

constexpr int kLocalSize = 256;
constexpr int kAutotuneIters = 5;

const char *kReduceGlsl = R"(#version 450
layout(local_size_x = 256) in;
layout(set = 0, binding = 0) readonly buffer In { float a[]; };
layout(set = 0, binding = 1) writeonly buffer Out { float partial[]; };
layout(push_constant) uniform PC { float data[32]; } pc;
shared float sdata[256];
void main() {
    uint tid = gl_LocalInvocationID.x;
    uint gid = gl_GlobalInvocationID.x;
    uint size = uint(pc.data[0] + 0.5);
    int op = int(pc.data[1] + 0.5);
    float ident = (op == 0) ? 0.0 : (op == 1 ? 3.402823e38 : -3.402823e38);
    sdata[tid] = (gid < size) ? a[gid] : ident;
    barrier();
    for (uint s = 128u; s > 0u; s >>= 1u) {
        if (tid < s) {
            if (op == 0) sdata[tid] += sdata[tid + s];
            else if (op == 1) sdata[tid] = min(sdata[tid], sdata[tid + s]);
            else sdata[tid] = max(sdata[tid], sdata[tid + s]);
        }
        barrier();
    }
    if (tid == 0u) partial[gl_WorkGroupID.x] = sdata[0];
}
)";

struct ReduceKernels {
    gpgpu::Gpgpu *gpgpu = nullptr;
    gpgpu::ComputeShader *reduce = nullptr;
};

/** Lazily created, process-lifetime singleton. Returns nullptr if unavailable. */
ReduceKernels *getReduceKernels() {
    static ReduceKernels *kernels = nullptr;
    static bool failed = false;
    if (kernels) return kernels;
    if (failed) return nullptr;
    try {
        auto *gp = gpgpu::Gpgpu::create();
        if (!gp || !gp->isAvailable()) {
            failed = true;
            return nullptr;
        }
        auto *k = new ReduceKernels();
        k->gpgpu = gp;
        k->reduce = gp->newShader(kReduceGlsl);
        kernels = k;
        return kernels;
    } catch (...) {
        failed = true;
        return nullptr;
    }
}

int groupsFor(int count) { return (count + kLocalSize - 1) / kLocalSize; }

/** Bind a single-pass kernel to its real input/output arena buffers. */
void bindKernel(gpgpu::ComputeShader *shader, const KernelSpec &spec,
                const std::vector<gpgpu::GpuBuffer *> &inputs, gpgpu::GpuBuffer *output) {
    for (int i = 0; i < spec.inputCount; ++i)
        shader->bindBuffer(i, inputs[static_cast<size_t>(i)]);
    shader->bindBuffer(spec.inputCount, output);
}

double timeDispatch(gpgpu::Gpgpu *gp, gpgpu::ComputeShader *shader, int gx, int gy) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kAutotuneIters; ++i) gp->dispatch(shader, gx, gy, 1);
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count() / kAutotuneIters;
}

}  // namespace

struct GpuProgram::Impl {
    struct GroupRuntime {
        KernelSpec spec;
        std::unique_ptr<gpgpu::ComputeShader> pass1;
        std::unique_ptr<gpgpu::ComputeShader> pass2;
        std::vector<gpgpu::GpuBuffer *> inputs;   // one per group input node
        std::vector<gpgpu::GpuBuffer *> stats;    // statsCount working buffers
        gpgpu::GpuBuffer *qScales = nullptr;      // per-group scales (int8/int4)
        gpgpu::GpuBuffer *output = nullptr;
    };

    gpgpu::Gpgpu *gpgpu = nullptr;
    std::vector<GroupRuntime> groups;
    std::vector<gpgpu::GpuBuffer *> slotBuffer;   // arena slot -> buffer
    std::vector<gpgpu::GpuBuffer *> placeholderBuffers;
    std::vector<int> placeholderSizes;
    std::vector<gpgpu::GpuBuffer *> ownedBuffers;  // every allocated buffer (cleanup)
    std::map<int, gpgpu::GpuBuffer *> qScalesByNode;  // quantized const node -> scales
    gpgpu::GpuBuffer *outputBuffer = nullptr;
    int outputSize = 0;
    std::unique_ptr<gpgpu::Sequence> sequence;
    std::unique_ptr<gpgpu::GpuBuffer> outputStaging;

    gpgpu::GpuBuffer *alloc(int byteSize) {
        auto *buf = gpgpu->newBuffer(byteSize, "storage");
        ownedBuffers.push_back(buf);
        return buf;
    }

    /** Bind real arena buffers once at build time; bindings persist across runs. */
    void bindGroup(const GroupRuntime &g) const {
        if (g.spec.twoPass) {
            auto *p1 = g.pass1.get();
            for (int i = 0; i < g.spec.inputsReadPass1; ++i)
                p1->bindBuffer(i, g.inputs[static_cast<size_t>(i)]);
            for (int s = 0; s < g.spec.statsCount; ++s)
                p1->bindBuffer(g.spec.inputsReadPass1 + s, g.stats[static_cast<size_t>(s)]);
        }
        auto *p2 = g.pass2.get();
        for (int i = 0; i < g.spec.inputCount; ++i)
            p2->bindBuffer(i, g.inputs[static_cast<size_t>(i)]);
        if (g.spec.scalesBinding >= 0 && g.qScales)
            p2->bindBuffer(g.spec.scalesBinding, g.qScales);
        const int outBinding = g.spec.outputBinding >= 0 ? g.spec.outputBinding
                                                         : g.spec.inputCount;
        if (g.spec.twoPass) {
            for (int s = 0; s < g.spec.statsCount; ++s)
                p2->bindBuffer(g.spec.inputCount + s, g.stats[static_cast<size_t>(s)]);
            p2->bindBuffer(g.spec.inputCount + g.spec.statsCount, g.output);
        } else {
            p2->bindBuffer(outBinding, g.output);
        }
    }

    /** Record all dispatches of one fused group into the shared sequence. */
    void recordGroup(const GroupRuntime &g, gpgpu::Sequence *seq) const {
        if (g.spec.twoPass)
            seq->recordDispatch(g.pass1.get(), g.spec.groupsX1, g.spec.groupsY1,
                                g.spec.groupsZ1);
        seq->recordDispatch(g.pass2.get(), g.spec.groupsX2, g.spec.groupsY2,
                            g.spec.groupsZ2);
    }
};

GpuProgram::GpuProgram() : impl_(new Impl()) {}
GpuProgram::~GpuProgram() { delete impl_; }

GpuProgram *GpuProgram::tryBuild(const Graph &graph, const OptimizedGraph &opt, int outputNode) {
    auto *gp = gpgpu::Gpgpu::create();
    if (!gp || !gp->isAvailable()) return nullptr;
    if (outputNode < 0 || outputNode >= graph.nodeCount()) return nullptr;

    auto *prog = new GpuProgram();
    prog->impl_->gpgpu = gp;
    auto &impl = *prog->impl_;

    try {
        // arena buffers: one per memory-plan slot
        impl.slotBuffer.resize(opt.slotSize.size(), nullptr);
        for (size_t s = 0; s < opt.slotSize.size(); ++s)
            impl.slotBuffer[s] = impl.alloc(opt.slotSize[s] * int(sizeof(float)));

        impl.sequence.reset(gp->newSequence());

        // placeholder / const uploads
        impl.placeholderBuffers.clear();
        impl.placeholderSizes.clear();
        for (int id = 0; id < graph.nodeCount(); ++id) {
            const auto &nd = graph.node(id);
            if (opt.nodeSlot[static_cast<size_t>(id)] < 0) continue;
            auto *buf = impl.slotBuffer[static_cast<size_t>(opt.nodeSlot[static_cast<size_t>(id)])];
            if (nd.type == OpType::Placeholder) {
                const int slot = nd.placeholderSlot;
                if (slot < 0) throw eve::Exception("GpuProgram: bad placeholder slot");
                if (int(impl.placeholderBuffers.size()) <= slot) {
                    impl.placeholderBuffers.resize(size_t(slot) + 1, nullptr);
                    impl.placeholderSizes.resize(size_t(slot) + 1, 0);
                }
                impl.placeholderBuffers[static_cast<size_t>(slot)] = buf;
                impl.placeholderSizes[static_cast<size_t>(slot)] = nd.size;
            } else if (nd.type == OpType::Const) {
                if (!nd.constBytes.empty()) {
                    buf->uploadBytes(nd.constBytes.data(), nd.constBytes.size());
                    if (!nd.constScales.empty()) {
                        auto *sb = impl.alloc(int(nd.constScales.size()) * int(sizeof(float)));
                        sb->uploadBytes(nd.constScales.data(),
                                        sizeof(float) * nd.constScales.size());
                        impl.qScalesByNode[id] = sb;
                    }
                } else {
                    buf->uploadBytes(nd.constData.data(), sizeof(float) * size_t(nd.size));
                }
            }
        }

        // per-group kernels (topological execution order)
        for (int gi : opt.groupOrder) {
            const auto &grp = opt.groups[static_cast<size_t>(gi)];
            if (grp.kind == GroupKind::Alias) continue;  // pure buffer alias

            Impl::GroupRuntime rt;
            for (int input : grp.inputs) {
                const int slot = opt.nodeSlot[static_cast<size_t>(input)];
                if (slot < 0) throw eve::Exception("GpuProgram: input without slot");
                rt.inputs.push_back(impl.slotBuffer[static_cast<size_t>(slot)]);
                const GraphNode &inN = graph.node(input);
                if (!inN.constBytes.empty() && !inN.constScales.empty()) {
                    auto it = impl.qScalesByNode.find(input);
                    if (it != impl.qScalesByNode.end()) rt.qScales = it->second;
                }
            }
            const int outSlot = opt.nodeSlot[static_cast<size_t>(grp.outputNode)];
            if (outSlot < 0) throw eve::Exception("GpuProgram: output without slot");
            rt.output = impl.slotBuffer[static_cast<size_t>(outSlot)];

            KernelSpec spec;
            if (grp.kind == GroupKind::MatMul) {
                const GraphNode &mm = graph.node(grp.nodes.front());
                KernelSpec naive;
                if (!generateMatMulVariant(graph, grp, false, naive))
                    throw eve::Exception("GpuProgram: matmul codegen failed");
                if (mm.rank == 2) {
                    KernelSpec tiled;
                    std::unique_ptr<gpgpu::ComputeShader> naiveShader, tiledShader;
                    if (generateMatMulVariant(graph, grp, true, tiled)) {
                        naiveShader.reset(gp->newShader(naive.pass2));
                        tiledShader.reset(gp->newShader(tiled.pass2));
                        try {
                            // Time with the real arena buffers bound: dispatching
                            // unbound kernels on the 4-byte dummy SSBO faults.
                            bindKernel(naiveShader.get(), naive, rt.inputs, rt.output);
                            const double tNaive =
                                timeDispatch(gp, naiveShader.get(), naive.groupsX2,
                                             naive.groupsY2);
                            bindKernel(tiledShader.get(), tiled, rt.inputs, rt.output);
                            const double tTiled =
                                timeDispatch(gp, tiledShader.get(), tiled.groupsX2,
                                             tiled.groupsY2);
                            if (tTiled < tNaive) {
                                spec = tiled;
                                rt.pass2 = std::move(tiledShader);
                            } else {
                                spec = naive;
                                rt.pass2 = std::move(naiveShader);
                            }
                        } catch (...) {
                            spec = naive;
                            rt.pass2 = std::move(naiveShader);
                        }
                    } else {
                        spec = naive;
                        rt.pass2.reset(gp->newShader(naive.pass2));
                    }
                } else {
                    spec = naive;
                    rt.pass2.reset(gp->newShader(naive.pass2));
                }
            } else {
                if (!generateKernel(graph, grp, spec))
                    throw eve::Exception("GpuProgram: kernel codegen failed");
                rt.pass2.reset(gp->newShader(spec.pass2));
                if (spec.twoPass) rt.pass1.reset(gp->newShader(spec.pass1));
            }
            rt.spec = spec;

            for (int s = 0; s < spec.statsCount; ++s)
                rt.stats.push_back(impl.alloc(spec.statsSize * int(sizeof(float))));
            impl.bindGroup(rt);
            impl.groups.push_back(std::move(rt));
        }

        // final output
        const int outSlot = opt.nodeSlot[static_cast<size_t>(outputNode)];
        if (outSlot < 0) throw eve::Exception("GpuProgram: final output without slot");
        impl.outputBuffer = impl.slotBuffer[static_cast<size_t>(outSlot)];
        impl.outputSize = graph.node(outputNode).size;
        impl.outputStaging.reset(
            gp->newBuffer(impl.outputSize * int(sizeof(float)), "staging"));
        return prog;
    } catch (const std::exception &e) {
        fprintf(stderr, "[tensor] GpuProgram::tryBuild failed: %s\n", e.what());
        delete prog;
        return nullptr;
    } catch (...) {
        delete prog;
        return nullptr;
    }
}

std::vector<float> GpuProgram::run(const std::vector<const float *> &feeds) const {
    gpgpu::Sequence *seq = impl_->sequence.get();
    seq->begin();
    for (size_t i = 0; i < feeds.size(); ++i) {
        if (i >= impl_->placeholderBuffers.size() || !impl_->placeholderBuffers[i]) continue;
        seq->recordUpload(impl_->placeholderBuffers[i], feeds[i],
                          sizeof(float) * size_t(impl_->placeholderSizes[i]));
    }
    for (const auto &g : impl_->groups) impl_->recordGroup(g, seq);
    const uint64_t outBytes = sizeof(float) * size_t(impl_->outputSize);
    seq->recordDownload(impl_->outputBuffer, impl_->outputStaging.get(), outBytes);
    seq->submit();

    std::vector<float> out(static_cast<size_t>(impl_->outputSize));
    impl_->outputStaging->downloadBytes(out.data(), outBytes);
    return out;
}

bool gpuReduce(const float *data, int size, int op, float &outResult) {
    if (!data || size <= 0) return false;
    ReduceKernels *kernels = getReduceKernels();
    if (!kernels) return false;
    try {
        std::unique_ptr<gpgpu::GpuBuffer> in(
            kernels->gpgpu->newBuffer(size * int(sizeof(float)), "storage"));
        in->uploadBytes(data, sizeof(float) * size_t(size));

        const int groups = groupsFor(size);
        std::unique_ptr<gpgpu::GpuBuffer> partial(
            kernels->gpgpu->newBuffer(groups * int(sizeof(float)), "storage"));

        kernels->reduce->bindBuffer(0, in.get());
        kernels->reduce->bindBuffer(1, partial.get());
        kernels->reduce->setFloat(0, float(size));
        kernels->reduce->setFloat(1, float(op));
        kernels->gpgpu->dispatch(kernels->reduce, groups);

        std::vector<float> parts(static_cast<size_t>(groups));
        partial->downloadBytes(parts.data(), sizeof(float) * size_t(groups));

        float acc = op == 0 ? 0.f
                            : (op == 1 ? std::numeric_limits<float>::max()
                                       : -std::numeric_limits<float>::max());
        for (float v : parts) {
            if (op == 0) acc += v;
            else if (op == 1) acc = std::min(acc, v);
            else acc = std::max(acc, v);
        }
        outResult = acc;
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace eve::tensor
