// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"role":"tests","default":"local-logic","module":"tensor"}
#include "tensor/OnnxCompiler.h"
#include "zeroerr/unittest.h"

namespace {
std::mutex              gateMutex;
std::condition_variable gateChanged;
unsigned                entered  = 0;
bool                    released = false;

struct Observation {
    bool   admitted;
    size_t peak;
    bool   wordsPreserved;
};

Observation observeQueue(unsigned workers, unsigned expected) {
    entered  = 0;
    released = false;
    eve::tensor::onnx_detail::CompilerQueue                             queue(workers);
    std::vector<std::future<eve::tensor::onnx_detail::CompiledProgram>> futures;
    for (unsigned i = 0; i < 4; ++i) futures.push_back(queue.enqueue("controlled"));
    bool admitted;
    {
        std::unique_lock lock(gateMutex);
        admitted = gateChanged.wait_for(lock, std::chrono::seconds(2), [expected] { return entered >= expected; });
        released = true;
    }
    gateChanged.notify_all();
    bool wordsPreserved = true;
    for (auto& future : futures) wordsPreserved &= future.get().words == std::vector<uint32_t>{0x07230203u};
    queue.waitIdle();
    return {admitted, queue.peakWorkers(), wordsPreserved};
}
}  // namespace

// This isolated executable provides a controlled compiler double. No GPU or
// shaderc implementation is linked here. The real shader/output/cache contract
// remains in tensor.onnx.gpu.parallelCompilation, without test synchronization.
namespace eve::gpgpu {
Result<std::vector<uint32_t>> compileComputeSpirv(const std::string&) {
    std::unique_lock lock(gateMutex);
    ++entered;
    gateChanged.notify_all();
    gateChanged.wait(lock, [] { return released; });
    return Result<std::vector<uint32_t>>::success({0x07230203u});
}
}  // namespace eve::gpgpu

TEST_CASE("tensor.onnx.compilerQueue.configuredCapacity") {
    for (unsigned workers : {1u, 4u}) {
        const auto observation = observeQueue(workers, workers);
        REQUIRE(observation.admitted);
        REQUIRE(observation.peak == workers);
        CHECK(observation.wordsPreserved);
    }
}

TEST_CASE("tensor.onnx.compilerQueue.rejectsSerialAdmission") {
    const auto observation = observeQueue(1, 4);
    REQUIRE(!observation.admitted);
    REQUIRE(observation.peak == 1);
    CHECK(observation.wordsPreserved);
}

int main(int argc, char** argv) { return zeroerr::UnitTest().parseArgs(argc, const_cast<const char**>(argv)).run(); }
