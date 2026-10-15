#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/PbrSurface.h"
#include "graphics/VegetationField.h"
#include "graphics/VegetationFieldGpu.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::graphics;

TEST_CASE("graphics.vegetation.gradient_validation_rejects_invalid_surface") {
    PbrSurface surface;
    surface.vegetationGradient.enabled = true;
    surface.vegetationGradient.minimum = -.01f;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.vegetationGradient.minimum = 0;
    surface.vegetationGradient.maximum = -.0001f;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.vegetationGradient.maximum  = 1;
    surface.vegetationGradient.colorOne = {-1, 1, 1};
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.vegetationGradient.colorOne = {1, 1, 1};
    surface.vegetationGradient.colorTwo = {1, INFINITY, 1};
    REQUIRE(!validatePbrSurface(surface).ok());
}
