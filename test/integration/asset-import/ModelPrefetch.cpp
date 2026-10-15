#include <future>
#include <thread>
#include <vector>
#include "common/AsyncWork.h"
#include "common/Capability.h"
#include "common/Resource.h"
#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
struct ExecutorScope {
    eve::caps::IAsyncWorkExecutor* previous = eve::cap::query<eve::caps::IAsyncWorkExecutor>();
    explicit ExecutorScope(eve::caps::IAsyncWorkExecutor* current) {
        if (previous) eve::cap::revoke(previous);
        if (current) eve::cap::provide(current);
    }
    ~ExecutorScope() { eve::cap::provide(previous); }
};
class Worker final : public eve::caps::IAsyncWorkExecutor {
public:
    std::vector<std::future<void>> jobs;
    std::thread::id                workerId;
    eve::Result<void>              submit(std::function<void()> work) override {
        jobs.push_back(std::async(std::launch::async, [this, work = std::move(work)] {
            workerId = std::this_thread::get_id();
            work();
        }));
        return eve::Result<void>::success();
    }
    void join() {
        for (auto& job : jobs)
            if (job.valid()) job.get();
    }
    ~Worker() { join(); }
};
}  // namespace

TEST_CASE("model3d.prefetch rejects missing executor and empty paths") {
    ExecutorScope scope(nullptr);
    auto*         models = eve::model3d::Model3D::create();
    auto          empty  = models->requestModelData("");
    REQUIRE(!empty.ok());
    auto unsupported = models->requestModelData("prefetch.obj");
    REQUIRE(!unsupported.ok());
}

TEST_CASE("model3d.prefetch decodes on worker and joins the canonical cache") {
    auto* fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("eve_model_prefetch_contract", true));
    REQUIRE(fs->setupWriteDirectory());
    const std::string obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    fs->write("prefetch.obj", obj.data(), obj.size());
    Worker        worker;
    ExecutorScope scope(&worker);
    auto*         models = eve::model3d::Model3D::create();
    REQUIRE(models->requestModelData("prefetch.obj").ok());
    REQUIRE(models->requestModelData("prefetch.obj").ok());
    auto* first = models->newModelDataFromFile("prefetch.obj");
    REQUIRE(first != nullptr);
    REQUIRE(first->getMeshCount() == 1);
    REQUIRE(models->newModelDataFromFile("prefetch.obj") == first);
    worker.join();
    REQUIRE(worker.jobs.size() == 1);
    REQUIRE(worker.workerId != std::this_thread::get_id());
    eve::model3d::ModelLoadOptions options;
    options.joinIdenticalVertices = false;
    options.improveCacheLocality  = false;
    REQUIRE(models->requestModelData("prefetch.obj", options).ok());
    REQUIRE(models->requestModelData("prefetch.obj", options).ok());
    auto* explicitOptions = models->newModelDataFromFile("prefetch.obj", options);
    REQUIRE(explicitOptions != first);
    REQUIRE(explicitOptions->getMeshCount() == 1);
    REQUIRE(models->newModelDataFromFile("prefetch.obj", options) == explicitOptions);
    worker.join();
    REQUIRE(worker.jobs.size() == 2);
    REQUIRE(models->requestModelData("missing-prefetch-model.glb").ok());
    bool reportedFailure = false;
    try {
        models->newModelDataFromFile("missing-prefetch-model.glb");
    } catch (const std::exception&) {
        reportedFailure = true;
    }
    worker.join();
    REQUIRE(reportedFailure);
    REQUIRE(eve::ResourceManager::getInstance().pendingCount() == 0);
}
