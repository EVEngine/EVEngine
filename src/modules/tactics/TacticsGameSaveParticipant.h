#pragma once

#include "common/Export.h"
#include "common/GameSaveSession.h"

namespace eve::tactics {
class Battle;

/** @brief Borrow one authoritative battle as a game-save participant. */
class EVENGINE_API_DOMAINS TacticsGameSaveParticipant final : public IGameSaveParticipant {
public:
    /** @brief Battle and digest contract must remain stable for this adapter's lifetime. */
    TacticsGameSaveParticipant(Battle& battle, SnapshotHashProvider hashProvider);
    std::string_view                                            saveKey() const noexcept override;
    std::string_view                                            messageType() const noexcept override;
    /** @brief Also classify the retired JSON-in-BytesValue wire type for read-only migration. */
    GameSaveMessageTypeMatch          matchMessageType(std::string_view type) const noexcept override;
    [[nodiscard]] Result<std::string> captureBinary() const override;
    [[nodiscard]] Result<std::unique_ptr<IPreparedGameRestore>> prepareBinary(std::string_view wire) override;
    [[nodiscard]] Result<std::unique_ptr<IPreparedGameRestore>> prepare(const Value& state) override;

private:
    Battle*              battle_;
    SnapshotHashProvider hashProvider_;
};

}  // namespace eve::tactics
