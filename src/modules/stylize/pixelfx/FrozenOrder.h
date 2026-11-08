// Native index-only adaptation of V8 array-sort.tq at Node v26.4.0.
// Copyright (c) 2001-2018 Python Software Foundation; All Rights Reserved.
// Copyright 2014, the V8 project authors. See FrozenOrder.LICENSE.txt.
// Modification: bounded owned C++ indices and a synchronous random comparison
// stream replace JS receivers, accessors, callbacks and runtime allocation.
#pragma once
#include <numeric>
#include "common/Assert.h"
#include "stylize/pixelfx/Math.h"
namespace eve::stylize::pixelfx::status {
/** @brief Owned canonical Frozen root ordering and cold comparison count. */
struct FrozenOrderResult {
    std::vector<int> indices;
    uint64_t         comparisons;
};
namespace detail {
template <class Comparison>
class FrozenOrder {
    std::vector<int>                work, temp;
    std::vector<std::array<int, 2>> runs;
    Comparison&                     comparison;
    uint64_t                        comparisons = 0;
    int                             minGallop   = 7;
    double                          compare() {
        ++comparisons;
        return comparison();
    }
    static int& at(std::vector<int>& v, int i) {
        EV_ASSERT((i >= 0 && size_t(i) < v.size()), "Frozen source sort index stays in owned storage");
        return v[size_t(i)];
    }
    static void copy(std::vector<int>& src, int from, std::vector<int>& dst, int to, int n) {
        EV_ASSERT((n >= 0 && from >= 0 && to >= 0 && size_t(from + n) <= src.size() && size_t(to + n) <= dst.size()),
                  "Frozen source sort copy is bounded");
        if (from < to)
            for (int i = n - 1; i >= 0; --i) at(dst, to + i) = at(src, from + i);
        else
            for (int i = 0; i < n; ++i) at(dst, to + i) = at(src, from + i);
    }
    void insertion(int low, int start, int high) {
        if (low == start) ++start;
        for (; start < high; ++start) {
            const int pivot = at(work, start);
            int       left = low, right = start;
            while (left < right) {
                const int mid = left + ((right - left) >> 1);
                if (compare() < 0)
                    right = mid;
                else
                    left = mid + 1;
            }
            for (int i = start; i > left; --i) at(work, i) = at(work, i - 1);
            at(work, left) = pivot;
        }
    }
    int countRun(int low, int high) {
        if (low + 1 == high) return 1;
        int        length     = 2;
        const bool descending = compare() < 0;
        for (int i = low + 2; i < high; ++i) {
            const double order = compare();
            if (descending ? order >= 0 : order < 0) break;
            ++length;
        }
        if (descending) std::reverse(work.begin() + low, work.begin() + low + length);
        return length;
    }
    int gallopLeft(int length, int hint) {
        int    last = 0, offset = 1;
        double order = compare();
        if (order < 0) {
            const int max = length - hint;
            while (offset < max) {
                order = compare();
                if (order >= 0) break;
                last   = offset;
                offset = (offset << 1) + 1;
            }
            offset = std::min(offset, max);
            last += hint;
            offset += hint;
        } else {
            const int max = hint + 1;
            while (offset < max) {
                order = compare();
                if (order < 0) break;
                last   = offset;
                offset = (offset << 1) + 1;
            }
            offset        = std::min(offset, max);
            const int old = last;
            last          = hint - offset;
            offset        = hint - old;
        }
        for (++last; last < offset;) {
            const int mid = last + ((offset - last) >> 1);
            (void)mid;
            if (compare() < 0)
                last = mid + 1;
            else
                offset = mid;
        }
        return offset;
    }
    int gallopRight(int length, int hint) {
        int    last = 0, offset = 1;
        double order = compare();
        if (order < 0) {
            const int max = hint + 1;
            while (offset < max) {
                order = compare();
                if (order >= 0) break;
                last   = offset;
                offset = (offset << 1) + 1;
            }
            offset        = std::min(offset, max);
            const int old = last;
            last          = hint - offset;
            offset        = hint - old;
        } else {
            const int max = length - hint;
            while (offset < max) {
                order = compare();
                if (order < 0) break;
                last   = offset;
                offset = (offset << 1) + 1;
            }
            offset = std::min(offset, max);
            last += hint;
            offset += hint;
        }
        for (++last; last < offset;) {
            const int mid = last + ((offset - last) >> 1);
            if (compare() < 0)
                offset = mid;
            else
                last = mid + 1;
        }
        return offset;
    }
    void mergeLow(int baseA, int lengthA, int baseB, int lengthB) {
        temp.resize(size_t(lengthA));
        copy(work, baseA, temp, 0, lengthA);
        int dest = baseA, cursorTemp = 0, cursorB = baseB;
        at(work, dest++) = at(work, cursorB++);
        if (--lengthB == 0) goto succeed;
        if (lengthA == 1) goto copyB;
        {
            int gallop = minGallop;
            for (;;) {
                int winsA = 0, winsB = 0;
                for (;;) {
                    if (compare() < 0) {
                        at(work, dest++) = at(work, cursorB++);
                        ++winsB;
                        --lengthB;
                        winsA = 0;
                        if (lengthB == 0) goto succeed;
                        if (winsB >= gallop) break;
                    } else {
                        at(work, dest++) = at(temp, cursorTemp++);
                        ++winsA;
                        --lengthA;
                        winsB = 0;
                        if (lengthA == 1) goto copyB;
                        if (winsA >= gallop) break;
                    }
                }
                ++gallop;
                bool first = true;
                while (winsA >= 7 || winsB >= 7 || first) {
                    first     = false;
                    gallop    = std::max(1, gallop - 1);
                    minGallop = gallop;
                    winsA     = gallopRight(lengthA, 0);
                    if (winsA > 0) {
                        copy(temp, cursorTemp, work, dest, winsA);
                        dest += winsA;
                        cursorTemp += winsA;
                        lengthA -= winsA;
                        if (lengthA == 1) goto copyB;
                        if (lengthA == 0) goto succeed;
                    }
                    at(work, dest++) = at(work, cursorB++);
                    if (--lengthB == 0) goto succeed;
                    winsB = gallopLeft(lengthB, 0);
                    if (winsB > 0) {
                        copy(work, cursorB, work, dest, winsB);
                        dest += winsB;
                        cursorB += winsB;
                        lengthB -= winsB;
                        if (lengthB == 0) goto succeed;
                    }
                    at(work, dest++) = at(temp, cursorTemp++);
                    if (--lengthA == 1) goto copyB;
                }
                ++gallop;
                minGallop = gallop;
            }
        }
    succeed:
        if (lengthA > 0) copy(temp, cursorTemp, work, dest, lengthA);
        return;
    copyB:
        copy(work, cursorB, work, dest, lengthB);
        at(work, dest + lengthB) = at(temp, cursorTemp);
    }
    void mergeHigh(int baseA, int lengthA, int baseB, int lengthB) {
        temp.resize(size_t(lengthB));
        copy(work, baseB, temp, 0, lengthB);
        int dest = baseB + lengthB - 1, cursorTemp = lengthB - 1, cursorA = baseA + lengthA - 1;
        at(work, dest--) = at(work, cursorA--);
        if (--lengthA == 0) goto succeed;
        if (lengthB == 1) goto copyA;
        {
            int gallop = minGallop;
            for (;;) {
                int winsA = 0, winsB = 0;
                for (;;) {
                    if (compare() < 0) {
                        at(work, dest--) = at(work, cursorA--);
                        ++winsA;
                        --lengthA;
                        winsB = 0;
                        if (lengthA == 0) goto succeed;
                        if (winsA >= gallop) break;
                    } else {
                        at(work, dest--) = at(temp, cursorTemp--);
                        ++winsB;
                        --lengthB;
                        winsA = 0;
                        if (lengthB == 1) goto copyA;
                        if (winsB >= gallop) break;
                    }
                }
                ++gallop;
                bool first = true;
                while (winsA >= 7 || winsB >= 7 || first) {
                    first     = false;
                    gallop    = std::max(1, gallop - 1);
                    minGallop = gallop;
                    winsA     = lengthA - gallopRight(lengthA, lengthA - 1);
                    if (winsA > 0) {
                        dest -= winsA;
                        cursorA -= winsA;
                        copy(work, cursorA + 1, work, dest + 1, winsA);
                        lengthA -= winsA;
                        if (lengthA == 0) goto succeed;
                    }
                    at(work, dest--) = at(temp, cursorTemp--);
                    if (--lengthB == 1) goto copyA;
                    winsB = lengthB - gallopLeft(lengthB, lengthB - 1);
                    if (winsB > 0) {
                        dest -= winsB;
                        cursorTemp -= winsB;
                        copy(temp, cursorTemp + 1, work, dest + 1, winsB);
                        lengthB -= winsB;
                        if (lengthB == 1) goto copyA;
                        if (lengthB == 0) goto succeed;
                    }
                    at(work, dest--) = at(work, cursorA--);
                    if (--lengthA == 0) goto succeed;
                }
                ++gallop;
                minGallop = gallop;
            }
        }
    succeed:
        if (lengthB > 0) copy(temp, 0, work, dest - (lengthB - 1), lengthB);
        return;
    copyA:
        dest -= lengthA;
        cursorA -= lengthA;
        copy(work, cursorA + 1, work, dest + 1, lengthA);
        at(work, dest) = at(temp, cursorTemp);
    }
    void mergeAt(int i) {
        int       baseA = runs[size_t(i)][0], lengthA = runs[size_t(i)][1];
        const int baseB    = runs[size_t(i + 1)][0];
        int       lengthB  = runs[size_t(i + 1)][1];
        runs[size_t(i)][1] = lengthA + lengthB;
        runs.erase(runs.begin() + i + 1);
        const int k = gallopRight(lengthA, 0);
        baseA += k;
        lengthA -= k;
        if (lengthA == 0) return;
        lengthB = gallopLeft(lengthB, lengthB - 1);
        if (lengthB == 0) return;
        if (lengthA <= lengthB)
            mergeLow(baseA, lengthA, baseB, lengthB);
        else
            mergeHigh(baseA, lengthA, baseB, lengthB);
    }
    bool invariant(int n) const {
        return n < 2 || runs[size_t(n - 2)][1] > runs[size_t(n - 1)][1] + runs[size_t(n)][1];
    }
    void collapse() {
        while (runs.size() > 1) {
            int n = int(runs.size()) - 2;
            if (!invariant(n + 1) || !invariant(n)) {
                if (runs[size_t(n - 1)][1] < runs[size_t(n + 1)][1]) --n;
                mergeAt(n);
            } else if (runs[size_t(n)][1] <= runs[size_t(n + 1)][1])
                mergeAt(n);
            else
                break;
        }
    }

public:
    /** @brief Borrow a trusted compiled comparison stream only for this synchronous preparation object.
     * @note No scripts, locks, retained actor pointers or callbacks outside the preparation call. */
    FrozenOrder(int length, Comparison& stream) : work(size_t(length)), comparison(stream) {
        std::iota(work.begin(), work.end(), 0);
    }
    /** @brief Produce source-compatible order; no comparator imposes a C++ ordering contract.
     * @cost Bounded O(n log n) comparisons and O(n) owned scratch per configuration preparation. */
    [[nodiscard]] FrozenOrderResult run() {
        const int length = int(work.size());
        if (length < 8)
            insertion(0, 0, length);
        else {
            int n = length, r = 0;
            while (n >= 64) {
                r |= n & 1;
                n >>= 1;
            }
            const int minimum = n + r;
            int       low = 0, remaining = length;
            while (remaining) {
                int count = countRun(low, low + remaining);
                if (count < minimum) {
                    const int forced = std::min(minimum, remaining);
                    insertion(low, low + count, low + forced);
                    count = forced;
                }
                runs.push_back({low, count});
                collapse();
                low += count;
                remaining -= count;
            }
            while (runs.size() > 1) {
                int i = int(runs.size()) - 2;
                if (i > 0 && runs[size_t(i - 1)][1] < runs[size_t(i + 1)][1]) --i;
                mergeAt(i);
            }
        }
        return {std::move(work), comparisons};
    }
};
}  // namespace detail
/** @brief Prepare canonical Frozen candidate order, consuming a borrowed RNG synchronously.
 * @note Caller-thread affine; trusted length in [0,42496]; output owns all indices, no RNG/actor retained.
 * @cost O(n log n) bounded cold comparison work and O(n) storage, amortized over all instance draws. */
[[nodiscard]] inline FrozenOrderResult frozenRootOrder(int length, Rng& rng) {
    EV_PARAM_CHECK((length >= 0 && length <= 42496), "Frozen root count follows bounded actor silhouette");
    auto compare = [&rng] { return rng.next() - .5; };
    return detail::FrozenOrder(length, compare).run();
}
}  // namespace eve::stylize::pixelfx::status
