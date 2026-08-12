/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_COMPONENTS_NATIVEWIDGET_H
#define NEUIPLUSPLUS_COMPONENTS_NATIVEWIDGET_H

#include <functional>
#include <string>
#include <vector>

#include "../Component.h"
#include "../Session.h"

/**
 * @file
 * @brief @ref neuiplusplus::NativeWidget - a real neui control, not a CUSTOMDRAW.
 */

namespace neuiplusplus
{

/**
 * @brief Wraps one of neui's built-in widgets (INPUTBOX, BUTTON, COMBOBOX, ...).
 *
 * Every other component here owns a `NEUI_W_CUSTOMDRAW` and paints itself. This
 * one deliberately does not: it exists for the cases where neui's own widget is
 * the right answer and reimplementing it would be a mistake. A text field is the
 * motivating case - IME, clipboard, undo, selection and word navigation are all
 * already there, and all of them are miserable to redo.
 *
 * @warning KEY HANDLING IS FIRST-REFUSAL, NOT INTERCEPTION. It names
 *          @ref interfaces::KeyboardEvents so an owner can claim a key such as
 *          Enter, and `Session::dispatch` returns whatever the handler returns:
 *          **false hands the keystroke back to neui's own INPUTBOX handling**.
 *          So @ref onKeyPressed must return true ONLY for keys it genuinely
 *          consumes. Return true unconditionally and the field stops accepting
 *          text - every character would be swallowed before neui sees it.
 *
 * @code
 * auto &field = row.add<npp::NativeWidget>(NEUI_W_INPUTBOX);
 * field.setText("120.0");
 * field.setBounds({4, 2, 120, 20});
 * field.onKeyPressed = [&](const npp::KeyEvent &e) {
 *     if (e.keyCode != NEUI_KEY_RETURN) return false;   // let neui type it
 *     commit(field.text());
 *     return true;
 * };
 * field.takeFocus();
 * @endcode
 */
class NativeWidget
    : public Component<NativeWidget, interfaces::FocusEvents, interfaces::KeyboardEvents>
{
  public:
    /** @param widgetType one of the `NEUI_W_*` type ids. */
    NativeWidget(Parent p, const char *widgetType) : Component(p, widgetType) {}

    /// @name Text
    /// @{
    void setText(const std::string &t)
    {
        session().widgets()->set_text(session().raw(), widget(), t.c_str());
    }

    /** @brief Read the live text back. Two-pass, as neui's C API requires. */
    std::string text() const
    {
        const int n = session().widgets()->get_text(session().raw(), widget(), nullptr, 0);
        if (n <= 1)
            return {};
        std::vector<char> buf(std::size_t(n), '\0');
        session().widgets()->get_text(session().raw(), widget(), buf.data(), n);
        return std::string(buf.data());
    }
    /// @}

    /// @name Focus
    /// `takeFocus()` comes from FocusEvents. These are the notifications.
    /// @{
    std::function<void()> onFocusGained;
    std::function<void()> onFocusLost;

    void focusGained() override
    {
        if (onFocusGained)
            onFocusGained();
    }
    void focusLost() override
    {
        if (onFocusLost)
            onFocusLost();
    }
    /// @}

    /// @name Keys
    /// @{
    /**
     * @brief First refusal on a key routed to this widget.
     * @return true to CONSUME it, false to let neui handle it normally.
     * @warning Returning true for anything but the specific keys you claim will
     *          stop the widget working. @see the class note.
     */
    std::function<bool(const KeyEvent &)> onKeyPressed;

    bool keyPressed(const KeyEvent &e) override { return onKeyPressed ? onKeyPressed(e) : false; }
    /// @}
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_COMPONENTS_NATIVEWIDGET_H
