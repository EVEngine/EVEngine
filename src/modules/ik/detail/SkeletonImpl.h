#pragma once

/**
 * @file Module-private: defines Skeleton2D/3D Pimpl storage.
 * Must NOT be included from public module headers — only from .cpp under src/modules/ik/.
 */

#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"

#include "ik.hpp"

namespace eve::ik {

struct Skeleton2D::Impl {
    ::ik::skeleton2d sk;
    ::ik::ecs2d      state;

    void ensureSorted() {
        if (sk.bones().empty()) {
            sk.topologicalSort();
        }
    }

    void ensureStateSize() {
        ensureSorted();
        const unsigned n = static_cast<unsigned>(sk.bones().size());
        if (state.size() < n) {
            state.position_.resize(n);
            state.orientation_.resize(n);
            state.scale_.resize(n);
            state.rotation_.resize(n);
        }
    }

    ::ik::bone2d *boneAt(int boneId) const {
        if (boneId < 0) return nullptr;
        const auto &bones = sk.bones();
        if (static_cast<size_t>(boneId) >= bones.size()) return nullptr;
        return bones[static_cast<size_t>(boneId)];
    }
};

struct Skeleton3D::Impl {
    ::ik::skeleton3d sk;
    ::ik::ecs3d      state;

    void ensureSorted() {
        if (sk.bones().empty()) {
            sk.topologicalSort();
        }
    }

    void ensureStateSize() {
        ensureSorted();
        const unsigned n = static_cast<unsigned>(sk.bones().size());
        if (state.size() < n) {
            state.position_.resize(n);
            state.orientation_.resize(n);
            state.scale_.resize(n);
            state.rotation_.resize(n);
        }
    }

    ::ik::bone3d *boneAt(int boneId) const {
        if (boneId < 0) return nullptr;
        const auto &bones = sk.bones();
        if (static_cast<size_t>(boneId) >= bones.size()) return nullptr;
        return bones[static_cast<size_t>(boneId)];
    }
};

}  // namespace eve::ik
