#include "tilelayer_target/TileLayerTargetModule.h"

#include "editor/EditorSession.h"
#include "map_editing/TileLayerTarget.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <functional>
#include <utility>

namespace eve::tilelayer_target {

Module_IMPL(TileLayerTargetModule, new TileLayerTargetModule());

std::unique_ptr<map_editing::TileLayerTarget> TileLayerTargetModule::newTarget(std::string     id,
                                                                               map::TileLayer* layer) const {
    return map_editing::createTileLayerTarget(std::move(id), layer);
}

void TileLayerTargetModule::bind(editor::EditorSession* session, map_editing::TileLayerTarget* target) const {
    if (session) session->bindTarget(target);
}

void TileLayerTargetModule::expose(ssq::Table& table) {
    auto target = table.addClass<map_editing::TileLayerTarget>(
        "TileLayerTarget",
        std::function<map_editing::TileLayerTarget*()>([]() -> map_editing::TileLayerTarget* { return nullptr; }),
        true);
    target.addFunc("getTargetId",
                   [](map_editing::TileLayerTarget* self) { return self ? self->targetId().value() : std::string{}; });
    target.addFunc("getRevision", [](map_editing::TileLayerTarget* self) {
        return self ? static_cast<int64_t>(self->revision()) : int64_t{0};
    });
    target.addFunc("getWidth", &map_editing::TileLayerTarget::width);
    target.addFunc("getHeight", &map_editing::TileLayerTarget::height);
    target.addFunc("readInt", &map_editing::TileLayerTarget::readInt);
    target.addFunc("writeInt", &map_editing::TileLayerTarget::writeInt);
    target.addFunc("clearDirtyRegion", &map_editing::TileLayerTarget::clearDirtyRegion);

    auto module = table.addClass(name, TileLayerTargetModule::create, false);
    expose(module);
}

void TileLayerTargetModule::expose(ssq::Class& cls) {
    cls.addFunc("create", [](TileLayerTargetModule* self, const std::string& id, map::TileLayer* layer) {
        return self->newTarget(id, layer).release();
    });
    cls.addFunc("bind", &TileLayerTargetModule::bind);
}

}  // namespace eve::tilelayer_target
