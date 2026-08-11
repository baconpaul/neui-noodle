/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_INTERFACES_FOCUSEVENTS_H
#define NEUIPLUSPLUS_INTERFACES_FOCUSEVENTS_H

#include "../detail/CoreAccess.h"

/**
 * @file
 * @brief interfaces::FocusEvents - "this component can hold keyboard focus".
 */

namespace neuiplusplus::interfaces
{

/**
 * @brief Names a component as focusable.
 *
 * Declaring this also makes the component a keyboard TAB STOP, which is what
 * neui's traversal - and its accessibility walk - reads. So it is not only about
 * drawing a focus ring: it is how a component joins the tab order at all.
 *
 * @tparam Derived the component's own type (CRTP).
 */
template <class Derived> struct FocusEvents
{
    virtual ~FocusEvents() = default;

    virtual void focusGained() {}
    virtual void focusLost() {}

    /** @brief Move focus here - the usual response to a mouse press on a control. */
    void takeFocus() { detail::CoreAccess::takeFocus(self()); }

  private:
    Derived &self() { return *static_cast<Derived *>(this); }
};

} // namespace neuiplusplus::interfaces

#endif // NEUIPLUSPLUS_INTERFACES_FOCUSEVENTS_H
