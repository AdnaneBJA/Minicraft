#include "camera.h"

#include <algorithm>
#include <cmath>

namespace {

// Clamp the view along one axis; if the world is smaller than the view, centre the world instead.
float clampAxis(float position, float viewSize, float worldSize) {
    if (worldSize <= viewSize) {
        return (worldSize - viewSize) / 2.0f;
    }
    return std::clamp(position, 0.0f, worldSize - viewSize);
}

}  // namespace

void Camera::setViewSize(float width, float height) {
    width_ = width;
    height_ = height;
}

void Camera::follow(float targetX, float targetY, float worldWidth, float worldHeight) {
    // Round to whole pixels so pixel art never lands between pixels (no shimmering).
    x_ = std::round(clampAxis(targetX - width_ / 2.0f, width_, worldWidth));
    y_ = std::round(clampAxis(targetY - height_ / 2.0f, height_, worldHeight));
}
