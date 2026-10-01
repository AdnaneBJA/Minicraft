#pragma once

// A view onto the world, in world pixels. (x, y) is the top-left corner of the visible area.
class Camera {
public:
    void setViewSize(float width, float height);

    // Centres the view on a target point, clamped so it never shows past the edges of the world.
    void follow(float targetX, float targetY, float worldWidth, float worldHeight);

    float x() const { return x_; }
    float y() const { return y_; }
    float width() const { return width_; }
    float height() const { return height_; }

private:
    float x_ = 0.0f;
    float y_ = 0.0f;
    float width_ = 0.0f;
    float height_ = 0.0f;
};
