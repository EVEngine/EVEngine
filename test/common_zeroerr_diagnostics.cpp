#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(std::is_nothrow_move_constructible_v<zeroerr::ExprResult>);
static_assert(std::is_nothrow_move_assignable_v<zeroerr::ExprResult>);
static_assert(std::is_nothrow_move_constructible_v<zeroerr::TestCase>);
static_assert(std::is_nothrow_move_assignable_v<zeroerr::TestCase>);
static_assert(std::is_nothrow_move_constructible_v<zeroerr::AssertionData>);
static_assert(std::is_nothrow_move_assignable_v<zeroerr::AssertionData>);

TEST_CASE("common.zeroerr.preservesExpressionAndSource") {
    const unsigned         line = __LINE__;
    zeroerr::assert_info   info{zeroerr::assert_level::ZEROERR_FATAL_l, zeroerr::assert_throw::no_throw, false};
    zeroerr::AssertionData data(__FILE__, line, "left == right", info);
    int                    left = 2, right = 3;
    data.setResult(zeroerr::ExpressionDecomposer() << left == right);
    CHECK(!data.passed);
    CHECK(data.line == line);
    CHECK(data.file == std::string(__FILE__));
    CHECK(data.cond == "left == right");
    CHECK(data.message.find("2") != std::string::npos);
    CHECK(data.message.find("3") != std::string::npos);
    CHECK(data.log().find("left == right") != std::string::npos);
    bool caught = false;
    try {
        data();
    } catch (const zeroerr::AssertionData& failure) {
        caught = true;
        CHECK(failure.cond == data.cond);
    }
    CHECK(caught);
}

TEST_CASE("common.zeroerr.preservesAssertionCounters") {
    const auto before = _ZEROERR_TEST_CONTEXT->passed_as;
    CHECK(2 + 3 == 5);
    const auto after = _ZEROERR_TEST_CONTEXT->passed_as;
    CHECK(after == before + 1);
    auto knownFailure = [reporter = &_ZEROERR_TEST_CONTEXT->reporter] {
        zeroerr::TestContext context(*reporter);
        auto*                _ZEROERR_TEST_CONTEXT = &context;
        CHECK(2 == 3);
        return context.warning_as;
    };
    CHECK(knownFailure() == 1);
}

struct CompiledFixture {
    int value = 7;
};
TEST_CASE_FIXTURE(CompiledFixture, "common.zeroerr.preservesFixtureRegistration") { CHECK(value == 7); }

TEST_CASE("common.zeroerr.expressionValueLifecycle") {
    zeroerr::Expression<bool> plain(true);
    zeroerr::ExprResult       booleanResult = plain;
    CHECK(booleanResult.res);
    CHECK(booleanResult.decomp == "true");
    plain.lhs                        = false;
    zeroerr::ExprResult cachedResult = plain;
    CHECK(cachedResult.decomp == "true");
    zeroerr::Expression<bool> described(false, false, std::string("known decomposition"));
    zeroerr::ExprResult       describedResult = described;
    CHECK(!describedResult.res);
    CHECK(describedResult.decomp == "known decomposition");

    zeroerr::ExprResult original(true, "original decomposition");
    zeroerr::ExprResult copied(original);
    original.decomp = "changed";
    CHECK(copied.decomp == "original decomposition");
    zeroerr::ExprResult moved(std::move(copied));
    CHECK(moved.res);
    CHECK(moved.decomp == "original decomposition");
    zeroerr::ExprResult assigned(false);
    assigned = moved;
    CHECK(assigned.decomp == moved.decomp);
    assigned = std::move(moved);
    CHECK(assigned.res);
    copied = zeroerr::ExprResult(false, "reused");
    CHECK(!copied.res);
    CHECK(copied.decomp == "reused");
}

TEST_CASE("common.zeroerr.testMetadataLifecycle") {
    auto               payload = std::make_shared<int>(0);
    std::weak_ptr<int> lifetime(payload);
    {
        zeroerr::TestCase child("child", __FILE__, __LINE__, {});
        zeroerr::TestCase original("original", __FILE__, __LINE__, [payload](zeroerr::TestContext*) { ++*payload; },
                                   {});
        original.subcases.push_back(&child);
        original.decorators.push_back(zeroerr::skip());
        zeroerr::TestCase copied(original);
        original.name = "changed";
        CHECK(copied.name == "original");
        CHECK(copied.subcases == original.subcases);
        CHECK(copied.decorators == original.decorators);
        copied.func(_ZEROERR_TEST_CONTEXT);
        zeroerr::TestCase moved(std::move(copied));
        moved.func(_ZEROERR_TEST_CONTEXT);
        zeroerr::TestCase assigned("empty", __FILE__, __LINE__, {});
        assigned = original;
        assigned.func(_ZEROERR_TEST_CONTEXT);
        assigned = std::move(moved);
        assigned.func(_ZEROERR_TEST_CONTEXT);
        CHECK(assigned.name == "original");
        CHECK(assigned.subcases.front() == &child);
        CHECK(*payload == 4);
        payload.reset();
        CHECK(!lifetime.expired());
    }
    CHECK(lifetime.expired());
}

TEST_CASE("common.zeroerr.assertionExceptionLifecycle") {
    const unsigned         line = __LINE__;
    zeroerr::assert_info   info{zeroerr::assert_level::ZEROERR_FATAL_l, zeroerr::assert_throw::no_throw, false};
    zeroerr::AssertionData original(__FILE__, line, "condition", info);
    original.setResult(zeroerr::ExprResult(false, "decomposition"));
    zeroerr::AssertionData copied(original);
    original.message = "changed";
    CHECK(copied.message == "decomposition");
    zeroerr::AssertionData moved(std::move(copied));
    CHECK(moved.file == std::string(__FILE__));
    CHECK(moved.line == line);
    zeroerr::AssertionData assigned(__FILE__, line, "empty", info);
    assigned.setResult(zeroerr::ExprResult(true));
    assigned = moved;
    CHECK(assigned.message == "decomposition");
    assigned = std::move(moved);
    CHECK(!assigned.passed);
    bool caught = false;
    try {
        assigned();
    } catch (const zeroerr::AssertionData& failure) {
        caught = true;
        CHECK(failure.cond == "condition");
        CHECK(failure.message == "decomposition");
        CHECK(failure.file == std::string(__FILE__));
    }
    CHECK(caught);
    std::unique_ptr<std::exception> erased = std::make_unique<zeroerr::AssertionData>(assigned);
    erased.reset();
}

namespace {
struct OrderedPrintValue {
    int               id;
    std::vector<int>* calls;
    bool              fail = false;
};
std::ostream& operator<<(std::ostream& stream, const OrderedPrintValue& value) {
    value.calls->push_back(value.id);
    if (value.fail) throw std::runtime_error("printing failed");
    return stream << value.id;
}
}  // namespace

TEST_CASE("common.zeroerr.formatPreservesGrammarAndOrder") {
    std::vector<int>  calls;
    OrderedPrintValue first{1, &calls}, second{2, &calls}, unused{3, &calls};
    auto              text = zeroerr::format("{left}/{right}", first, second, unused);
    CHECK(text == "1/2");
    CHECK(calls == std::vector<int>({1, 2, 3}));
    CHECK(zeroerr::format("prefix {unfinished", "ignored") == "prefix ");
    CHECK(zeroerr::format("{{nested} tail", "value") == "value tail");
    CHECK(zeroerr::format("plain text", "unused") == "plain text");
    CHECK(zeroerr::format("{}:{}", "unquoted", 7) == "unquoted:7");
    CHECK(zeroerr::format("{value}", std::string("owned string")) == "owned string");
    CHECK(zeroerr::format("{}", std::string(4096, 'x')) == std::string(4096, 'x'));
    CHECK(zeroerr::format("{}", std::string("a\0b", 3)) == std::string("a\0b", 3));
    calls.clear();
    second.fail = true;
    bool caught = false;
    try {
        (void)zeroerr::format("{}", first, second, unused);
    } catch (const std::runtime_error& error) {
        caught = std::string(error.what()) == "printing failed";
    }
    CHECK(caught);
    CHECK(calls == std::vector<int>({1, 2}));
}

TEST_CASE("common.zeroerr.printerOwnsAndResetsStream") {
    zeroerr::Printer print;
    print.isQuoted   = false;
    print.line_break = "";
    CHECK(print("first").str() == "first");
    print.os.setstate(std::ios::failbit);
    CHECK(print("second").str() == "second");
    print.clear_stream_before_printing = false;
    CHECK(print("third").str() == "secondthird");
    const auto& constPrint = print;
    CHECK(static_cast<std::string>(constPrint) == "secondthird");
    std::ostringstream copied;
    copied << print;
    CHECK(copied.str() == "secondthird");
}

TEST_CASE("common.zeroerr.printerBorrowsExternalStream") {
    std::ostringstream stream;
    {
        zeroerr::Printer print(stream);
        print.isQuoted   = false;
        print.line_break = "";
        print("first");
        print("second");
        CHECK(stream.str() == "firstsecond");
        bool caught = false;
        try {
            (void)print.str();
        } catch (const std::runtime_error& error) {
            caught = std::string(error.what()) == "Printer is not using stringstream";
        }
        CHECK(caught);
    }
    stream << "after destruction";
    CHECK(stream.str() == "firstsecondafter destruction");
}

TEST_CASE("common.zeroerr.printerFormattingModes") {
    zeroerr::Printer print;
    print.indent    = 3;
    print.isCompact = false;
    CHECK(print.tab(2) == "      ");
    print.isCompact = true;
    CHECK(print.tab(2).empty());
    print.isQuoted = true;
    CHECK(std::string(print.quote()) == "\"");
    print.isQuoted = false;
    CHECK(std::string(print.quote()).empty());
    CHECK(zeroerr::Printer::demangle("plain-type-name") == "plain-type-name");
#ifdef __GNUG__
    CHECK(zeroerr::Printer::demangle("i") == "int");
#else
    CHECK(zeroerr::Printer::demangle("i") == "i");
#endif
}
