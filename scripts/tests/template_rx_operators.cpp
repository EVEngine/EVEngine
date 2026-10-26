// Standalone regression/codegen probe; no graphics, Squirrel or zeroerr needed.
#include "rx/Rx.h"

#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace eve::rx;

static Observable<int>* makeOperator(int kind, Subject<int>& source, Subject<int>& stop) {
    switch (kind) {
        case 0: return source.filter([](const int& v) { return v % 2 == 0; });
        case 1: return source.take(2);
        case 2: return source.skip(1);
        case 3: return source.first();
        case 4: return source.takeUntil(&stop);
        default: return source.distinctUntilChanged();
    }
}

int main() {
    const std::vector<std::vector<int>> expected = {
        {2, 2}, {1, 2}, {2, 2, 3}, {1}, {1, 2, 2, 3}, {1, 2, 3}
    };
    for (int kind = 0; kind < 6; ++kind) {
        // Value filtering/transformation and cancellation remain unchanged.
        {
            Subject<int> source, stop;
            std::unique_ptr<Observable<int>> op(makeOperator(kind, source, stop));
            std::vector<int> values;
            auto sub = op->subscribe([&](const int& v) { values.push_back(v); });
            for (int v : {1, 2, 2, 3}) source.onNext(v);
            assert(values == expected[kind]);
            sub.dispose();
            source.onNext(4);
            assert(values == expected[kind]);
        }
        for (bool error : {false, true}) {
            Subject<int> source, stop;
            std::unique_ptr<Observable<int>> op(makeOperator(kind, source, stop));
            int errors = 0, completed = 0;
            Observer<int> out;
            out.onError = [&](const std::string& e) { assert(e == "failure"); ++errors; };
            out.onCompleted = [&] { ++completed; };
            auto a = op->subscribe(out);
            auto b = op->subscribe(out);
            if (error) source.onError("failure");
            else source.onCompleted();
            assert(errors == (error ? 2 : 0));
            assert(completed == (error ? 0 : 2));
        }
    }

    // map must use the output Observer<string>, not the input Observer<int>.
    for (bool error : {false, true}) {
        Subject<int> source;
        std::unique_ptr<Observable<std::string>> op(
            source.map<std::string>([](const int& v) { return std::to_string(v); }));
        std::string value;
        int errors = 0, completed = 0;
        auto sub = op->subscribe([&](const std::string& v) { value = v; },
                                [&](const std::string& e) { assert(e == "mapped"); ++errors; },
                                [&] { ++completed; });
        source.onNext(42);
        assert(value == "42");
        if (error) source.onError("mapped"); else source.onCompleted();
        assert(errors == (error ? 1 : 0));
        assert(completed == (error ? 0 : 1));
    }

    // Stopping still cancels upstream; stopper callbacks carry more than out.
    {
        Subject<int> source, stop;
        std::unique_ptr<Observable<int>> op(source.takeUntil(&stop));
        int completed = 0;
        auto sub = op->subscribe([](const int&) {}, [](const std::string&) {}, [&] { ++completed; });
        stop.onNext(1);
        assert(completed == 1 && !source.hasObservers());
    }

    // Preserve the existing independent Observer copies in terminal closures.
    // This deliberately records current semantics rather than fixing Rx here.
    {
        Subject<int> source;
        std::unique_ptr<Observable<int>> op(source.first());
        int completed = 0;
        auto sub = op->subscribe([](const int&) {}, [](const std::string&) {}, [&] { ++completed; });
        source.onNext(1);
        source.onCompleted();
        assert(completed == 2);
    }

    // Terminal callback exceptions must propagate through the shared functors.
    {
        Subject<int> source;
        std::unique_ptr<Observable<int>> op(source.skip(1));
        auto sub = op->subscribe([](const int&) {}, [](const std::string&) {
            throw std::runtime_error("terminal callback");
        }, [] {});
        bool caught = false;
        try { source.onError("failure"); } catch (const std::runtime_error&) { caught = true; }
        assert(caught);
    }
}
