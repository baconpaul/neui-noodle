/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_INTERFACES_PAINTS_H
#define NEUIPLUSPLUS_INTERFACES_PAINTS_H

#include "../detail/CoreAccess.h"

/**
 * @file
 * @brief interfaces::Paints - "this component draws itself".
 */

namespace neuiplusplus
{
class Canvas;

namespace interfaces
{

/**
 * @brief Names a component as painting, and obliges it to implement @ref paint.
 *
 * Carries @ref repaint, because invalidating a component that never paints does
 * nothing - the reason capabilities are named separately in the first place.
 *
 * @tparam Derived the component's own type (CRTP).
 */
template <class Derived> struct Paints
{
    virtual ~Paints() = default;

    /**
     * @brief Draw the component. Called with the zoom scale already installed,
     *        so @p g is in design units with the origin at the component's
     *        top-left.
     */
    virtual void paint(Canvas &g) = 0;

    /** @brief Ask for a repaint. Coalesced by the host; safe to call often. */
    void repaint() const { detail::CoreAccess::repaint(self()); }

  private:
    const Derived &self() const { return *static_cast<const Derived *>(this); }
};

} // namespace interfaces
} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_INTERFACES_PAINTS_H
