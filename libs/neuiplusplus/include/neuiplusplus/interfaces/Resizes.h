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
 * ComponentCore::setBounds fires @ref resized once the new bounds are in place,
 * so it may read ComponentCore::localBounds straight away - and a setBounds on a
 * child that also names Resizes cascades, so laying a tree out is one call at
 * the top.
 *
 * @note `NEUI_EVENT_RESIZE` is NOT how this arrives. neui delivers that to the
 * FRAME and nowhere else; a child component never sees one however it is
 * declared. Frame turns it into a Frame::onResize callback, and the setBounds
 * you make from there is what reaches everything below.
 *
 * @tparam Derived the component's own type (CRTP).
 */
template <class Derived> struct Resizes
{
    virtual ~Resizes() = default;

    /** @brief Position the children. Called on every bounds change. */
    virtual void resized() = 0;
};

} // namespace neuiplusplus::interfaces

#endif // NEUIPLUSPLUS_INTERFACES_RESIZES_H
