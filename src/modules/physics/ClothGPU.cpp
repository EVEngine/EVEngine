#include "physics/ClothGPU.h"

#include "common/Exception.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace eve::physics {

using eve::graphics::Color;
using eve::gpgpu::GpuBuffer;

namespace {

// One thread owns one particle: integrate, then relax its fixed-size link list.
// Push constant layout (float indices):
//   0 dt | 1 gx | 2 gy | 3 stiffness | 4 iterations | 5 count | 6 damping
//   7 boundX | 8 boundY | 9 boundW | 10 boundH | 11 hasBounds
//   12 forceX | 13 forceY | 14 interactX | 15 interactY
//   16 interactRadius | 17 interactStrength | 18 links-per-particle
//   19 mode (0 = integrate, 1 = constraints + bounds, 2 = build hash,
//            3 = self-collision, 4 = clear hash counters)
//   20 self-collision min distance | 21 self-collision on/off
//   22 hash nx | 23 hash ny | 24 cell count
const char *kClothKernel = R"glsl(#version 450
layout(local_size_x = 64) in;

layout(set = 0, binding = 0) buffer Pos {
    vec4 posIn[]; // x, y, prevX, prevY (read)
};
layout(set = 0, binding = 1) buffer PosOut {
    vec4 posOut[]; // x, y, prevX, prevY (write)
};
layout(set = 0, binding = 2) buffer Links {
    vec4 links[]; // otherIndex, restLen, 0, 0 per particle slot
};
layout(set = 0, binding = 3) buffer Flags {
    vec4 flags[]; // x = 1.0 when pinned
};
layout(set = 0, binding = 4) buffer CellHead {
    uint cellHead[]; // per-cell linked-list head; 0xFFFFFFFF = empty
};
layout(set = 0, binding = 5) buffer CellNext {
    uint cellNext[]; // slot -> next slot in the same cell
};
layout(set = 0, binding = 6) buffer CellItems {
    uint cellItems[]; // slot -> particle index
};
layout(set = 0, binding = 7) buffer CellSlot {
    uint cellSlot[1]; // global slot counter for hash building
};

layout(push_constant) uniform PC {
    float data[32];
} pc;

// Keep two non-adjacent particles at least minDist apart. Pinned endpoints are
// handled by their own thread being skipped: the free side takes the full
// correction; two free sides split it.
void separatePair(uint i, uint j, inout vec4 p) {
    if (j == i) return;
    const uint K = uint(pc.data[18]);
    bool linked = false;
    for (uint k = 0u; k < K; ++k) {
        vec4 l = links[i * K + k];
        if (int(l.x) < 0) break;
        if (int(l.x) == int(j)) {
            linked = true;
            break;
        }
    }
    if (linked) return;
    bool jPinned = flags[j].x > 0.5;
    vec4 q = posIn[j];
    float dx = q.x - p.x;
    float dy = q.y - p.y;
    float d2 = dx * dx + dy * dy;
    float minDist = pc.data[20];
    if (d2 >= minDist * minDist || d2 < 1e-8) return;
    float d = sqrt(d2);
    float corr = min(2.0, (minDist - d) / d);
    float w = jPinned ? 1.0 : 0.5;
    p.x -= dx * corr * w;
    p.y -= dy * corr * w;
}

void main() {
    // Reset the hash: every cell head becomes empty, the global slot counter
    // goes back to zero. Runs once per substep before building.
    if (int(pc.data[19]) == 4) {
        uint cid = gl_GlobalInvocationID.x;
        if (cid < uint(pc.data[24])) cellHead[cid] = 0xFFFFFFFFu;
        if (cid == 0u) cellSlot[0] = 0u;
        return;
    }

    uint i = gl_GlobalInvocationID.x;
    uint n = uint(pc.data[5]);
    if (i >= n) return;

    const float dt = pc.data[0];
    const float damp = 1.0 - pc.data[6];
    const bool pinned = flags[i].x > 0.5;
    const uint K = uint(pc.data[18]);
    const int mode = int(pc.data[19]);
    vec4 p = posIn[i];

    if (mode == 0 && !pinned) {
        // Verlet integrate.
        float vx = (p.x - p.z) * damp;
        float vy = (p.y - p.w) * damp;
        p.z = p.x;
        p.w = p.y;
        float ax = pc.data[1] + pc.data[12];
        float ay = pc.data[2] + pc.data[13];
        p.x += vx + ax * dt * dt;
        p.y += vy + ay * dt * dt;

        // Pointer field (Fluid2D-style attract/repel).
        float ir = pc.data[16];
        if (ir > 0.0 && abs(pc.data[17]) > 1e-9) {
            float dx = pc.data[14] - p.x;
            float dy = pc.data[15] - p.y;
            float r2 = dx * dx + dy * dy;
            if (r2 < ir * ir && r2 > 1e-6) {
                float r = sqrt(r2);
                float w = 1.0 - r / ir;
                p.x += (dx / r) * pc.data[17] * w * dt * dt;
                p.y += (dy / r) * pc.data[17] * w * dt * dt;
            }
        }
    }

    // Distance-constraint relaxation over this particle's link slots. The
    // owning thread never moves a pinned particle: a pinned endpoint leaves
    // the full correction to the free partner's thread. Runs in a separate
    // dispatch so integration results are visible before solving.
    if (mode == 1 && !pinned) {
        // One Jacobi pass per dispatch: each thread reads the previous pass's
        // positions (posIn) and writes its own corrected position (posOut).
        // The outer loop in update() repeats the dispatch to converge.
        for (uint k = 0u; k < K; ++k) {
            vec4 l = links[i * K + k];
            int other = int(l.x);
            if (other < 0) break;
            vec4 q = posIn[uint(other)];
            bool otherPinned = flags[uint(other)].x > 0.5;
            float dx = q.x - p.x;
            float dy = q.y - p.y;
            float dist = sqrt(dx * dx + dy * dy);
            if (dist < 1e-5) continue;
            float diff = (dist - l.y) / dist * pc.data[3];
            if (otherPinned) {
                p.x += dx * diff;
                p.y += dy * diff;
            } else {
                p.x += dx * diff * 0.5;
                p.y += dy * diff * 0.5;
            }
        }
    }

    // Build the spatial hash: each particle atomically claims a global slot and
    // prepends it to its cell's linked list (overflow-free).
    if (mode == 2 && pc.data[21] > 0.5) {
        int nx = int(pc.data[22]);
        int ny = int(pc.data[23]);
        if (nx > 0 && ny > 0) {
            float cellSize = pc.data[20];
            float bx = pc.data[7];
            float by = pc.data[8];
            int cx = clamp(int(floor((p.x - bx) / cellSize)), 0, nx - 1);
            int cy = clamp(int(floor((p.y - by) / cellSize)), 0, ny - 1);
            uint cid = uint(cy * nx + cx);
            uint slot = atomicAdd(cellSlot[0], 1u);
            if (slot >= n) return;
            cellNext[slot] = atomicExchange(cellHead[cid], slot);
            cellItems[slot] = i;
        }
    }

    // Particle-level self-collision via the hash (3x3 neighbor cells) or an
    // O(n) scan fallback when no bounds define a hash grid.
    if (mode == 3 && pc.data[21] > 0.5 && !pinned) {
        int nx = int(pc.data[22]);
        int ny = int(pc.data[23]);
        if (nx > 0 && ny > 0 && pc.data[20] > 0.0) {
            float cellSize = pc.data[20];
            float bx = pc.data[7];
            float by = pc.data[8];
            int cx = clamp(int(floor((p.x - bx) / cellSize)), 0, nx - 1);
            int cy = clamp(int(floor((p.y - by) / cellSize)), 0, ny - 1);
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    int ncx = clamp(cx + ox, 0, nx - 1);
                    int ncy = clamp(cy + oy, 0, ny - 1);
                    uint cid = uint(ncy * nx + ncx);
                    uint s = cellHead[cid];
                    while (s != 0xFFFFFFFFu) {
                        separatePair(i, cellItems[s], p);
                        s = cellNext[s];
                    }
                }
            }
        } else {
            for (uint j = 0u; j < n; ++j) separatePair(i, j, p);
        }
    }

    // Axis-aligned bounds with a soft bounce (mirrors the CPU cloth).
    if (mode == 1 && pc.data[11] > 0.5 && !pinned) {
        float bx = pc.data[7];
        float by = pc.data[8];
        float bw = pc.data[9];
        float bh = pc.data[10];
        if (p.x < bx) {
            float vx = p.x - p.z;
            p.x = bx;
            p.z = p.x + vx * 0.35;
        } else if (p.x > bx + bw) {
            float vx = p.x - p.z;
            p.x = bx + bw;
            p.z = p.x + vx * 0.35;
        }
        if (p.y < by) {
            float vy = p.y - p.w;
            p.y = by;
            p.w = p.y + vy * 0.35;
        } else if (p.y > by + bh) {
            float vy = p.y - p.w;
            p.y = by + bh;
            p.w = p.y + vy * 0.35;
        }
    }

    posOut[i] = p;
}
)glsl";

}  // namespace

ClothGPU::ClothGPU(eve::gpgpu::Gpgpu *gpgpu, int cols, int rows, float spacing, float originX,
                   float originY)
    : gpgpu_(gpgpu), cols_(cols), rows_(rows), spacing_(spacing), originX_(originX),
      originY_(originY) {
    if (!gpgpu_) throw Exception("ClothGPU: Gpgpu module required");
    if (cols_ < 2 || rows_ < 2) throw Exception("ClothGPU: cols and rows must be >= 2");
    if (spacing_ <= 0.f) throw Exception("ClothGPU: spacing must be > 0");

    const int count = getParticleCount();
    posCpu_.assign(static_cast<size_t>(count) * 4, 0.f);
    flagsCpu_.assign(static_cast<size_t>(count), 0.f);
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            const size_t i = static_cast<size_t>(r * cols_ + c);
            posCpu_[i * 4 + 0] = originX_ + float(c) * spacing_;
            posCpu_[i * 4 + 1] = originY_ + float(r) * spacing_;
            posCpu_[i * 4 + 2] = posCpu_[i * 4 + 0];
            posCpu_[i * 4 + 3] = posCpu_[i * 4 + 1];
        }
    }
    rebuildLinks();
    // Pin the top row in the CPU flag mirror only; buffers are created next and
    // uploadInitialState() uploads the flags together with positions/links.
    for (int c = 0; c < cols_; ++c) flagsCpu_[static_cast<size_t>(c)] = 1.f;

    posBuf_ = gpgpu_->newBuffer(count * int(4 * sizeof(float)), "storage");
    posBufB_ = gpgpu_->newBuffer(count * int(4 * sizeof(float)), "storage");
    linkBuf_ = gpgpu_->newBuffer(count * kMaxLinksPerParticle * int(4 * sizeof(float)),
                                 "storage");
    flagBuf_ = gpgpu_->newBuffer(count * int(4 * sizeof(float)), "storage");
    staging_ = gpgpu_->newBuffer(count * int(4 * sizeof(float)), "staging");
    if (!posBuf_ || !posBufB_ || !linkBuf_ || !flagBuf_ || !staging_)
        throw Exception("ClothGPU: failed to allocate storage buffers");
    uploadInitialState();

    shader_ = gpgpu_->newShader(kClothKernel);
    if (!shader_) throw Exception("ClothGPU: compute shader compile failed");
    seq_ = gpgpu_->newSequence();
    if (!seq_ || !seq_->isAvailable())
        throw Exception("ClothGPU: command sequence unavailable");
}

ClothGPU::~ClothGPU() { destroy(); }

void ClothGPU::destroy() {
    if (destroyed_) return;
    destroyed_ = true;
    delete seq_;
    seq_ = nullptr;
    delete shader_;
    shader_ = nullptr;
    delete posBuf_;
    posBuf_ = nullptr;
    delete posBufB_;
    posBufB_ = nullptr;
    delete linkBuf_;
    linkBuf_ = nullptr;
    delete flagBuf_;
    flagBuf_ = nullptr;
    delete staging_;
    staging_ = nullptr;
    delete cellHeadBuf_;
    cellHeadBuf_ = nullptr;
    delete cellNextBuf_;
    cellNextBuf_ = nullptr;
    delete cellItemsBuf_;
    cellItemsBuf_ = nullptr;
    delete cellSlotBuf_;
    cellSlotBuf_ = nullptr;
}

void ClothGPU::rebuildLinks() {
    links_.assign(static_cast<size_t>(getParticleCount()) * kMaxLinksPerParticle, Link{});
    linkCpu_.assign(static_cast<size_t>(getParticleCount()) * kMaxLinksPerParticle * 4, 0.f);

    const auto findFreeSlot = [&](int i) {
        for (int k = 0; k < kMaxLinksPerParticle; ++k) {
            Link &s = links_[static_cast<size_t>(i) * kMaxLinksPerParticle + k];
            if (s.other < 0) return k;
        }
        return kMaxLinksPerParticle - 1;
    };
    auto add = [&](int r, int c, int nr, int nc) {
        if (nr < 0 || nc < 0 || nr >= rows_ || nc >= cols_) return;
        const int i = r * cols_ + c;
        const int j = nr * cols_ + nc;
        Link &slot = links_[static_cast<size_t>(i) * kMaxLinksPerParticle +
                            findFreeSlot(i)];
        slot.other = j;
        const float dx = (float(nc) - float(c)) * spacing_;
        const float dy = (float(nr) - float(r)) * spacing_;
        slot.rest = std::sqrt(dx * dx + dy * dy);
    };

    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            add(r, c, r, c + 1);  // right
            add(r, c, r, c - 1);  // left
            add(r, c, r + 1, c);  // down
            add(r, c, r - 1, c);  // up
            add(r, c, r + 1, c + 1);
            add(r, c, r + 1, c - 1);
            add(r, c, r - 1, c + 1);
            add(r, c, r - 1, c - 1);
            add(r, c, r, c + 2);  // bend
            add(r, c, r, c - 2);
            add(r, c, r + 2, c);
            add(r, c, r - 2, c);
        }
    }

    for (size_t i = 0; i < links_.size(); ++i) {
        const Link &link = links_[i];
        linkCpu_[i * 4 + 0] = float(link.other);
        linkCpu_[i * 4 + 1] = link.rest;
        linkCpu_[i * 4 + 2] = 0.f;
        linkCpu_[i * 4 + 3] = 0.f;
    }
}

void ClothGPU::uploadInitialState() {
    const int count = getParticleCount();
    posBuf_->writeFloat32s(posCpu_.data(), count * 4, 0);
    posBufB_->writeFloat32s(posCpu_.data(), count * 4, 0);
    linkBuf_->writeFloat32s(linkCpu_.data(), count * kMaxLinksPerParticle * 4, 0);
    uploadPinned();
}

void ClothGPU::uploadPinned() {
    const int count = getParticleCount();
    std::vector<float> buf(static_cast<size_t>(count) * 4, 0.f);
    for (int i = 0; i < count; ++i) buf[static_cast<size_t>(i) * 4] = flagsCpu_[static_cast<size_t>(i)];
    flagBuf_->writeFloat32s(buf.data(), count * 4, 0);
}

void ClothGPU::ensureHashBuffers() {
    const float minDist = std::max(1e-3f, particleSize_ * 2.f);
    if (!selfCollision_ || !hasBounds_) {
        if (hashNx_ != 0 || hashNy_ != 0) {
            delete cellHeadBuf_;
            cellHeadBuf_ = nullptr;
            delete cellNextBuf_;
            cellNextBuf_ = nullptr;
            delete cellItemsBuf_;
            cellItemsBuf_ = nullptr;
            delete cellSlotBuf_;
            cellSlotBuf_ = nullptr;
            hashNx_ = hashNy_ = 0;
        }
        return;
    }
    const int nx = std::max(1, int(std::ceil(boundW_ / minDist)));
    const int ny = std::max(1, int(std::ceil(boundH_ / minDist)));
    const int nCells = nx * ny;
    const int count = getParticleCount();
    if (nx == hashNx_ && ny == hashNy_) return;
    delete cellHeadBuf_;
    cellHeadBuf_ = nullptr;
    delete cellNextBuf_;
    cellNextBuf_ = nullptr;
    delete cellItemsBuf_;
    cellItemsBuf_ = nullptr;
    delete cellSlotBuf_;
    cellSlotBuf_ = nullptr;
    cellHeadBuf_ = gpgpu_->newBuffer(nCells * int(sizeof(uint32_t)), "storage");
    cellNextBuf_ = gpgpu_->newBuffer(count * int(sizeof(uint32_t)), "storage");
    cellItemsBuf_ = gpgpu_->newBuffer(count * int(sizeof(uint32_t)), "storage");
    cellSlotBuf_ = gpgpu_->newBuffer(int(sizeof(uint32_t)), "storage");
    hashNx_ = nx;
    hashNy_ = ny;
}

void ClothGPU::setGravity(float gx, float gy) {
    gravityX_ = gx;
    gravityY_ = gy;
}

void ClothGPU::setStiffness(float stiffness) {
    stiffness_ = std::clamp(stiffness, 0.f, 1.f);
}

void ClothGPU::setIterations(int iterations) {
    iterations_ = std::max(1, iterations);
}

void ClothGPU::setDamping(float damping) {
    damping_ = std::clamp(damping, 0.f, 1.f);
}

void ClothGPU::setParticleSize(float size) {
    particleSize_ = std::max(1.f, size);
}

void ClothGPU::setSelfCollision(bool on) { selfCollision_ = on; }

void ClothGPU::setBounds(float x, float y, float w, float h) {
    if (w <= 0.f || h <= 0.f) {
        clearBounds();
        return;
    }
    hasBounds_ = true;
    boundX_ = x;
    boundY_ = y;
    boundW_ = w;
    boundH_ = h;
}

void ClothGPU::clearBounds() { hasBounds_ = false; }

void ClothGPU::pin(int index) {
    if (index < 0 || index >= getParticleCount())
        throw Exception("ClothGPU.pin: index out of range");
    flagsCpu_[static_cast<size_t>(index)] = 1.f;
    uploadPinned();
}

void ClothGPU::unpin(int index) {
    if (index < 0 || index >= getParticleCount())
        throw Exception("ClothGPU.unpin: index out of range");
    flagsCpu_[static_cast<size_t>(index)] = 0.f;
    uploadPinned();
}

void ClothGPU::pinTopRow() {
    for (int c = 0; c < cols_; ++c) flagsCpu_[static_cast<size_t>(c)] = 1.f;
    uploadPinned();
}

bool ClothGPU::isPinned(int index) const {
    if (index < 0 || index >= getParticleCount()) return false;
    return flagsCpu_[static_cast<size_t>(index)] > 0.5f;
}

void ClothGPU::applyForce(float fx, float fy) {
    forceX_ += fx;
    forceY_ += fy;
}

void ClothGPU::interactAt(float x, float y, float radius, float strength) {
    interactX_ = x;
    interactY_ = y;
    interactRadius_ = std::max(0.f, radius);
    interactStrength_ = strength;
}

void ClothGPU::setColor(float r, float g, float b, float a) {
    colorR_ = r;
    colorG_ = g;
    colorB_ = b;
    colorA_ = a;
}

float ClothGPU::getParticleX(int index) const {
    if (index < 0 || index >= getParticleCount()) return 0.f;
    return posCpu_[static_cast<size_t>(index) * 4 + 0];
}

float ClothGPU::getParticleY(int index) const {
    if (index < 0 || index >= getParticleCount()) return 0.f;
    return posCpu_[static_cast<size_t>(index) * 4 + 1];
}

void ClothGPU::reset() {
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            const size_t i = static_cast<size_t>(r * cols_ + c);
            posCpu_[i * 4 + 0] = originX_ + float(c) * spacing_;
            posCpu_[i * 4 + 1] = originY_ + float(r) * spacing_;
            posCpu_[i * 4 + 2] = posCpu_[i * 4 + 0];
            posCpu_[i * 4 + 3] = posCpu_[i * 4 + 1];
        }
    }
    std::fill(flagsCpu_.begin(), flagsCpu_.end(), 0.f);
    pinTopRow();
    forceX_ = forceY_ = 0.f;
    interactStrength_ = 0.f;
    uploadInitialState();
}

void ClothGPU::update(float dt) {
    if (destroyed_) return;
    if (dt < 0.f) dt = 0.f;
    if (dt > 0.05f) dt = 0.05f;
    auto result = stepGpu(dt, 2);
    result.ignore("legacy ClothGPU::update cannot return a structured result");
}

eve::Result<void> ClothGPU::stepGpu(float dt, int substeps) {
    if (destroyed_ || !shader_ || !gpgpu_ || !seq_)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "GPU cloth resources are not available",
                                                                 "physics.clothGpu.step"));
    if (substeps < 1 || substeps > 1024)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "GPU cloth substep count must be in [1, 1024]",
                                                                 "physics.clothGpu.step.substeps"));

    const int count = getParticleCount();
    ensureHashBuffers();
    const float h = dt / float(substeps);
    shader_->bindBuffer(2, linkBuf_);
    shader_->bindBuffer(3, flagBuf_);
    if (cellHeadBuf_ && cellNextBuf_ && cellItemsBuf_ && cellSlotBuf_) {
        shader_->bindBuffer(4, cellHeadBuf_);
        shader_->bindBuffer(5, cellNextBuf_);
        shader_->bindBuffer(6, cellItemsBuf_);
        shader_->bindBuffer(7, cellSlotBuf_);
    }
    shader_->setFloat(0, h);
    shader_->setFloat(1, gravityX_);
    shader_->setFloat(2, gravityY_);
    shader_->setFloat(3, stiffness_);
    shader_->setFloat(4, float(iterations_));
    shader_->setFloat(5, float(count));
    shader_->setFloat(6, damping_);
    shader_->setFloat(7, boundX_);
    shader_->setFloat(8, boundY_);
    shader_->setFloat(9, boundW_);
    shader_->setFloat(10, boundH_);
    shader_->setFloat(11, hasBounds_ ? 1.f : 0.f);
    shader_->setFloat(12, forceX_);
    shader_->setFloat(13, forceY_);
    shader_->setFloat(14, interactX_);
    shader_->setFloat(15, interactY_);
    shader_->setFloat(16, interactRadius_);
    shader_->setFloat(17, interactStrength_);
    shader_->setFloat(18, float(kMaxLinksPerParticle));
    shader_->setFloat(20, particleSize_ * 2.f);
    shader_->setFloat(21, selfCollision_ ? 1.f : 0.f);
    shader_->setFloat(22, float(hashNx_));
    shader_->setFloat(23, float(hashNy_));
    const int nCells = hashNx_ > 0 && hashNy_ > 0 ? hashNx_ * hashNy_ : 0;
    shader_->setFloat(24, float(nCells));

    const int groups = (count + 63) / 64;
    // Double-buffered passes: every pass reads posIn and writes posOut, then the
    // roles swap. Keeps constraint iterations Jacobi-stable (no in-place
    // feedback between threads). The whole frame is recorded into one Sequence
    // submission (the Sequence inserts memory barriers between dispatches).
    GpuBuffer *in = posBuf_;
    GpuBuffer *out = posBufB_;
    const auto pass = [&](float mode) {
        shader_->bindBuffer(0, in);
        shader_->bindBuffer(1, out);
        shader_->setFloat(19, mode);
        seq_->recordDispatch(shader_, groups, 1, 1);
        std::swap(in, out);
    };
    seq_->begin();
    for (int s = 0; s < substeps; ++s) {
        pass(0.f);  // integrate
        if (selfCollision_ && nCells > 0) {
            // Clear counters, build the hash from the freshly integrated
            // positions, then constraints, then hash-based self-collision.
            shader_->setFloat(19, 4.f);
            seq_->recordDispatch(shader_, (nCells + 63) / 64, 1, 1);
            pass(2.f);
        }
        for (int it = 0; it < iterations_; ++it) pass(1.f);  // constraints + bounds
        if (selfCollision_) {
            for (int sc = 0; sc < 2; ++sc)
                pass(3.f);  // self-collision (hash or O(n²) fallback)
        }
    }

    // The pass count is even in both branches, so the result lands in posBuf_.
    seq_->recordDownload(posBuf_, staging_, uint64_t(count) * 4 * sizeof(float));
    seq_->submit();
    staging_->downloadBytes(posCpu_.data(), uint64_t(count) * 4 * sizeof(float));
    forceX_ = 0.f;
    forceY_ = 0.f;
    interactStrength_ = 0.f;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ClothGPU::step(const eve::SimulationStep &stepValue, const SimulationSettings &settings) {
    if (destroyed_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Cannot step a destroyed GPU cloth", "physics.clothGpu.step"));
    auto valid = detail::validateSimulationStep(stepValue, settings, observation_);
    if (!valid) return valid;
    auto next = detail::advanceSimulationObservation(observation_, stepValue);
    if (!next) return eve::Result<void>::failure(next.status());
    try {
        auto applied = stepGpu(static_cast<float>(stepValue.delta.seconds()), settings.subStepCount);
        if (!applied) return applied;
    } catch (const std::exception &error) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                                                 std::string("GPU cloth step failed: ") + error.what(),
                                                                 "physics.clothGpu.step"));
    } catch (...) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "GPU cloth step failed with an unknown exception", "physics.clothGpu.step"));
    }
    observation_ = std::move(next).takeValue();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ClothGPU::restoreObservation(const SimulationObservation &observation) {
    auto valid = detail::validateSimulationObservation(observation, "physics.clothGpu.restoreObservation");
    if (!valid) return valid;
    if (destroyed_)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "Cannot restore a destroyed GPU cloth",
                                                                 "physics.clothGpu.restoreObservation"));
    observation_ = observation;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void ClothGPU::draw(graphics::Graphics *gfx) {
    if (!gfx || destroyed_) return;
    const Color linkColor(colorR_, colorG_, colorB_, colorA_ * 0.75f);
    const Color nodeColor(colorR_, colorG_, colorB_, colorA_);
    const int count = getParticleCount();
    for (int i = 0; i < count; ++i) {
        for (int k = 0; k < kMaxLinksPerParticle; ++k) {
            const Link &link = links_[static_cast<size_t>(i) * kMaxLinksPerParticle + k];
            if (link.other < 0) break;
            const float x1 = posCpu_[static_cast<size_t>(i) * 4 + 0];
            const float y1 = posCpu_[static_cast<size_t>(i) * 4 + 1];
            const float x2 = posCpu_[static_cast<size_t>(link.other) * 4 + 0];
            const float y2 = posCpu_[static_cast<size_t>(link.other) * 4 + 1];
            const float dx = x2 - x1;
            const float dy = y2 - y1;
            const float len = std::sqrt(dx * dx + dy * dy);
            const int steps = std::max(1, int(len / 4.f));
            for (int s = 0; s <= steps; ++s) {
                const float t = float(s) / float(steps);
                gfx->drawSolidRect(x1 + dx * t - 1.f, y1 + dy * t - 1.f, 2.f, 2.f, linkColor);
            }
        }
    }
    for (int i = 0; i < count; ++i) {
        const float s = flagsCpu_[static_cast<size_t>(i)] > 0.5f
                            ? std::max(5.f, particleSize_ + 2.f)
                            : particleSize_;
        gfx->drawSolidRect(posCpu_[static_cast<size_t>(i) * 4 + 0] - s * 0.5f,
                           posCpu_[static_cast<size_t>(i) * 4 + 1] - s * 0.5f, s, s,
                           nodeColor);
    }
}

}  // namespace eve::physics
