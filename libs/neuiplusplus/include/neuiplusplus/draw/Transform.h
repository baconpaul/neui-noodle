/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_DRAW_TRANSFORM_H
#define NEUIPLUSPLUS_DRAW_TRANSFORM_H

#include <cmath>

#include "Geometry.h"

/**
 * @file
 * @brief @ref neuiplusplus::Transform - a 2x3 affine matrix value type.
 *
 * A VALUE TYPE, NOT A CANVAS SETTER. neui's painter has no `set_transform`: it
 * exposes `translate` / `rotate` / `scale`, each post-multiplied onto the top of
 * its own stack. So this cannot be handed to the painter, and Canvas offers no
 * method that takes one.
 *
 * What it is for is the arithmetic the painter cannot do for you: mirror the
 * ops you pushed, then invert them to hit-test the shape you drew.
 *
 * @code
 * // paint: rotate the pointer about the knob centre
 * auto s = g.savedState();
 * g.translate(c.x, c.y);
 * g.rotate(angle);
 * g.fillRect(pointerRect, ink);
 *
 * // hit-test: the same transform as a value, run backwards
 * const auto t = Transform::rotationAbout(c, angle);
 * const bool onPointer = pointerRect.contains(t.inverted().apply(e.position));
 * @endcode
 *
 * Layout is row-major, mapping (x, y) to
 * `(m00*x + m01*y + m02, m10*x + m11*y + m12)`.
 */

namespace neuiplusplus
{

/** @brief An affine 2D transform. Immutable; every operation returns a new value. */
class Transform
{
  public:
    /** @brief The identity. */
    constexpr Transform() = default;
    constexpr Transform(float m00, float m01, float m02, float m10, float m11, float m12)
        : m00_(m00), m01_(m01), m02_(m02), m10_(m10), m11_(m11), m12_(m12)
    {
    }

    static constexpr Transform translation(float dx, float dy)
    {
        return {1.0f, 0.0f, dx, 0.0f, 1.0f, dy};
    }
    static constexpr Transform scaling(float sx, float sy)
    {
        return {sx, 0.0f, 0.0f, 0.0f, sy, 0.0f};
    }
    static constexpr Transform shear(float sx, float sy)
    {
        return {1.0f, sx, 0.0f, sy, 1.0f, 0.0f};
    }

    /** @brief Clockwise in neui's Y-down coordinates, about the origin. */
    static Transform rotation(float radians);
    /** @brief Rotation about @p centre - the `translate / rotate / translate-back` sandwich. */
    static Transform rotationAbout(Point centre, float radians);
    /** @brief Scaling about @p centre rather than the origin. */
    static constexpr Transform scalingAbout(Point centre, float sx, float sy)
    {
        return translation(-centre.x, -centre.y)
            .followedBy(scaling(sx, sy))
            .followedBy(translation(centre.x, centre.y));
    }

    /// @name Composition
    /// `a.followedBy(b)` applies @c a first, then @c b - the order the canvas
    /// ops read in, not the order matrix multiplication writes in.
    /// @{
    constexpr Transform followedBy(const Transform &t) const
    {
        return {t.m00_ * m00_ + t.m01_ * m10_,          t.m00_ * m01_ + t.m01_ * m11_,
                t.m00_ * m02_ + t.m01_ * m12_ + t.m02_, t.m10_ * m00_ + t.m11_ * m10_,
                t.m10_ * m01_ + t.m11_ * m11_,          t.m10_ * m02_ + t.m11_ * m12_ + t.m12_};
    }
    constexpr Transform translated(float dx, float dy) const
    {
        return followedBy(translation(dx, dy));
    }
    constexpr Transform scaled(float sx, float sy) const { return followedBy(scaling(sx, sy)); }
    Transform rotated(float radians) const { return followedBy(rotation(radians)); }
    /// @}

    /// @name Application
    /// @{
    constexpr Point apply(Point p) const
    {
        return {m00_ * p.x + m01_ * p.y + m02_, m10_ * p.x + m11_ * p.y + m12_};
    }
    /** @brief Applies the linear part only - for directions, which do not translate. */
    constexpr Point applyToVector(Point v) const
    {
        return {m00_ * v.x + m01_ * v.y, m10_ * v.x + m11_ * v.y};
    }
    /**
     * @brief The axis-aligned bounding box of the four transformed corners.
     * @note An arbitrary transform turns a rect into a parallelogram, so this
     *       is a bound and not the shape itself.
     */
    constexpr Rect boundsOf(Rect r) const
    {
        const Point a = apply({r.getX(), r.getY()});
        const Point b = apply({r.getRight(), r.getY()});
        const Point c = apply({r.getX(), r.getBottom()});
        const Point d = apply({r.getRight(), r.getBottom()});
        const float x0 = min4(a.x, b.x, c.x, d.x);
        const float y0 = min4(a.y, b.y, c.y, d.y);
        return {x0, y0, max4(a.x, b.x, c.x, d.x) - x0, max4(a.y, b.y, c.y, d.y) - y0};
    }
    /// @}

    constexpr float determinant() const { return m00_ * m11_ - m01_ * m10_; }
    /** @brief False when the transform collapses an axis (a zero scale). */
    constexpr bool isInvertible() const { return determinant() != 0.0f; }
    /** @brief The inverse, or the identity if @ref isInvertible is false. */
    constexpr Transform inverted() const
    {
        const float det = determinant();
        if (det == 0.0f)
            return {};
        const float inv = 1.0f / det;
        const float i00 = m11_ * inv, i01 = -m01_ * inv;
        const float i10 = -m10_ * inv, i11 = m00_ * inv;
        return {i00, i01, -(i00 * m02_ + i01 * m12_), i10, i11, -(i10 * m02_ + i11 * m12_)};
    }

    constexpr bool isIdentity() const { return *this == Transform{}; }
    constexpr bool operator==(const Transform &) const = default;

    constexpr float m00() const { return m00_; }
    constexpr float m01() const { return m01_; }
    constexpr float m02() const { return m02_; }
    constexpr float m10() const { return m10_; }
    constexpr float m11() const { return m11_; }
    constexpr float m12() const { return m12_; }

  private:
    static constexpr float min4(float a, float b, float c, float d)
    {
        const float ab = a < b ? a : b, cd = c < d ? c : d;
        return ab < cd ? ab : cd;
    }
    static constexpr float max4(float a, float b, float c, float d)
    {
        const float ab = a > b ? a : b, cd = c > d ? c : d;
        return ab > cd ? ab : cd;
    }

    float m00_{1.0f}, m01_{0.0f}, m02_{0.0f};
    float m10_{0.0f}, m11_{1.0f}, m12_{0.0f};
};

// Out of line only because std::sin / std::cos are not constexpr before C++26.
inline Transform Transform::rotation(float radians)
{
    const float c = std::cos(radians), s = std::sin(radians);
    return {c, -s, 0.0f, s, c, 0.0f};
}

inline Transform Transform::rotationAbout(Point centre, float radians)
{
    return translation(-centre.x, -centre.y)
        .followedBy(rotation(radians))
        .followedBy(translation(centre.x, centre.y));
}

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_DRAW_TRANSFORM_H
