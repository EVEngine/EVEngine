#include "scene/LocationProfileBindings.h"

#include "common/SquirrelBinding.h"
#include "scene/LocationProfile.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::scene {
namespace {
eve::Value poseValue(const LocationPose& p) {
    return eve::Value::Object{{"x", p.x}, {"y", p.y}, {"z", p.z}, {"qx", p.qx}, {"qy", p.qy},
                              {"qz", p.qz}, {"qw", p.qw}};
}
eve::Value snapshotValue(const LocationSnapshot& s) {
    eve::Value::Object value{{"camera", poseValue(s.camera)}, {"hasPlayer", bool(s.player)}};
    value["player"] = s.player ? poseValue(*s.player) : eve::Value();
    return value;
}
}

void exposeLocationProfileBindings(ssq::Table& table) {
    auto pose = table.addClass("LocationPose", ssq::Class::Ctor<LocationPose()>());
    pose.addVar("x", &LocationPose::x); pose.addVar("y", &LocationPose::y); pose.addVar("z", &LocationPose::z);
    pose.addVar("qx", &LocationPose::qx); pose.addVar("qy", &LocationPose::qy); pose.addVar("qz", &LocationPose::qz);
    pose.addVar("qw", &LocationPose::qw);
    auto bookmark = table.addClass("LocationBookmark", ssq::Class::Ctor<LocationBookmark()>());
    bookmark.addVar("name", &LocationBookmark::name);
    bookmark.addVar("controller", &LocationBookmark::controller);
    bookmark.addVar("scene", &LocationBookmark::scene);
    bookmark.addFunc("setCamera", [](LocationBookmark* b, const LocationPose* p) { if (b && p) b->camera = *p; });
    bookmark.addFunc("setPlayer", [](LocationBookmark* b, const LocationPose* p) { if (b && p) b->player = *p; });
    bookmark.addFunc("clearPlayer", [](LocationBookmark* b) { if (b) b->player.reset(); });

    auto profile = table.addClass("LocationProfile", ssq::Class::Ctor<LocationProfile()>());
    profile.addFunc("hasSavedLocation", [](const LocationProfile* p) { return p && p->hasSavedLocation(); });
    profile.addFunc("getBookmarkCount", [](const LocationProfile* p) { return p ? p->getBookmarkCount() : 0; });
    profile.addFunc("clearBookmarks", [](LocationProfile* p) { if (p) p->clearBookmarks(); });
    profile.addFunc("serializeJson", [vm=table.getHandle()](const LocationProfile* p) {
        auto r=p?p->serializeJson():Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"profile required"));
        return eve::script::projectResult(vm,std::move(r),[](const std::string& v){return eve::Value(v);});
    });
    profile.addFunc("restoreJson", [vm=table.getHandle()](LocationProfile* p,const std::string& json) {
        auto r=p?p->restoreJson(json):Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"profile required"));
        return eve::script::projectResult(vm,std::move(r));
    });
    profile.addFunc("saveLocation", [vm=table.getHandle()](LocationProfile* p, const LocationPose* camera) {
        return eve::script::projectResult(vm, p && camera ? p->saveLocation(*camera)
            : Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"location profile and camera required")));
    });
    profile.addFunc("saveLocationWithPlayer", [vm=table.getHandle()](LocationProfile* p, const LocationPose* camera,
                                                                      const LocationPose* player) {
        return eve::script::projectResult(vm, p && camera && player ? p->saveLocation(*camera,*player)
            : Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"profile, camera and player required")));
    });
    profile.addFunc("loadLocation", [vm=table.getHandle()](LocationProfile* p) {
        auto r=p?p->loadLocation():Result<LocationSnapshot>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"profile required"));
        return eve::script::projectResult(vm,std::move(r),[](const LocationSnapshot& s){return snapshotValue(s);});
    });
    profile.addFunc("addBookmark", [vm=table.getHandle()](LocationProfile* p, const LocationBookmark* b) {
        auto r=p&&b?p->addBookmark(*b):Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"profile and bookmark required"));
        return eve::script::projectResult(vm,std::move(r),[](int v){return eve::Value(v);});
    });
    profile.addFunc("loadBookmark", [vm=table.getHandle()](const LocationProfile* p,int i) {
        auto r=p?p->loadBookmark(i):Result<LocationSnapshot>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"profile required"));
        return eve::script::projectResult(vm,std::move(r),[](const LocationSnapshot& s){return snapshotValue(s);});
    });
    profile.addFunc("removeBookmark", [vm=table.getHandle()](LocationProfile* p,int i,int selected) {
        auto r=p?p->removeBookmark(i,selected):Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"profile required"));
        return eve::script::projectResult(vm,std::move(r),[](int v){return eve::Value(v);});
    });
    profile.addFunc("getBookmarkName", [vm=table.getHandle()](const LocationProfile* p,int i) {
        auto r=p?p->getBookmarkName(i):Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"profile required"));
        return eve::script::projectResult(vm,std::move(r),[](const std::string& v){return eve::Value(v);});
    });
    profile.addFunc("getBookmarkController", [vm=table.getHandle()](const LocationProfile* p,int i) {
        auto r=p?p->getBookmarkController(i):Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"profile required"));
        return eve::script::projectResult(vm,std::move(r),[](const std::string& v){return eve::Value(v);});
    });
    profile.addFunc("getBookmarkScene", [vm=table.getHandle()](const LocationProfile* p,int i) {
        auto r=p?p->getBookmarkScene(i):Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"profile required"));
        return eve::script::projectResult(vm,std::move(r),[](const std::string& v){return eve::Value(v);});
    });
    profile.addFunc("overrideBookmark", [vm=table.getHandle()](LocationProfile* p,int i,const LocationBookmark* b) {
        auto r=p&&b?p->overrideBookmark(i,LocationSnapshot{b->camera,b->player},b->controller,b->scene)
            :Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"profile and bookmark required"));
        return eve::script::projectResult(vm,std::move(r));
    });
    profile.addFunc("previousBookmark", [vm=table.getHandle()](const LocationProfile* p,int i) {
        auto r=p?p->previousBookmark(i):Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"profile required"));
        return eve::script::projectResult(vm,std::move(r),[](int v){return eve::Value(v);});
    });
    profile.addFunc("nextBookmark", [vm=table.getHandle()](const LocationProfile* p,int i) {
        auto r=p?p->nextBookmark(i):Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,"profile required"));
        return eve::script::projectResult(vm,std::move(r),[](int v){return eve::Value(v);});
    });
}
}  // namespace eve::scene
