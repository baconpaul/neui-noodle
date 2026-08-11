/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_DETAIL_COREACCESS_H
#define NEUIPLUSPLUS_DETAIL_COREACCESS_H

#include "../Cursor.h"

/**
 * @file
 * @brief The one friend of ComponentCore.
 *
 * A capability interface is a SIBLING of ComponentCore under Component, not a
 * subclass, so it has no protected access of its own. Rather than have
 * ComponentCore befriend every interface - a list that would have to grow with
 * each new one - it befriends this single access key, and the interfaces route
 * their capability-gated operations through it.
 *
 * C++26's P2893 variadic friends would let the core befriend the pack directly.
 * On C++20 the access key is the way.
 *
 * Templates rather than `ComponentCore &` parameters so this header does not
 * need the core to be complete; each call resolves where the interface is used.
 */

namespace neuiplusplus
{
class ComponentCore;

namespace detail
{

/** @brief Access key. Not part of the public API; interfaces only. */
struct CoreAccess
{
    template <class C> static void repaint(const C &c) { c.repaintImpl(); }
    template <class C> static void setCursor(C &c, Cursor k) { c.setCursorImpl(k); }
    template <class C> static bool beginRelativeDrag(C &c) { return c.beginRelativeDragImpl(); }
    template <class C> static void endRelativeDrag(C &c) { c.endRelativeDragImpl(); }
    template <class C> static void takeFocus(C &c) { c.takeFocusImpl(); }
};

} // namespace detail
} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_DETAIL_COREACCESS_H
