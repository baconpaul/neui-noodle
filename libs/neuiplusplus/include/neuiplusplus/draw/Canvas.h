/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_DRAW_CANVAS_H
#define NEUIPLUSPLUS_DRAW_CANVAS_H

#include <string>
#include <string_view>

#include <neui/neui.h>

#include "Color.h"
#include "Font.h"
#include "Geometry.h"

/**
 * @file
 * @brief @ref neuiplusplus::Canvas - the painter, bound to its handle.
 *
 * Canvas ties neui's painter vtable to the opaque per-paint handle, so a draw
 * call is `g.fillRect(r, c)` rather than `api->fill_rect(p, x, y, w, h, argb)`.
 *
 * THE STACKS ARE RAII ONLY. The painter has four push/pop stacks (clip,
 * transform, alpha, font) and neui's own docs warn that an unbalanced push
 * leaks into the widget. Every one of them is reachable here only through a
 * `[[nodiscard]]` guard, so that class of bug is unrepresentable.
 *
 * Coordinates are DESIGN UNITS. The dispatcher installs the zoom scale before
 * calling paint, so paint code never multiplies by anything.
 *
 * Passed as `Canvas &`, non-const, matching `juce::Graphics &` - sst-jucegui
 * caches into members during paint (tab rects, last-painted regions) and a const
 * paint would fight ~20 call sites for no gain.
 */

namespace neuiplusplus
{

enum class HAlign
{
    left,
    centre,
    right
};

enum class VAlign
{
    top,
    middle,
    bottom
};

/** @brief Vertical metrics of the active font at a given size. */
struct FontMetrics
{
    float ascent{0.0f};
    float descent{0.0f};
    float lineHeight{0.0f};
};

/** @brief A drawing surface for the duration of one paint call. Never stored. */
class Canvas
{
  public:
    Canvas(neui_painter_api_t *api, neui_painter_t *p, Rect localBounds, bool focused)
        : api_(api), p_(p), bounds_(localBounds), focused_(focused)
    {
    }

    Canvas(const Canvas &) = delete;
    Canvas &operator=(const Canvas &) = delete;

    /** @brief The component's own area, origin at (0, 0), in design units. */
    Rect bounds() const { return bounds_; }
    /** @brief Whether the widget being painted currently holds keyboard focus. */
    bool hasFocus() const { return focused_; }

    /// @name State guards
    /// Each is `[[nodiscard]]`: dropping one on the floor would pop immediately
    /// and do nothing, which is never what was meant.
    /// @{

    /** @brief Holds one clip + transform push. Restores both on scope exit. */
    class StateGuard
    {
      public:
        ~StateGuard()
        {
            if (api_)
            {
                api_->pop_clip(p_);
                api_->pop_transform(p_);
            }
        }
        StateGuard(StateGuard &&o) noexcept : api_(o.api_), p_(o.p_) { o.api_ = nullptr; }
        StateGuard(const StateGuard &) = delete;

      private:
        friend class Canvas;
        StateGuard(neui_painter_api_t *a, neui_painter_t *p, Rect clip) : api_(a), p_(p)
        {
            api_->push_transform(p_);
            api_->push_clip(p_, clip.getX(), clip.getY(), clip.getWidth(), clip.getHeight());
        }
        neui_painter_api_t *api_;
        neui_painter_t *p_;
    };

    /** @brief Holds one opacity push. Pushes compose multiplicatively. */
    class AlphaGuard
    {
      public:
        ~AlphaGuard()
        {
            if (api_)
                api_->pop_alpha(p_);
        }
        AlphaGuard(AlphaGuard &&o) noexcept : api_(o.api_), p_(o.p_) { o.api_ = nullptr; }
        AlphaGuard(const AlphaGuard &) = delete;

      private:
        friend class Canvas;
        AlphaGuard(neui_painter_api_t *a, neui_painter_t *p, float f) : api_(a), p_(p)
        {
            api_->push_alpha(p_, f);
        }
        neui_painter_api_t *api_;
        neui_painter_t *p_;
    };

    /** @brief Holds one (family, weight, italic) push. The size is per draw call. */
    class FontGuard
    {
      public:
        ~FontGuard()
        {
            if (api_)
                api_->pop_font(p_);
        }
        FontGuard(FontGuard &&o) noexcept : api_(o.api_), p_(o.p_) { o.api_ = nullptr; }
        FontGuard(const FontGuard &) = delete;

      private:
        friend class Canvas;
        FontGuard(neui_painter_api_t *a, neui_painter_t *p, const char *family, int weight,
                  bool italic)
            : api_(a), p_(p)
        {
            // push_font_styled is vtable-appended; fall back on an older host.
            if (api_->push_font_styled)
                api_->push_font_styled(p_, family, weight, italic);
            else
                api_->push_font(p_, family, weight);
        }
        neui_painter_api_t *api_;
        neui_painter_t *p_;
    };

    /** @brief Save clip + transform, clipped to the whole component. */
    [[nodiscard]] StateGuard savedState() { return StateGuard{api_, p_, bounds_}; }
    /** @brief Save clip + transform, clipped to @p r. */
    [[nodiscard]] StateGuard clippedTo(Rect r) { return StateGuard{api_, p_, r}; }
    [[nodiscard]] AlphaGuard withAlpha(float f) { return AlphaGuard{api_, p_, f}; }
    [[nodiscard]] FontGuard withFont(const char *family, int weight = 0, bool italic = false)
    {
        return FontGuard{api_, p_, family, weight, italic};
    }
    /** @brief The Font overload. Only family / weight / italic are pushed; @ref Font::size
     *         is a per-call argument, so pass the same Font to drawText. */
    [[nodiscard]] FontGuard withFont(const Font &f)
    {
        return FontGuard{api_, p_, f.family.c_str(), f.weight, f.italic};
    }
    /// @}

    /// @name Transforms
    /// Post-multiplied onto the top of the transform stack, so they compose in
    /// call order. Only legal inside a @ref savedState. @see Transform for the
    /// value type that mirrors these for hit-testing.
    /// @{
    void translate(float dx, float dy) { api_->translate(p_, dx, dy); }
    void rotate(float radians) { api_->rotate(p_, radians); }
    void scale(float sx, float sy) { api_->scale(p_, sx, sy); }
    /// @}

    /// @name Shapes
    /// @{
    void fillAll(Color c) { fillRect(bounds_, c); }
    void fillRect(Rect r, Color c)
    {
        api_->fill_rect(p_, r.getX(), r.getY(), r.getWidth(), r.getHeight(), c.argb());
    }
    void drawRect(Rect r, float thickness, Color c)
    {
        api_->draw_rect(p_, r.getX(), r.getY(), r.getWidth(), r.getHeight(), thickness, c.argb());
    }
    void fillRoundRect(Rect r, float radius, Color c)
    {
        api_->fill_round_rect(p_, r.getX(), r.getY(), r.getWidth(), r.getHeight(), radius,
                              c.argb());
    }
    void drawRoundRect(Rect r, float radius, float thickness, Color c)
    {
        api_->draw_round_rect(p_, r.getX(), r.getY(), r.getWidth(), r.getHeight(), radius,
                              thickness, c.argb());
    }
    void fillEllipse(Rect r, Color c)
    {
        api_->fill_ellipse(p_, r.getX(), r.getY(), r.getWidth(), r.getHeight(), c.argb());
    }
    void drawEllipse(Rect r, float thickness, Color c)
    {
        api_->draw_ellipse(p_, r.getX(), r.getY(), r.getWidth(), r.getHeight(), thickness,
                           c.argb());
    }
    void drawLine(Point a, Point b, float thickness, Color c)
    {
        api_->draw_line(p_, a.x, a.y, b.x, b.y, thickness, c.argb());
    }
    /// @}

    /// @name Text
    /// `draw_text` takes a NUL-terminated pointer with no length, so these do
    /// too - a string_view would need a copy to guarantee termination.
    /// @{
    void drawText(const char *utf8, Rect r, float size, Color c, HAlign h = HAlign::left,
                  VAlign v = VAlign::middle)
    {
        api_->draw_text_aligned(p_, r.getX(), r.getY(), r.getWidth(), r.getHeight(), utf8, size,
                                c.argb(), toNeui(h), toNeui(v));
    }
    void drawText(const std::string &utf8, Rect r, float size, Color c, HAlign h = HAlign::left,
                  VAlign v = VAlign::middle)
    {
        drawText(utf8.c_str(), r, size, c, h, v);
    }
    /** @brief Draws in @p f, pushing and popping its family for the call. */
    void drawText(const char *utf8, Rect r, const Font &f, Color c, HAlign h = HAlign::left,
                  VAlign v = VAlign::middle)
    {
        auto g = withFont(f);
        drawText(utf8, r, f.size, c, h, v);
    }
    void drawText(const std::string &utf8, Rect r, const Font &f, Color c, HAlign h = HAlign::left,
                  VAlign v = VAlign::middle)
    {
        drawText(utf8.c_str(), r, f, c, h, v);
    }

    /** @brief Width of @p utf8 in the ACTIVE font (whatever is on the stack) at @p size. */
    float textWidth(std::string_view utf8, float size) const
    {
        return api_->measure_text(p_, utf8.data(), int(utf8.size()), size);
    }
    /** @brief Width in @p f specifically, pushing it for the measurement. */
    float textWidth(std::string_view utf8, const Font &f)
    {
        auto g = withFont(f);
        return textWidth(utf8, f.size);
    }
    /** @brief Vertical metrics of the active font. Use with VAlign::top for baseline work. */
    FontMetrics fontMetrics(float size) const
    {
        FontMetrics m;
        api_->font_metrics(p_, size, &m.ascent, &m.descent, &m.lineHeight);
        return m;
    }
    FontMetrics fontMetrics(const Font &f)
    {
        auto g = withFont(f);
        return fontMetrics(f.size);
    }
    /// @}

    /// @name Paths
    /// Kept as raw verbs rather than a Path object: neui's path state lives in
    /// the painter for the duration of one build, so a detached Path value would
    /// be lying about where the data is.
    /// @{
    void beginPath() { api_->begin_path(p_); }
    void moveTo(Point pt) { api_->move_to(p_, pt.x, pt.y); }
    void lineTo(Point pt) { api_->line_to(p_, pt.x, pt.y); }
    void arcTo(Point centre, float r, float a0, float a1)
    {
        api_->arc(p_, centre.x, centre.y, r, a0, a1);
    }
    void cubicTo(Point c1, Point c2, Point end)
    {
        api_->cubic_to(p_, c1.x, c1.y, c2.x, c2.y, end.x, end.y);
    }
    void closePath() { api_->close_path(p_); }
    void fillPath(Color c) { api_->fill_path(p_, c.argb()); }
    void strokePath(float thickness, Color c) { api_->stroke_path(p_, thickness, c.argb()); }
    /// @}

    /// @name Escape hatch
    /// For anything not wrapped yet. If you reach for these often, the wrapper
    /// is missing a method - add it here rather than at the call site.
    /// @{
    neui_painter_api_t *rawApi() const { return api_; }
    neui_painter_t *rawHandle() const { return p_; }
    /// @}

  private:
    static neui_text_halign_t toNeui(HAlign h)
    {
        switch (h)
        {
        case HAlign::centre:
            return NEUI_TEXT_ALIGN_CENTER;
        case HAlign::right:
            return NEUI_TEXT_ALIGN_END;
        case HAlign::left:
        default:
            return NEUI_TEXT_ALIGN_START;
        }
    }
    static neui_text_valign_t toNeui(VAlign v)
    {
        switch (v)
        {
        case VAlign::top:
            return NEUI_TEXT_VALIGN_TOP;
        case VAlign::bottom:
            return NEUI_TEXT_VALIGN_BOTTOM;
        case VAlign::middle:
        default:
            return NEUI_TEXT_VALIGN_MIDDLE;
        }
    }

    neui_painter_api_t *api_;
    neui_painter_t *p_;
    Rect bounds_;
    bool focused_;
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_DRAW_CANVAS_H
