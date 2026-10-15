#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Result.h"

#include <memory>
#include <string>
#include <type_traits>

using eve::Diagnostic;
using eve::DiagnosticCode;
using eve::Result;
using eve::Severity;
using eve::Status;
using eve::StatusCode;

static_assert(!std::is_copy_constructible_v<Result<int>>);
static_assert(!std::is_copy_assignable_v<Result<int>>);
static_assert(std::is_move_constructible_v<Result<int>>);
static_assert(!std::is_copy_constructible_v<Result<void>>);
static_assert(std::is_convertible_v<Result<int>, Result<const int>>);
static_assert(!std::is_convertible_v<Result<const int>, Result<int>>);
static_assert(std::is_convertible_v<Result<int*>, Result<const int*>>);
static_assert(!std::is_convertible_v<Result<const int*>, Result<int*>>);
static_assert(!std::is_convertible_v<Result<void>, Result<int>>);
static_assert(!std::is_convertible_v<Result<int>, Result<void>>);

TEST_CASE("common.result.successAndObservationAccessors") {
    auto result = Result<int>::success(42);

    CHECK(result.ok());
    CHECK(result.hasValue());
    CHECK(result.code() == StatusCode::Ok);
    CHECK(result.value() == 42);
    CHECK(result.status().isSuccess());
    CHECK(result.diagnostics().empty());
    CHECK(result.error() == nullptr);
}

TEST_CASE("common.result.failureCarriesStructuredDiagnostic") {
    auto diagnostic = Diagnostic::error(DiagnosticCode::InvalidArgument, "width must be positive", "window.width");
    diagnostic.addDetail("received", "0");
    auto result = Result<int>::failure(std::move(diagnostic));

    CHECK(!result.ok());
    CHECK(result.code() == StatusCode::Rejected);
    REQUIRE(result.error() != nullptr);
    CHECK(result.error()->code() == DiagnosticCode::InvalidArgument);
    CHECK(result.error()->severity() == Severity::Error);
    CHECK(result.error()->message() == "width must be positive");
    CHECK(result.error()->path() == "window.width");
    REQUIRE(result.error()->details().size() == 1u);
    CHECK(result.error()->details()[0].first == "received");
    CHECK(result.error()->details()[0].second == "0");
    CHECK(result.status().describe() == "rejected: width must be positive [window.width]");
}

TEST_CASE("common.result.ignoreAndExpectAreExplicitConsumption") {
    Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "best effort cleanup"))
        .ignore("cleanup is best effort");

    const int value = Result<int>::success(7).expect("initialization must succeed");
    CHECK(value == 7);

    CHECK_THROWS((Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "not available"))
                      .expect("feature must be available"),
                  0));
}

TEST_CASE("common.result.unobservedDestructionIsCaughtInDebug") {
#if !defined(ZEROERR_NO_ASSERT)
    CHECK_THROWS((
        [] {
            auto result = Result<int>::success(1);
            (void)result;
        }(),
        0));
#else
    CHECK(true);
#endif
}

TEST_CASE("common.result.moveTransfersObservationResponsibility") {
    auto source = Result<std::unique_ptr<int>>::success(std::make_unique<int>(9));
    auto moved  = std::move(source);
    REQUIRE(moved.ok());
    REQUIRE(moved.value().get() != nullptr);
    CHECK(*moved.value() == 9);

    auto assignedSource = Result<int>::success(11);
    auto assignedTarget = Result<int>::success(13);
    CHECK(assignedTarget.ok());
    assignedTarget = std::move(assignedSource);
    CHECK(assignedTarget.value() == 11);
}

TEST_CASE("common.result.moveAssignmentDoesNotOverwriteUncheckedTarget") {
#if !defined(ZEROERR_NO_ASSERT)
    auto target = Result<int>::success(1);
    auto source = Result<int>::success(2);
    CHECK_THROWS((target = std::move(source), 0));
    target.ignore("retain target after rejected move assignment");
    source.ignore("retain source after rejected move assignment");
#else
    CHECK(true);
#endif
}

TEST_CASE("common.result.compositionPreservesFailureStatus") {
    auto success = Result<int>::success(3).andThen(
        [](int value) { return Result<std::string>::success(std::to_string(value + 1)); });
    REQUIRE(success.ok());
    CHECK(success.value() == "4");

    auto failure   = Result<int>::failure(Diagnostic::error(DiagnosticCode::NotFound, "entity not found"));
    auto recovered = std::move(failure).orElse([&](const Status& status) {
        CHECK(status.code() == StatusCode::NotFound);
        return Result<int>::success(99);
    });
    CHECK(recovered.value() == 99);
}

TEST_CASE("common.result.constConversionConstrainsValue") {
    auto makeConstValue = []() -> Result<const int> { return Result<int>::success(4); };
    auto constValue     = makeConstValue();
    REQUIRE(constValue.ok());
    CHECK(constValue.value() == 4);

    int  storage        = 8;
    auto makeConstPointer = [](int* pointer) -> Result<const int*> { return Result<int*>::success(pointer); };
    auto constPointer     = makeConstPointer(&storage);
    REQUIRE(constPointer.ok());
    CHECK(*constPointer.value() == 8);

    auto assigned = Result<const int>::failure(Diagnostic::error(DiagnosticCode::Failed, "placeholder"));
    CHECK(!assigned.ok());
    assigned = Result<int>::success(11);
    CHECK(assigned.value() == 11);
    assigned = Result<const int>::success(13);
    CHECK(assigned.value() == 13);

    auto makeConstFailure = []() -> Result<const int> {
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::NotFound, "missing"));
    };
    auto failed = makeConstFailure();
    CHECK(!failed.ok());
    CHECK(failed.code() == StatusCode::NotFound);
}

TEST_CASE("common.result.voidStatusAndBoolCheck") {
    auto result = Result<void>::success(Status::success(StatusCode::Applied));
    CHECK(static_cast<bool>(result));
    CHECK(result.code() == StatusCode::Applied);
    result.value();

    auto failed = Result<void>::failure(
        Status::failure(StatusCode::Conflict, Diagnostic::error(DiagnosticCode::Conflict, "revision changed")));
    CHECK(!failed);
    REQUIRE(failed.error() != nullptr);
    CHECK(failed.error()->code() == DiagnosticCode::Conflict);
}
