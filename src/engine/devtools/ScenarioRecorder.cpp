#include "devtools/ScenarioRecorder.h"
#include "devtools/Snapshot.hpp"

#include "common/Module.h"
#include "platform_event/PlatformEvent.h"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <fstream>
#include <sstream>

namespace eve::dev {

ScenarioRecorder& ScenarioRecorder::instance() {
    static ScenarioRecorder inst;
    return inst;
}

void ScenarioRecorder::setObserver() {
    detachObserver();
    eventModule_ = eve::ModuleManager::requireInstance<eve::platform_event::PlatformEvent>("PlatformEvent");
    if (!eventModule_) return;
    savedObserver_ = eventModule_->pollObserver();
    eventModule_->setPollObserver(
        [this](const eve::platform_event::Message& m) { onEventConsumed(m); });
}

void ScenarioRecorder::detachObserver() {
    if (!eventModule_) return;
    eventModule_->setPollObserver(savedObserver_);
    eventModule_   = nullptr;
    savedObserver_ = {};
}

bool ScenarioRecorder::begin(HSQUIRRELVM vm, std::string* err) {
    if (!vm) {
        if (err) *err = "null VM";
        return false;
    }
    cancel();
    const std::string base = Snapshot::instance().capture(vm, err);
    if (base.empty()) return false;
    vm_          = vm;
    baseline_    = base;
    frames_.clear();
    errorReport_.clear();
    errorSite_.clear();
    currentFrame_ = 0;
    recording_   = true;
    setObserver();
    return true;
}

void ScenarioRecorder::markFrame() {
    if (!recording_) return;
    ++currentFrame_;
    if (frames_.empty() || frames_.back().frame != currentFrame_)
        frames_.push_back(ScenarioFrame{currentFrame_});
}

bool ScenarioRecorder::end(const std::string& path, std::string* err) {
    if (!recording_) {
        if (err) *err = "not recording";
        return false;
    }
    recording_ = false;
    detachObserver();
    const bool ok = save(baseline_, frames_, errorReport_, errorSite_, path, err);
    vm_ = nullptr;
    return ok;
}

void ScenarioRecorder::cancel() {
    recording_ = false;
    replay_    = false;
    detachObserver();
    vm_ = nullptr;
    baseline_.clear();
    frames_.clear();
    errorReport_.clear();
    errorSite_.clear();
    currentFrame_ = 0;
    replayIndex_  = 0;
}

void ScenarioRecorder::setErrorInfo(const std::string& report, const std::string& site) {
    if (!recording_) return;
    if (!report.empty()) errorReport_ = report;
    if (!site.empty()) errorSite_ = site;
}

void ScenarioRecorder::onEventConsumed(const eve::platform_event::Message& msg) {
    if (!recording_ || !vm_) return;
    ScenarioEvent ev;
    ev.name = msg.name;
    for (const auto& a : msg.args) {
        switch (a.type) {
            case eve::platform_event::Variant::Type::String: ev.args.push_back(a.s); break;
            case eve::platform_event::Variant::Type::Int:
                ev.args.push_back(std::to_string(static_cast<long long>(a.i)));
                break;
            case eve::platform_event::Variant::Type::Nil: ev.args.push_back({}); break;
            default:
                // Ptr payloads (borrowed/owned native pointers) are not serializable; drop.
                break;
        }
    }
    if (frames_.empty() || frames_.back().frame != currentFrame_)
        frames_.push_back(ScenarioFrame{currentFrame_});
    frames_.back().events.push_back(std::move(ev));
}

bool ScenarioRecorder::beginReplay(HSQUIRRELVM vm, const std::string& path, std::string* err) {
    if (!vm) {
        if (err) *err = "null VM";
        return false;
    }
    std::string base, er, es;
    std::vector<ScenarioFrame> frames;
    if (!load(path, &base, &frames, &er, &es, err)) return false;
    if (!Snapshot::instance().restore(vm, base, err)) return false;
    cancel();
    vm_          = vm;
    baseline_    = base;
    frames_      = std::move(frames);
    errorReport_ = er;
    errorSite_   = es;
    replay_      = true;
    replayIndex_ = 0;
    // Acquire the event module for input injection (observer stays detached).
    eventModule_ = eve::ModuleManager::requireInstance<eve::platform_event::PlatformEvent>("PlatformEvent");
    return true;
}

bool ScenarioRecorder::stageFrame() {
    if (!replay_ || !eventModule_) return false;
    if (replayIndex_ >= static_cast<int>(frames_.size())) {
        replay_ = false;
        return false;
    }
    const ScenarioFrame& frame = frames_[replayIndex_++];
    for (const auto& ev : frame.events) {
        std::vector<eve::platform_event::Variant> args;
        for (const auto& a : ev.args)
            args.push_back(eve::platform_event::Variant::makeString(a));
        eventModule_->push(new eve::platform_event::Message(ev.name, args));
    }
    return true;
}

bool ScenarioRecorder::save(const std::string& baseline,
                            const std::vector<ScenarioFrame>& frames,
                            const std::string& errorReport, const std::string& errorSite,
                            const std::string& path, std::string* err) {
    try {
        Poco::JSON::Object root;
        root.set("version", "1");
        root.set("baseline", baseline);
        Poco::JSON::Array framesArr;
        for (const auto& f : frames) {
            Poco::JSON::Object fo;
            fo.set("frame", f.frame);
            Poco::JSON::Array evs;
            for (const auto& e : f.events) {
                Poco::JSON::Object eo;
                eo.set("name", e.name);
                Poco::JSON::Array args;
                for (const auto& a : e.args)
                    args.add(a);
                eo.set("args", args);
                evs.add(eo);
            }
            fo.set("events", evs);
            framesArr.add(fo);
        }
        root.set("frames", framesArr);
        root.set("errorReport", errorReport);
        root.set("errorSite", errorSite);
        std::ostringstream oss;
        root.stringify(oss);
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) {
            if (err) *err = "cannot write " + path;
            return false;
        }
        ofs << oss.str();
        return static_cast<bool>(ofs);
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

bool ScenarioRecorder::load(const std::string& path, std::string* baseline,
                            std::vector<ScenarioFrame>* frames, std::string* errorReport,
                            std::string* errorSite, std::string* err) {
    try {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            if (err) *err = "cannot read " + path;
            return false;
        }
        std::ostringstream oss;
        oss << ifs.rdbuf();
        Poco::JSON::Parser parser;
        Poco::Dynamic::Var result = parser.parse(oss.str());
        Poco::JSON::Object::Ptr root = result.extract<Poco::JSON::Object::Ptr>();
        if (baseline) *baseline = root->getValue<std::string>("baseline");
        if (errorReport) *errorReport = root->optValue<std::string>("errorReport", "");
        if (errorSite) *errorSite = root->optValue<std::string>("errorSite", "");
        if (frames) {
            frames->clear();
            Poco::JSON::Array::Ptr fa = root->getArray("frames");
            if (fa) {
                for (size_t i = 0; i < fa->size(); ++i) {
                    Poco::JSON::Object::Ptr fo = fa->getObject(static_cast<unsigned int>(i));
                    ScenarioFrame f;
                    f.frame = fo->optValue<int>("frame", static_cast<int>(i));
                    Poco::JSON::Array::Ptr evs = fo->getArray("events");
                    if (evs) {
                        for (size_t j = 0; j < evs->size(); ++j) {
                            Poco::JSON::Object::Ptr eo =
                                evs->getObject(static_cast<unsigned int>(j));
                            ScenarioEvent e;
                            e.name = eo->getValue<std::string>("name");
                            Poco::JSON::Array::Ptr args = eo->getArray("args");
                            if (args) {
                                for (size_t k = 0; k < args->size(); ++k)
                                    e.args.push_back(args->getElement<std::string>(
                                        static_cast<unsigned int>(k)));
                            }
                            f.events.push_back(std::move(e));
                        }
                    }
                    frames->push_back(std::move(f));
                }
            }
        }
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

}  // namespace eve::dev
