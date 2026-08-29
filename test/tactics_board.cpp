#include "common/ECS.h"
#include "tactics/TacticsTypes.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

}  // namespace

TEST_CASE("tactics.boardMaintainsAtomicOccupancyIndexes") {
    eve::tactics::BoardState board;
    REQUIRE(board.addCell({0, 0, 0}).ok());
    REQUIRE(board.addCell({1, 0, 0}).ok());
    REQUIRE(board.addCell({2, 0, 0}, eve::tactics::CellState{100, 0, false, {}}).ok());

    const auto alpha = subject("00000000-0000-0000-0000-000000000001");
    const auto beta  = subject("00000000-0000-0000-0000-000000000002");
    REQUIRE(board.place(alpha, {0, 0, 0}).ok());
    REQUIRE(board.place(beta, {1, 0, 0}).ok());

    auto occupied = board.move(alpha, {1, 0, 0});
    CHECK(!occupied.ok());
    CHECK_EQ(occupied.code(), eve::StatusCode::Conflict);
    CHECK((board.position(alpha) == eve::tactics::Cell{0, 0, 0}));
    CHECK(board.occupant({1, 0, 0}) == beta);

    auto blocked = board.move(alpha, {2, 0, 0});
    CHECK(!blocked.ok());
    CHECK_EQ(blocked.code(), eve::StatusCode::Rejected);
    CHECK((board.position(alpha) == eve::tactics::Cell{0, 0, 0}));
    REQUIRE(board.validateInvariants().ok());
}

TEST_CASE("tactics.boardEnumeratesTopologyDeterministically") {
    eve::tactics::BoardState board;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) REQUIRE(board.addCell({x, y, 0}).ok());
    }

    const auto square = board.neighbours({0, 0, 0});
    REQUIRE_EQ(square.size(), 4u);
    CHECK((square[0] == eve::tactics::Cell{1, 0, 0}));
    CHECK((square[1] == eve::tactics::Cell{0, 1, 0}));

    board.setTopology(eve::tactics::BoardTopology::HexAxial);
    const auto hex = board.neighbours({0, 0, 0});
    REQUIRE_EQ(hex.size(), 6u);
    CHECK((hex[0] == eve::tactics::Cell{1, 0, 0}));
    CHECK((hex[1] == eve::tactics::Cell{1, -1, 0}));
}
