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
    // Snap to whole pixels so pixel art never lands between pixels. Use floor, not round: the view's half
    // size can be fractional (135 / 2 = 67.5), and rounding would make the camera snap at different moments
    // than the target, so the followed sprite would jitter by a pixel. Callers pass a pixel-snapped target.
    x_ = std::floor(clampAxis(targetX - width_ / 2.0f, width_, worldWidth));
    y_ = std::floor(clampAxis(targetY - height_ / 2.0f, height_, worldHeight));
}
