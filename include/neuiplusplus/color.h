/*
 * neuiplusplus - a C++20 skin over the neui C API
 *
 * Colour value type. neui's painter takes colours as a bare 0xAARRGGBB
 * uint32_t; this wraps that so a palette can be constexpr and the arithmetic
 * has a home.
 *
 * Method names deliberately track juce::Colour (brighter / darker /
 * withAlpha) - the point of this library is that sst-jucegui paint code ports
 * mechanically, and there are ~128 juce::Colour uses to carry across.
 */

#ifndef NEUIPLUSPLUS_COLOR_H
#define NEUIPLUSPLUS_COLOR_H

#include <algorithm>
#include <cstdint>

namespace neuiplusplus
{

class Color
{
  public:
    constexpr Color() = default;

    static constexpr Color fromARGB(std::uint32_t argb) { return Color{argb}; }
    static constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        return rgba(r, g, b, 0xFF);
    }
    static constexpr Color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        return Color{(std::uint32_t(a) << 24) | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) |
                     std::uint32_t(b)};
    }
    static constexpr Color grey(std::uint8_t v) { return rgb(v, v, v); }

    // The value the painter API wants. The only place the representation leaks.
    constexpr std::uint32_t argb() const { return argb_; }

    constexpr std::uint8_t alpha() const { return std::uint8_t((argb_ >> 24) & 0xFF); }
    constexpr std::uint8_t red() const { return std::uint8_t((argb_ >> 16) & 0xFF); }
    constexpr std::uint8_t green() const { return std::uint8_t((argb_ >> 8) & 0xFF); }
    constexpr std::uint8_t blue() const { return std::uint8_t(argb_ & 0xFF); }

    constexpr float alphaF() const { return alpha() / 255.0f; }
    constexpr bool isTransparent() const { return alpha() == 0; }

    // Absolute alpha in [0, 1].
    constexpr Color withAlpha(float a) const
    {
        return rgba(red(), green(), blue(), channel(a * 255.0f));
    }
    // Scales the existing alpha - the usual "dim this out" operation.
    constexpr Color withMultipliedAlpha(float f) const
    {
        return rgba(red(), green(), blue(), channel(alpha() * f));
    }

    // amount in [0, 1]: 0 is unchanged, 1 is white / black. Simple linear
    // ramps - NOT bit-identical to juce::Colour, which uses a non-linear
    // curve. Close enough to port against, different enough to re-eyeball a
    // skin once.
    constexpr Color brighter(float amount = 0.4f) const
    {
        const float t = clamp01(amount);
        return rgba(lerpChannel(red(), 255, t), lerpChannel(green(), 255, t),
                    lerpChannel(blue(), 255, t), alpha());
    }
    constexpr Color darker(float amount = 0.4f) const
    {
        const float t = clamp01(amount);
        return rgba(lerpChannel(red(), 0, t), lerpChannel(green(), 0, t),
                    lerpChannel(blue(), 0, t), alpha());
    }

    // Straight (non-premultiplied) interpolation, alpha included.
    constexpr Color interpolatedWith(Color other, float t) const
    {
        const float u = clamp01(t);
        return rgba(lerpChannel(red(), other.red(), u), lerpChannel(green(), other.green(), u),
                    lerpChannel(blue(), other.blue(), u), lerpChannel(alpha(), other.alpha(), u));
    }

    constexpr bool operator==(const Color &) const = default;

  private:
    explicit constexpr Color(std::uint32_t v) : argb_(v) {}

    static constexpr float clamp01(float f) { return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f); }
    static constexpr std::uint8_t channel(float f)
    {
        return std::uint8_t(f < 0.0f ? 0.0f : (f > 255.0f ? 255.0f : f) + 0.5f);
    }
    static constexpr std::uint8_t lerpChannel(std::uint8_t from, std::uint8_t to, float t)
    {
        return channel(float(from) + (float(to) - float(from)) * t);
    }

    std::uint32_t argb_{0};
};

namespace colors
{
inline constexpr Color transparent = Color::rgba(0, 0, 0, 0);
inline constexpr Color black = Color::rgb(0, 0, 0);
inline constexpr Color white = Color::rgb(255, 255, 255);
} // namespace colors

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_COLOR_H
