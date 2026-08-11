/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_COMPONENTS_LABEL_H
#define NEUIPLUSPLUS_COMPONENTS_LABEL_H

#include <string>
#include <utility>

#include "../Component.h"
#include "../draw/Canvas.h"
#include "../draw/Color.h"
#include "../draw/Font.h"

/**
 * @file
 * @brief @ref neuiplusplus::Label - a line of text.
 *
 * The first inhabitant of `components/`, and the smallest complete example of
 * what a component looks like from the outside: it names one capability, so it
 * has `repaint()` and nothing else it cannot use.
 */

namespace neuiplusplus
{

/**
 * @brief Static text, optionally on a filled background.
 *
 * Names @ref interfaces::Paints only. So neui leaves `emit_events` off and the
 * label is never hit-tested - clicks land on whatever is behind it, which is
 * what you want from a caption.
 *
 * Declares @ref Role::staticText and keeps its accessible name in step with
 * @ref setText, because client-drawn text is invisible to the framework and
 * would otherwise reach a screen reader as an anonymous group.
 *
 * @code
 * auto &title = panel.add<npp::Label>("Filter");
 * title.setFont({"Inter", 14.0f, npp::FontWeight::semiBold});
 * title.setForeground(pal.ink);
 * title.setBounds(area.removeFromTop(20.0f));
 * @endcode
 *
 * @note A LEAF type. Components derive from `Component<Self, ...>`, not from
 *       another component - the capability set is resolved against the exact
 *       type passed to `add<T>`, so a subclass of Label would not find
 *       `Paints<Subclass>` among its bases. Compose instead: hold a Label as a
 *       child.
 */
class Label : public Component<Label, interfaces::Paints>
{
  public:
    explicit Label(Parent p, std::string initialText = {})
        : Component(p), text_(std::move(initialText))
    {
        setAccessibleRole(Role::staticText);
        setAccessibleName(text_.c_str());
    }

    /// @name Content
    /// @{
    /** @brief Set the text, republish the accessible name, and repaint. */
    void setText(std::string t)
    {
        if (t == text_)
            return;
        text_ = std::move(t);
        setAccessibleName(text_.c_str());
        notifyAccessible(A11yChange::name);
        repaint();
    }
    const std::string &text() const { return text_; }
    /// @}

    /// @name Appearance
    /// @{
    void setFont(Font f)
    {
        if (f == font_)
            return;
        font_ = std::move(f);
        repaint();
    }
    const Font &font() const { return font_; }

    /** @brief The text colour. */
    void setForeground(Color c)
    {
        if (c == foreground_)
            return;
        foreground_ = c;
        repaint();
    }
    Color foreground() const { return foreground_; }

    /** @brief The fill behind the text. Transparent (the default) draws nothing. */
    void setBackground(Color c)
    {
        if (c == background_)
            return;
        background_ = c;
        repaint();
    }
    Color background() const { return background_; }

    /** @brief Where the text sits in the label's bounds. Defaults to left / middle. */
    void setJustification(HAlign h, VAlign v = VAlign::middle)
    {
        if (h == hAlign_ && v == vAlign_)
            return;
        hAlign_ = h;
        vAlign_ = v;
        repaint();
    }

    /** @brief Inset applied to both the fill and the text. Defaults to 0. */
    void setPadding(float px)
    {
        if (px == padding_)
            return;
        padding_ = px;
        repaint();
    }
    /// @}

    void paint(Canvas &g) override
    {
        const auto b = g.bounds().reduced(padding_);
        if (!background_.isTransparent())
            g.fillRect(g.bounds(), background_);
        if (!text_.empty())
            g.drawText(text_, b, font_, foreground_, hAlign_, vAlign_);
    }

  private:
    std::string text_;
    Font font_{};
    Color foreground_{colors::white};
    Color background_{colors::transparent};
    HAlign hAlign_{HAlign::left};
    VAlign vAlign_{VAlign::middle};
    float padding_{0.0f};
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_COMPONENTS_LABEL_H
