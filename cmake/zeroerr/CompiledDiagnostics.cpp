#if !defined(ZEROERR_COMPILED_DIAGNOSTICS)
#error "CompiledDiagnostics.cpp requires ZEROERR_COMPILED_DIAGNOSTICS in library and consumers"
#endif

#include <iostream>
#include <sstream>

#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include "TestRegistration.h"
namespace zeroerr {
namespace detail {
void printAssertionMessage(assert_level level, const std::string& message) {
    switch (level) {
        case assert_level::ZEROERR_WARN_l: std::cerr << FgYellow << "WARN" << Reset; break;
        case assert_level::ZEROERR_ERROR_l: std::cerr << FgRed << "ERROR" << Reset; break;
        case assert_level::ZEROERR_FATAL_l: std::cerr << FgMagenta << "FATAL" << Reset; break;
    }
    std::cerr << message << std::endl;
}
}
Printer::Printer(std::ostream& os) : os(os) {}
Printer::Printer() : os(*new std::stringstream()) { use_stringstream = true; }
Printer::~Printer() {
    if (use_stringstream) delete &os;
}
void Printer::check_stream() {
    if (use_stringstream && clear_stream_before_printing) {
        auto& ss = static_cast<std::stringstream&>(os);
        ss.str(std::string());
        ss.clear();
    }
}
std::string Printer::str() const {
    if (use_stringstream == false) throw std::runtime_error("Printer is not using stringstream");
    return static_cast<std::stringstream&>(os).str();
}
AssertionData::AssertionData(const char* file, unsigned line, const char* cond, assert_info info)
    : file(file), line(line), info(info) {
    static std::string pattern = "zeroerr::ExpressionDecomposer() << ";
    std::string        cond_str(cond);
    size_t             pos = cond_str.find(pattern);
    if (pos != std::string::npos) cond_str.replace(pos, pos + pattern.size(), "");
    this->cond = cond_str;
}
void AssertionData::setResult(ExprResult&& result) {
    ExprResult r(std::move(result));
    if (info.is_false)
        passed = !r.res;
    else
        passed = r.res;
    message = r.decomp;
}
void AssertionData::setException(const std::exception& e) {
    passed  = info.throw_type == assert_throw::throws;
    message = e.what();
}
std::string AssertionData::log() {
    std::stringstream ss;
    ss << "    " << cond << "  expands to  " << message << std::endl;
    ss << Dim << "(" << file << ":" << line << ")" << Reset << std::endl;
    return ss.str();
}
void AssertionData::operator()() {
    if (passed) return;
    if (shouldThrow()) throw *this;
}
bool AssertionData::shouldThrow() { return info.level != assert_level::ZEROERR_WARN_l; }
ExprResult::ExprResult(bool res, std::string decomposition) : res(res), decomp(decomposition) {}
TestCase::TestCase(std::string name, std::string file, unsigned line, std::vector<Decorator*> decorators)
    : name(name), file(file), line(line), decorators(decorators) {}
TestCase::TestCase(std::string name, std::string file, unsigned line, std::function<void(TestContext*)> func,
                   std::vector<Decorator*> decorators)
    : name(name), file(file), line(line), func(func), decorators(decorators) {}
AssertionData::~AssertionData()                                   = default;
AssertionData::AssertionData(const AssertionData&)                = default;
AssertionData::AssertionData(AssertionData&&) noexcept            = default;
AssertionData& AssertionData::operator=(const AssertionData&)     = default;
AssertionData& AssertionData::operator=(AssertionData&&) noexcept = default;

ExprResult::~ExprResult()                                = default;
ExprResult::ExprResult(const ExprResult&)                = default;
ExprResult::ExprResult(ExprResult&&) noexcept            = default;
ExprResult& ExprResult::operator=(const ExprResult&)     = default;
ExprResult& ExprResult::operator=(ExprResult&&) noexcept = default;

TestCase::~TestCase()                              = default;
TestCase::TestCase(const TestCase&)                = default;
TestCase::TestCase(TestCase&&) noexcept            = default;
TestCase& TestCase::operator=(const TestCase&)     = default;
TestCase& TestCase::operator=(TestCase&&) noexcept = default;

namespace detail {
std::string formatPrintedArguments(const char* fmt, const std::string* args) {
    std::stringstream ss;
    bool              parse_name = false;
    int               j          = 0;
    for (const char* i = fmt; *i != '\0'; i++) {
        switch (*i) {
            case '{': parse_name = true; break;
            case '}':
                parse_name = false;
                ss << args[j++];
                break;
            default:
                if (!parse_name) ss << *i;
                break;
        }
    }
    return ss.str();
}
TestCase makeTestCase(std::string name, std::string file, unsigned line, void (*function)(TestContext*),
                      std::vector<Decorator*> decorators) {
    return TestCase(std::move(name), std::move(file), line, std::function<void(TestContext*)>(function),
                    std::move(decorators));
}
}  // namespace detail
}  // namespace zeroerr

namespace zeroerr::detail {
TestCase makeTestCaseFromDecorators(std::string_view name, std::string_view file, unsigned line,
                                    void (*function)(TestContext*), std::initializer_list<Decorator*> decorators) {
    return makeTestCase(std::string(name), std::string(file), line, function, std::vector<Decorator*>(decorators));
}
}  // namespace zeroerr::detail

namespace zeroerr {
std::string Printer::tab(unsigned level) { return std::string((isCompact ? 0 : level * indent), ' '); }
const char* Printer::quote() { return isQuoted ? "\"" : ""; }

std::string Printer::demangle(const char* name) {
#ifdef __GNUG__
    int         status = -4;
    char*       res    = abi::__cxa_demangle(name, NULL, NULL, &status);
    std::string ret    = (status == 0) ? res : name;
    std::free(res);
    return ret;
#else
    return name;
#endif
}

Printer::operator std::string() const { return str(); }
}  // namespace zeroerr

namespace zeroerr {
template std::string format<std::string>(const char* fmt, std::string&& args);
}  // namespace zeroerr

namespace zeroerr {
template <>
Expression<bool>::operator ExprResult() {
    if (decomp.empty()) {
        Printer print;
        print.isCompact  = true;
        print.line_break = "";
        decomp           = print(lhs).str();
    }
    return ExprResult(res, decomp);
}
}  // namespace zeroerr

namespace zeroerr {
template void Printer::print<int>(int value, unsigned level, const char* lb, rank<0>);
template void Printer::print<unsigned int>(unsigned int value, unsigned level, const char* lb, rank<0>);
template void Printer::print<long>(long value, unsigned level, const char* lb, rank<0>);
template void Printer::print<unsigned long>(unsigned long value, unsigned level, const char* lb, rank<0>);
template void Printer::print<long long>(long long value, unsigned level, const char* lb, rank<0>);
template void Printer::print<unsigned long long>(unsigned long long value, unsigned level, const char* lb, rank<0>);
template void Printer::print<float>(float value, unsigned level, const char* lb, rank<0>);
template void Printer::print<double>(double value, unsigned level, const char* lb, rank<0>);
template void Printer::print<const char*>(const char* value, unsigned level, const char* lb, rank<4>);
template void Printer::print<std::string>(std::string value, unsigned level, const char* lb, rank<4>);
}  // namespace zeroerr

namespace zeroerr {
template Expression<bool> ExpressionDecomposer::operator<< <bool>(bool&& operand);
}
