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

void Camera::setView(float width, float height, float scale) {
    width_ = width;
    height_ = height;
    scale_ = scale;
}

void Camera::follow(float targetX, float targetY, float worldWidth, float worldHeight) {
    // Floor, not round: half the view can fall between screen pixels (e.g. an odd window height), and rounding
    // would snap at different moments than the target, making the followed sprite jitter by a pixel.
    x_ = std::floor(clampAxis(targetX - width_ / 2.0f, width_, worldWidth) * scale_) / scale_;
    y_ = std::floor(clampAxis(targetY - height_ / 2.0f, height_, worldHeight) * scale_) / scale_;
}

float Camera::snap(float worldValue) const { return std::round(worldValue * scale_) / scale_; }
