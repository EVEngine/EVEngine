#include "zeroerr/unittest.h"

#include "common/GameSaveSession.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint64_t         hash = 14695981039346656037ull;
        for (const unsigned char byte : input) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::uint8_t>(hash >> ((index % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

class FakeParticipant final : public eve::IGameSaveParticipant {
public:
    FakeParticipant(std::string key, int value) : key_(std::move(key)), value(value) {}
    std::string_view        saveKey() const noexcept override { return key_; }
    eve::Result<eve::Value> capture() const override { return eve::Result<eve::Value>::success(eve::Value(value)); }
    eve::Result<std::unique_ptr<eve::IPreparedGameRestore>> prepare(const eve::Value& state) override {
        if (reject || !state.isInt64())
            return eve::Result<std::unique_ptr<eve::IPreparedGameRestore>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "injected participant failure"));
        if (returnNull) return eve::Result<std::unique_ptr<eve::IPreparedGameRestore>>::success(nullptr);
        class Prepared final : public eve::IPreparedGameRestore {
        public:
            Prepared(FakeParticipant& owner, int next) : owner_(owner), next_(next) {}
            void commit() noexcept override { owner_.value = next_; }

        private:
            FakeParticipant& owner_;
            int              next_;
        };
        std::unique_ptr<eve::IPreparedGameRestore> prepared =
            std::make_unique<Prepared>(*this, static_cast<int>(state.asInt()));
        return eve::Result<std::unique_ptr<eve::IPreparedGameRestore>>::success(std::move(prepared));
    }
    int  value      = 0;
    bool reject     = false;
    bool returnNull = false;

private:
    std::string key_;
};

}  // namespace

TEST_CASE("gameSave.failedLaterPrepareLeavesEarlierParticipantUnchanged") {
    FakeParticipant      first("rpg", 25);
    FakeParticipant      second("tactics", 3);
    eve::GameSaveSession session("game-content-v1");
    REQUIRE(session.add(first).ok());
    REQUIRE(session.add(second).ok());
    auto saved = session.snapshotJson(testHash());
    REQUIRE(saved.ok());

    first.value   = 99;
    second.value  = 9;
    second.reject = true;
    CHECK(!session.restoreJson(saved.value(), testHash()).ok());
    CHECK_EQ(first.value, 99);
    CHECK_EQ(second.value, 9);

    second.reject     = false;
    second.returnNull = true;
    CHECK(!session.restoreJson(saved.value(), testHash()).ok());
    CHECK_EQ(first.value, 99);
    second.returnNull = false;
    REQUIRE(session.restoreJson(saved.value(), testHash()).ok());
    CHECK_EQ(first.value, 25);
    CHECK_EQ(second.value, 3);
}

TEST_CASE("gameSave.absentProviderAndContentMigrationAreExplicit") {
    FakeParticipant      first("rpg", 25);
    FakeParticipant      second("tactics", 3);
    eve::GameSaveSession source("game-content-v1");
    REQUIRE(source.add(first).ok());
    REQUIRE(source.add(second).ok());
    auto saved = source.snapshotJson(testHash());
    REQUIRE(saved.ok());

    eve::GameSaveSession absent("game-content-v1");
    REQUIRE(absent.add(first).ok());
    first.value = 99;
    CHECK(!absent.restoreJson(saved.value(), testHash()).ok());
    CHECK_EQ(first.value, 99);

    eve::GameSaveSession migrated("game-content-v2");
    REQUIRE(migrated.add(first).ok());
    REQUIRE(migrated.add(second).ok());
    CHECK(!migrated.restoreJson(saved.value(), testHash()).ok());
    REQUIRE(migrated
                .addContentMigration("game-content-v1",
                                     [](const eve::Value& states) { return eve::Result<eve::Value>::success(states); })
                .ok());
    REQUIRE(migrated.restoreJson(saved.value(), testHash()).ok());
    CHECK_EQ(first.value, 25);
}
