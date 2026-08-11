/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_DRAW_GEOMETRY_H
#define NEUIPLUSPLUS_DRAW_GEOMETRY_H

/**
 * @file
 * @brief @ref neuiplusplus::Point and @ref neuiplusplus::Rect, in design units.
 *
 * Design units are zoom-independent floats. ComponentCore::setBounds multiplies
 * by the frame's zoom and snaps to integer device pixels on the way out to neui;
 * incoming mouse coordinates are divided by it on the way in. Nothing above that
 * boundary ever sees zoom.
 *
 * Accessor names track `juce::Rectangle` (including the British "centre") so the
 * ~57 `juce::Rectangle` uses in sst-jucegui port by include-swap. A porting
 * affordance, not a claim that it is the nicer spelling.
 */

namespace neuiplusplus
{

/** @brief A 2D position or offset in design units. */
struct Point
{
    float x{0.0f};
    float y{0.0f};

    constexpr Point() = default;
    constexpr Point(float px, float py) : x(px), y(py) {}

    constexpr Point translated(float dx, float dy) const { return {x + dx, y + dy}; }
    constexpr Point operator+(Point o) const { return {x + o.x, y + o.y}; }
    constexpr Point operator-(Point o) const { return {x - o.x, y - o.y}; }
    constexpr Point operator*(float s) const { return {x * s, y * s}; }
    constexpr bool operator==(const Point &) const = default;
};

/**
 * @brief An axis-aligned rectangle in design units.
 *
 * Immutable apart from the `removeFromX` family, which slice a strip off and
 * shrink the receiver - the layout idiom that makes `resized()` readable.
 */
class Rect
{
  public:
    constexpr Rect() = default;
    constexpr Rect(float x, float y, float w, float h) : x_(x), y_(y), w_(w), h_(h) {}

    /** @brief A rect of this size at the origin. */
    static constexpr Rect fromSize(float w, float h) { return {0.0f, 0.0f, w, h}; }

    constexpr float getX() const { return x_; }
    constexpr float getY() const { return y_; }
    constexpr float getWidth() const { return w_; }
    constexpr float getHeight() const { return h_; }
    constexpr float getRight() const { return x_ + w_; }
    constexpr float getBottom() const { return y_ + h_; }
    constexpr float getCentreX() const { return x_ + w_ * 0.5f; }
    constexpr float getCentreY() const { return y_ + h_ * 0.5f; }
    constexpr Point getCentre() const { return {getCentreX(), getCentreY()}; }
    constexpr Point getTopLeft() const { return {x_, y_}; }
    constexpr bool isEmpty() const { return w_ <= 0.0f || h_ <= 0.0f; }

    constexpr Rect withX(float v) const { return {v, y_, w_, h_}; }
    constexpr Rect withY(float v) const { return {x_, v, w_, h_}; }
    constexpr Rect withWidth(float v) const { return {x_, y_, v, h_}; }
    constexpr Rect withHeight(float v) const { return {x_, y_, w_, v}; }
    constexpr Rect withSize(float w, float h) const { return {x_, y_, w, h}; }
    constexpr Rect translated(float dx, float dy) const { return {x_ + dx, y_ + dy, w_, h_}; }

    /** @brief Origin-relative copy - the usual "paint me in my own coordinates" move. */
    constexpr Rect atOrigin() const { return {0.0f, 0.0f, w_, h_}; }

    constexpr Rect reduced(float d) const { return reduced(d, d); }
    constexpr Rect reduced(float dx, float dy) const
    {
        return {x_ + dx, y_ + dy, w_ - 2.0f * dx, h_ - 2.0f * dy};
    }
    constexpr Rect expanded(float d) const { return reduced(-d, -d); }

    constexpr Rect withTrimmedTop(float d) const { return {x_, y_ + d, w_, h_ - d}; }
    constexpr Rect withTrimmedBottom(float d) const { return {x_, y_, w_, h_ - d}; }
    constexpr Rect withTrimmedLeft(float d) const { return {x_ + d, y_, w_ - d, h_}; }
    constexpr Rect withTrimmedRight(float d) const { return {x_, y_, w_ - d, h_}; }

    /// @name Layout slicing
    /// Mutating "take a strip off this edge" helpers, after
    /// `juce::Rectangle::removeFromTop` and friends. Layout code reads far
    /// better with these than with arithmetic.
    /// @{
    constexpr Rect removeFromTop(float d)
    {
        const Rect taken{x_, y_, w_, d};
        y_ += d;
        h_ -= d;
        return taken;
    }
    constexpr Rect removeFromBottom(float d)
    {
        h_ -= d;
        return {x_, y_ + h_, w_, d};
    }
    constexpr Rect removeFromLeft(float d)
    {
        const Rect taken{x_, y_, d, h_};
        x_ += d;
        w_ -= d;
        return taken;
    }
    constexpr Rect removeFromRight(float d)
    {
        w_ -= d;
        return {x_ + w_, y_, d, h_};
    }
    /// @}

    /** @brief Half-open containment: the right and bottom edges are outside. */
    constexpr bool contains(Point p) const
    {
        return p.x >= x_ && p.y >= y_ && p.x < getRight() && p.y < getBottom();
    }

    constexpr bool operator==(const Rect &) const = default;

  private:
    float x_{0.0f};
    float y_{0.0f};
    float w_{0.0f};
    float h_{0.0f};
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_DRAW_GEOMETRY_H
