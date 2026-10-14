#include <zeroerr/unittest.h>
#include <type_traits>
#include "common/Diagnostic.h"
#include "common/Result.h"

static_assert(std::is_nothrow_move_constructible_v<eve::Status>);
static_assert(std::is_nothrow_move_assignable_v<eve::Status>);
static_assert(std::is_nothrow_default_constructible_v<eve::Status>);

TEST_CASE("common.diagnosticStorage.omittedAndExplicitDetailsMatch") {
    using namespace eve;
    Diagnostic simple(DiagnosticCode::InvalidArgument, Severity::Warning, "bad", "field");
    Diagnostic explicitEmpty(DiagnosticCode::InvalidArgument, Severity::Warning, "bad", "field", {});
    CHECK(simple.code() == explicitEmpty.code());
    CHECK(simple.severity() == explicitEmpty.severity());
    CHECK(simple.message() == explicitEmpty.message());
    CHECK(simple.path() == explicitEmpty.path());
    CHECK(simple.details().empty());
    CHECK(simple.source().empty());
    CHECK(Diagnostic::error(DiagnosticCode::Failed, "error").severity() == Severity::Error);
    CHECK(Diagnostic::warning(DiagnosticCode::Failed, "warning", "path").severity() == Severity::Warning);
    auto full = Diagnostic::error(DiagnosticCode::Failed, "error", "path", {{"a", "1"}, {"a", "2"}}, "test");
    REQUIRE(full.details().size() == 2u);
    CHECK(full.details()[1].second == "2");
    CHECK(full.source() == "test");
    full.addDetail("b", "3");
    CHECK(full.details().size() == 3u);
}

TEST_CASE("common.diagnosticStorage.statusCopyAndMoveOwnIndependentDiagnostics") {
    using namespace eve;
    auto status = Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "bad", "field"));
    CHECK(status.code() == StatusCode::Rejected);
    auto copy = status;
    status    = Status::success(StatusCode::Applied);
    CHECK(status.code() == StatusCode::Applied);
    CHECK(copy.describe() == "rejected: bad [field]");
    Status assigned;
    assigned = copy;
    copy     = Status::success();
    CHECK(assigned.diagnostics().size() == 1u);
    Status moved(std::move(assigned));
    CHECK(moved.code() == StatusCode::Rejected);
    Status destination;
    destination = std::move(moved);
    CHECK(destination.describe() == "rejected: bad [field]");
    CHECK(Status(StatusCode::Conflict, {Diagnostic::error(DiagnosticCode::Conflict, "collision")}).code() ==
          StatusCode::Conflict);
    auto result = Result<int>::failure(std::move(destination));
    CHECK(!result.ok());
    CHECK(result.code() == StatusCode::Rejected);
}
