/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_CURSOR_H
#define NEUIPLUSPLUS_CURSOR_H

/**
 * @file
 * @brief Pointer shapes, over neui's `NEUI_ATTR_CURSOR` string attribute.
 *
 * Two things about that attribute are worth knowing at this layer:
 *
 *  - It is resolved by the CROSSPLATFORM host only. The native win32 / macOS
 *    hosts do not read it, because their widgets are real OS controls that
 *    manage their own cursors.
 *  - It only takes effect on a widget that hit-tests, i.e. one with
 *    `emit_events`. Here that means a component naming at least one input
 *    interface; setting a cursor on a decorative component is silently a no-op.
 *    That is why interfaces::MouseEvents, not ComponentCore, owns setCursor.
 *
 * Not every shape exists on every OS - neui falls back to the nearest
 * neighbour rather than to the arrow, and macOS cannot honour wait / progress
 * at all.
 */

namespace neuiplusplus
{

/**
 * @brief A pointer shape.
 *
 * @ref Cursor::inherit is the default and defers to the nearest ancestor that
 * sets one, falling back to the OS arrow; @ref Cursor::arrow explicitly stops
 * inheriting.
 */
enum class Cursor
{
    inherit,
    arrow,
    ibeam,
    crosshair,
    hand,
    openHand,
    closedHand,
    ewResize,
    nsResize,
    neswResize,
    nwseResize,
    move,
    wait,
    progress,
    help,
    notAllowed,
    hidden
};

/**
 * @brief The canonical neui spelling of @p c.
 * @note Canonical rather than an alias, so a `get_string` round-trip compares equal.
 */
inline const char *cursorName(Cursor c)
{
    switch (c)
    {
    case Cursor::arrow:
        return "arrow";
    case Cursor::ibeam:
        return "ibeam";
    case Cursor::crosshair:
        return "crosshair";
    case Cursor::hand:
        return "hand";
    case Cursor::openHand:
        return "open-hand";
    case Cursor::closedHand:
        return "closed-hand";
    case Cursor::ewResize:
        return "ew-resize";
    case Cursor::nsResize:
        return "ns-resize";
    case Cursor::neswResize:
        return "nesw-resize";
    case Cursor::nwseResize:
        return "nwse-resize";
    case Cursor::move:
        return "move";
    case Cursor::wait:
        return "wait";
    case Cursor::progress:
        return "progress";
    case Cursor::help:
        return "help";
    case Cursor::notAllowed:
        return "not-allowed";
    case Cursor::hidden:
        return "none";
    case Cursor::inherit:
    default:
        return "default";
    }
}

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_CURSOR_H
