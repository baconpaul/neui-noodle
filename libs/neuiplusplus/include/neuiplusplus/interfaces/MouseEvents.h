/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_INTERFACES_MOUSEEVENTS_H
#define NEUIPLUSPLUS_INTERFACES_MOUSEEVENTS_H

#include "../Cursor.h"
#include "../Events.h"
#include "../detail/CoreAccess.h"

/**
 * @file
 * @brief interfaces::MouseEvents - "this component takes pointer input".
 */

namespace neuiplusplus::interfaces
{

/**
 * @brief Names a component as hit-testable, and hands it the pointer handlers.
 *
 * Every handler has an empty default, so a component overrides only the ones it
 * cares about. Naming this interface at all is what turns neui's `emit_events`
 * on for the widget.
 *
 * Carries the two operations that are about the pointer, and so belong to the
 * component that receives it: @ref setCursor and @ref beginRelativeDrag.
 *
 * @tparam Derived the component's own type (CRTP).
 */
template <class Derived> struct MouseEvents
{
    virtual ~MouseEvents() = default;

    /// @name Handlers
    /// @{
    virtual void mouseEnter(const MouseEvent &) {}
    virtual void mouseExit(const MouseEvent &) {}
    virtual void mouseMove(const MouseEvent &) {}
    virtual void mouseDown(const MouseEvent &) {}
    virtual void mouseDrag(const MouseEvent &) {}
    virtual void mouseUp(const MouseEvent &) {}
    virtual void mouseDoubleClick(const MouseEvent &) {}
    virtual void mouseRightButtonDown(const MouseEvent &) {}
    virtual void mouseRightButtonUp(const MouseEvent &) {}
    virtual void mouseWheel(const WheelEvent &) {}
    /// @}

    /**
     * @brief Set the pointer shape over this component.
     * @note No-op on the native hosts, which do not read `NEUI_ATTR_CURSOR`.
     * @see Cursor
     */
    void setCursor(Cursor k)
    {
        cursor_ = k;
        detail::CoreAccess::setCursor(self(), k);
    }
    Cursor cursor() const { return cursor_; }

    /**
     * @brief Enter relative (unbounded) pointer mode for a value drag.
     *
     * The visible cursor pins and hides, motion keeps arriving past the screen
     * edge, and on @ref endRelativeDrag the cursor is restored to where the
     * press happened.
     *
     * @warning CALL THIS FROM mouseDown, not elsewhere - neui seeds the virtual
     * position from the last dispatched mouse event, which inside a down handler
     * is that very press.
     *
     * @note Only for DELTA-driven drags. A control that maps position to value
     * absolutely wants the pointer bounded.
     *
     * @return false when the host has no pointer API, so a caller can degrade to
     *         an ordinary bounded drag. @ref endRelativeDrag is safe
     *         unconditionally.
     */
    bool beginRelativeDrag() { return detail::CoreAccess::beginRelativeDrag(self()); }
    void endRelativeDrag() { detail::CoreAccess::endRelativeDrag(self()); }

  private:
    Derived &self() { return *static_cast<Derived *>(this); }
    Cursor cursor_{Cursor::inherit};
};

} // namespace neuiplusplus::interfaces

#endif // NEUIPLUSPLUS_INTERFACES_MOUSEEVENTS_H
