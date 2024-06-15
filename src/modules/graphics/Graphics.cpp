#include "graphics/Graphics.h"
#include "graphics/vulkan/Graphics.h"

namespace eve::graphics {

Module_IMPL(Graphics, new vulkan::Graphics());



}  // namespace eve::graphics
