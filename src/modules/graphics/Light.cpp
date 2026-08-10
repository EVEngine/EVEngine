#include "graphics/Light.h"

namespace eve::graphics {

Light2D *Light2D::createLight(const std::string &type) {
    Light2D *l = Light2D::create();
    ASSERT(l != nullptr);
    l->data()->entity = l;
    l->setType(type);
    return l;
}

void Light2D::setType(const std::string &type) {
    auto d = data();
    if (type == "dir" || type == "point")
        d->type = type;
    else
        d->type = "point";
}

std::string Light2D::getType() { return data()->type; }

void Light2D::setPosition(float x, float y) {
    data()->x = x;
    data()->y = y;
}

float Light2D::getX() { return data()->x; }
float Light2D::getY() { return data()->y; }

void Light2D::setDirection(float dx, float dy) {
    data()->dx = dx;
    data()->dy = dy;
}

float Light2D::getDirX() { return data()->dx; }
float Light2D::getDirY() { return data()->dy; }

void Light2D::setColor(float r, float g, float b, float intensity) {
    auto d = data();
    d->r = r;
    d->g = g;
    d->b = b;
    d->intensity = intensity;
}

void Light2D::setRadius(float radius) { data()->radius = radius > 0.f ? radius : 0.f; }
float Light2D::getRadius() { return data()->radius; }

void Light2D::setEnabled(bool enabled) { data()->enabled = enabled; }
bool Light2D::isEnabled() { return data()->enabled; }

void Light2D::setVolumetric(bool enabled) { data()->volumetric = enabled; }
bool Light2D::getVolumetric() { return data()->volumetric; }

void Light2D::setVolumetricIntensity(float intensity) {
    data()->volumetricIntensity = intensity < 0.f ? 0.f : intensity;
}
float Light2D::getVolumetricIntensity() { return data()->volumetricIntensity; }

void Light2D::setCanvas(Canvas *canvas) { data()->canvas = canvas; }

Light3D *Light3D::createLight(const std::string &type) {
    Light3D *l = Light3D::create();
    ASSERT(l != nullptr);
    l->data()->entity = l;
    l->setType(type);
    return l;
}

void Light3D::setType(const std::string &type) {
    auto d = data();
    if (type == "dir" || type == "point")
        d->type = type;
    else
        d->type = "point";
}

std::string Light3D::getType() { return data()->type; }

void Light3D::setPosition(float x, float y, float z) {
    auto d = data();
    d->x = x;
    d->y = y;
    d->z = z;
}

float Light3D::getX() { return data()->x; }
float Light3D::getY() { return data()->y; }
float Light3D::getZ() { return data()->z; }

void Light3D::setDirection(float dx, float dy, float dz) {
    auto d = data();
    d->dx = dx;
    d->dy = dy;
    d->dz = dz;
}

float Light3D::getDirX() { return data()->dx; }
float Light3D::getDirY() { return data()->dy; }
float Light3D::getDirZ() { return data()->dz; }

void Light3D::setColor(float r, float g, float b, float intensity) {
    auto d = data();
    d->r = r;
    d->g = g;
    d->b = b;
    d->intensity = intensity;
}

void Light3D::setRadius(float radius) { data()->radius = radius > 0.f ? radius : 0.f; }
float Light3D::getRadius() { return data()->radius; }

void Light3D::setEnabled(bool enabled) { data()->enabled = enabled; }
bool Light3D::isEnabled() { return data()->enabled; }

void Light3D::setCastShadow(bool cast) { data()->castShadow = cast; }
bool Light3D::getCastShadow() { return data()->castShadow; }

void Light3D::setShadowBias(float bias) { data()->shadowBias = bias < 0.f ? 0.f : bias; }
float Light3D::getShadowBias() { return data()->shadowBias; }

void Light3D::setShadowStrength(float strength) {
    data()->shadowStrength = strength < 0.f ? 0.f : (strength > 1.f ? 1.f : strength);
}
float Light3D::getShadowStrength() { return data()->shadowStrength; }

void Light3D::setVolumetric(bool enabled) { data()->volumetric = enabled; }
bool Light3D::getVolumetric() { return data()->volumetric; }

void Light3D::setVolumetricIntensity(float intensity) {
    data()->volumetricIntensity = intensity < 0.f ? 0.f : intensity;
}
float Light3D::getVolumetricIntensity() { return data()->volumetricIntensity; }

}  // namespace eve::graphics
