/*
 * neuiplusplus - a C++20 skin over the neui C API
 *
 * Pointer shapes, mapped onto neui's NEUI_ATTR_CURSOR string attribute.
 *
 * Two things about that attribute worth knowing at this layer:
 *   - It is resolved by the CROSSPLATFORM host only. The native win32 / macOS
 *     hosts do not read it, because their widgets are real OS controls that
 *     manage their own cursors.
 *   - It only takes effect on a widget that hit-tests, i.e. one with
 *     emit_events. In this library that means a component inheriting
 *     MouseHandling / KeyboardHandling / FocusHandling; setting a cursor on a
 *     decorative component is silently a no-op.
 *
 * `inherit` is the default and defers to the nearest ancestor that sets one,
 * falling back to the OS arrow; `arrow` explicitly stops inheriting. Not every
 * shape exists on every OS - neui falls back to the nearest neighbour rather
 * than to the arrow, and macOS cannot honour wait / progress at all.
 */

#ifndef NEUIPLUSPLUS_CURSOR_H
#define NEUIPLUSPLUS_CURSOR_H

namespace neuiplusplus
{

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

// The canonical neui spelling. Canonical rather than an alias so a get_string
// round-trip compares equal.
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
