#pragma once
// Owning detached facts, shared only by native capture and the persistent codec.
#include "rts/RTSTypes.h"
#include "rts/spawn/RTSSpawnCapture.h"
namespace eve::rts_spawn {
struct RTSSpawnCapture::Impl {
    PersistentId                      owner;
    spawn::WorldIncarnation           incarnation;
    std::uint64_t                     epoch = 0;
    PersistentId                      saveEpoch;
    SubjectRef                        faction, commandTarget;
    std::vector<CapturedSpawnUnit>    units;
    std::vector<CapturedFutureUnit>   future;
    std::vector<CapturedSpawnReceipt> receipts;
    std::unique_ptr<rts::CommandSpec> command;
    std::uint64_t                     sequence = 0;
};
}  // namespace eve::rts_spawn
