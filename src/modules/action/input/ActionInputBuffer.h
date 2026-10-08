#pragma once
#include "common/Export.h"

/** @file ActionInputBuffer.h @brief Deterministic semantic input buffer for combat cancels. */

#include "common/Result.h"
#include "common/SubjectRef.h"
#include "common/Time.h"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace eve::action::input {

/** @brief One buffered semantic command tagged with the tick it was pressed. */
struct BufferedInput {
    SubjectRef     subject;
    std::string    input;
    SimulationTick tick = SimulationTick::zero();
    std::int32_t   priority = 0;
};

/**
 * @brief Owner-thread FIFO/priority buffer of semantic combat inputs.
 *
 * Capacity and lifetime are measured in simulation ticks. Consume removes the
 * matching command exactly once even when activation is later rejected.
 */
class EVENGINE_API_BACKENDS ActionInputBuffer {
public:
    /** @brief Construct with positive capacity and lifetime window in ticks. */
    ActionInputBuffer(std::uint32_t capacity, std::uint32_t lifetimeTicks);

    /** @brief Push one owning input at the supplied tick; full buffers reject. */
    [[nodiscard]] Result<void> push(BufferedInput input);
    /** @brief Drop entries older than lifetime relative to now. */
    [[nodiscard]] Result<void> expire(SimulationTick now);
    /**
     * @brief Consume the highest-priority buffered input that matches one of the allows.
     * @return The consumed command, or NotFound when nothing matches.
     */
    [[nodiscard]] Result<BufferedInput> consume(SubjectRef subject, const std::vector<std::string>& allows);
    /** @brief Peek without consuming. */
    [[nodiscard]] Result<BufferedInput> peek(SubjectRef subject, const std::vector<std::string>& allows) const;
    /** @brief Clear every buffered command for one subject. */
    void clear(SubjectRef subject);
    /** @brief Clear the entire buffer. */
    void clearAll() noexcept { entries_.clear(); }
    /** @brief Number of buffered commands. */
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    std::uint32_t            capacity_      = 1;
    std::uint32_t            lifetimeTicks_ = 1;
    std::deque<BufferedInput> entries_;
};

}  // namespace eve::action::input
