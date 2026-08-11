/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_DRAW_FONT_H
#define NEUIPLUSPLUS_DRAW_FONT_H

#include <string>

/**
 * @file
 * @brief @ref neuiplusplus::Font - the four things that pick a typeface.
 *
 * neui splits a font across two mechanisms: family / weight / italic live on the
 * painter's font STACK (`push_font_styled` ... `pop_font`), while the size is an
 * argument to each `draw_text` / `measure_text` call. That split is real but it
 * is not something a caller wants to think about, so this bundles all four and
 * Canvas takes them apart again - see Canvas::withFont and Canvas::drawText.
 */

namespace neuiplusplus
{

/** @brief CSS numeric weights. neui takes the raw number; these just name the usual ones. */
namespace FontWeight
{
inline constexpr int normal = 0; ///< neui's "unspecified", which resolves to 400
inline constexpr int light = 300;
inline constexpr int regular = 400;
inline constexpr int medium = 500;
inline constexpr int semiBold = 600;
inline constexpr int bold = 700;
inline constexpr int black = 900;
} // namespace FontWeight

/** @brief A resolved text style. An empty @ref family means the host default. */
struct Font
{
    std::string family;             ///< empty = host system default
    float size{13.0f};              ///< design units
    int weight{FontWeight::normal}; ///< CSS numeric weight; 0 = normal
    bool italic{false};

    Font() = default;
    explicit Font(float pointSize) : size(pointSize) {}
    Font(std::string fam, float pointSize, int w = FontWeight::normal, bool it = false)
        : family(std::move(fam)), size(pointSize), weight(w), italic(it)
    {
    }

    Font withSize(float s) const
    {
        Font f = *this;
        f.size = s;
        return f;
    }
    Font withWeight(int w) const
    {
        Font f = *this;
        f.weight = w;
        return f;
    }
    Font italicised(bool on = true) const
    {
        Font f = *this;
        f.italic = on;
        return f;
    }

    bool operator==(const Font &) const = default;
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_DRAW_FONT_H
