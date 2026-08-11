/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_INTERFACES_RESIZES_H
#define NEUIPLUSPLUS_INTERFACES_RESIZES_H

/**
 * @file
 * @brief interfaces::Resizes - "this component lays its children out".
 */

namespace neuiplusplus::interfaces
{

/**
 * @brief Names a component as laying out on every bounds change.
 *
 * @ref resized runs after the new bounds are in place and before the repaint,
 * so it may read ComponentCore::localBounds and call setBounds on children.
 *
 * @tparam Derived the component's own type (CRTP).
 */
template <class Derived> struct Resizes
{
    virtual ~Resizes() = default;

    /** @brief Position the children. Called after every bounds change. */
    virtual void resized() = 0;
};

} // namespace neuiplusplus::interfaces

#endif // NEUIPLUSPLUS_INTERFACES_RESIZES_H
