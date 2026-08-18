// Registers the scene link kind backed by audio: a positional 3D source that
// follows its node.
//
// audio::Source has no liveness API, so no `alive` callback is supplied and the
// link is assumed alive until explicitly unlinked.

#include "audio/Source.h"
#include "scene/SceneHost.h"
#include "scene/SceneLink.h"

namespace eve::audio {
namespace {

using eve::scene::SceneNode;

void pushSource3D(const SceneNode &n, void *target) {
    auto *s = static_cast<Source *>(target);
    s->setPosition(n.world[3][0], n.world[3][1], n.world[3][2]);
}

struct Register {
    Register() { eve::scene::registerLinkKind({"audio3d", &pushSource3D, nullptr, nullptr}); }
} g_register;

}  // namespace
}  // namespace eve::audio
