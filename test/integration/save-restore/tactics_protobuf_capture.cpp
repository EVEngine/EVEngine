#include "common/ECS.h"
#include "tactics/Tactics.h"
#include "tactics/TacticsGameSaveParticipant.h"
#include "tactics/TacticsPersistence.h"
#include "tactics_battle.pb.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <google/protobuf/wrappers.pb.h>

TEST_CASE("tactics.protobufCaptureUsesTypedBattleFields") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;
    const auto            battleId = eve::PersistentId::parse("00000000-0000-0000-0000-000000000a10");
    const auto            sideId   = eve::PersistentId::parse("00000000-0000-0000-0000-000000000a11");
    const auto            unitId   = eve::PersistentId::parse("00000000-0000-0000-0000-000000000a12");
    REQUIRE(battleId.has_value());
    REQUIRE(sideId.has_value());
    REQUIRE(unitId.has_value());
    const auto battleSubject = eve::SubjectRef::fromPersistentId(*battleId);
    const auto sideSubject   = eve::SubjectRef::fromPersistentId(*sideId);
    const auto unitSubject   = eve::SubjectRef::fromPersistentId(*unitId);
    auto       battle        = tactics.newBattle(battleSubject, 91);
    REQUIRE(battle.ok());
    const auto battleHandle = std::move(battle).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    auto side = tactics.newSide(battleHandle, sideSubject);
    REQUIRE(side.ok());
    auto unit = tactics.newUnit(battleHandle, std::move(side).takeValue(), unitSubject, {}, {0, 0, 0}, {1, 2, 3, 4});
    REQUIRE(unit.ok());

    auto* state = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    REQUIRE(state != nullptr);
    const eve::SnapshotHashProvider hash = [](std::string_view) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes digest{};
        digest.fill(0x42);
        return eve::Result<eve::ContentId>::success(eve::ContentId(digest));
    };
    auto binary = eve::tactics::TacticsPersistence::snapshotBinary(*state, hash);
    REQUIRE(binary.ok());
    eve::tactics::TacticsGameSaveParticipant participant(*state, hash);
    CHECK_EQ(participant.messageType(), std::string_view("eve.tactics.proto.BattleSaveEnvelope"));
    auto participantWire = participant.captureBinary();
    REQUIRE(participantWire.ok());
    CHECK_EQ(participantWire.value(), binary.value());
    auto participantPrepared = participant.prepareBinary(participantWire.value());
    REQUIRE(participantPrepared.ok());
    std::move(participantPrepared).takeValue()->commit();
    CHECK(participant.matchMessageType("google.protobuf.BytesValue") == eve::GameSaveMessageTypeMatch::Legacy);
    auto legacy = eve::tactics::TacticsPersistence::snapshot(*state, hash);
    REQUIRE(legacy.ok());
    auto legacyJson = eve::serializeSnapshotEnvelope(legacy.value());
    REQUIRE(legacyJson.ok());
    google::protobuf::BytesValue legacyWrapper;
    legacyWrapper.set_value(std::move(legacyJson).takeValue());
    auto legacyPrepared = participant.prepareBinary(legacyWrapper.SerializeAsString());
    REQUIRE(legacyPrepared.ok());
    auto repeated = eve::tactics::TacticsPersistence::snapshotBinary(*state, hash);
    REQUIRE(repeated.ok());
    CHECK_EQ(binary.value(), repeated.value());

    eve::tactics::proto::BattleSaveEnvelope envelope;
    REQUIRE(envelope.ParseFromString(binary.value()));
    CHECK_EQ(envelope.type(), std::string("tactics.battle"));
    CHECK_EQ(envelope.schema(), std::string("tactics:battle"));
    CHECK_EQ(envelope.version(), 8u);
    CHECK_EQ(envelope.instance_id(), battleSubject.format());
    CHECK_EQ(envelope.digest(), std::string(16, '\x42'));
    eve::tactics::proto::BattlePayload payload;
    REQUIRE(payload.ParseFromString(envelope.payload_wire()));
    CHECK_EQ(payload.seed(), 91u);
    CHECK_EQ(payload.board().cells_size(), 1);
    CHECK_EQ(payload.sides_size(), 1);
    CHECK_EQ(payload.units_size(), 1);
    CHECK_EQ(payload.units(0).subject(), unitSubject.format());
    CHECK_EQ(payload.units(0).side(), sideSubject.format());
    CHECK_EQ(payload.units(0).action_points(), 1);
    CHECK_EQ(payload.events().next_sequence(), 1u);
    CHECK_EQ(payload.commands().next_sequence(), 1u);

    auto prepared = eve::tactics::TacticsPersistence::prepareRestoreBinary(*state, binary.value(), hash);
    REQUIRE(prepared.ok());
    eve::tactics::TacticsPersistence::commitPrepared(std::move(prepared).takeValue());
    CHECK_EQ(state->identity()->seed, 91u);

    envelope.set_digest(std::string(16, '\x23'));
    auto corruptDigest =
        eve::tactics::TacticsPersistence::prepareRestoreBinary(*state, envelope.SerializeAsString(), hash);
    CHECK(!corruptDigest.ok());
    CHECK_EQ(state->identity()->seed, 91u);

    envelope.set_digest(std::string(16, '\x42'));
    payload.mutable_board()->mutable_cells(0)->clear_cell();
    envelope.set_payload_wire(payload.SerializeAsString());
    auto invalidCandidate =
        eve::tactics::TacticsPersistence::prepareRestoreBinary(*state, envelope.SerializeAsString(), hash);
    CHECK(!invalidCandidate.ok());
    CHECK_EQ(state->identity()->seed, 91u);

    auto log = eve::tactics::TacticsPersistence::commandLogJson(*state, state->turn()->revision);
    REQUIRE(log.ok());
    CHECK(log.value().find("tactics:command-log") != std::string::npos);
    auto replay = eve::tactics::TacticsPersistence::parseCommandLogJson(log.value(), state->turn()->revision);
    REQUIRE(replay.ok());
    CHECK(replay.value().values.empty());
    auto oldLog = eve::tactics::TacticsPersistence::commandLogValue(*state, state->turn()->revision);
    REQUIRE(oldLog.ok());
    auto oldJson = oldLog.value().toJson();
    REQUIRE(oldJson.ok());
    auto migrated = eve::tactics::TacticsPersistence::parseCommandLogJson(oldJson.value(), state->turn()->revision);
    REQUIRE(migrated.ok());
    CHECK(migrated.value().values.empty());
}
