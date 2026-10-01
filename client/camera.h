#pragma once

// A view onto the world, in world pixels. (x, y) is the top-left corner of the visible area.
// Positions are snapped to *screen* pixels (1 / scale world pixels), not world pixels: snapping to world
// pixels makes the view move in 4-screen-pixel steps at 4x zoom, which shakes visibly when moving diagonally.
class Camera {
public:
    // `scale` is how many screen pixels one world pixel covers.
    void setView(float width, float height, float scale);

    // Centres the view on a target point, clamped so it never shows past the edges of the world.
    // Pass a target already snapped with snap(), so the followed sprite stays fixed on screen.
    void follow(float targetX, float targetY, float worldWidth, float worldHeight);

    // Rounds a world coordinate to the nearest screen pixel.
    float snap(float worldValue) const;

    float x() const { return x_; }
    float y() const { return y_; }
    float width() const { return width_; }
    float height() const { return height_; }
    float scale() const { return scale_; }

private:
    float x_ = 0.0f;
    float y_ = 0.0f;
    float width_ = 0.0f;
    float height_ = 0.0f;
    float scale_ = 1.0f;
};
