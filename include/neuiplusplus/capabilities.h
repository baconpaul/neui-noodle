/*
 * neuiplusplus - a C++20 skin over the neui C API
 *
 * Capabilities. A component names the ones it has in its base declaration and
 * gets both the handlers to override and the operations that only make sense
 * given that capability:
 *
 *   struct Knob : npp::Component<Knob, npp::Paints, npp::MouseEvents,
 *                                npp::KeyboardEvents, npp::FocusEvents>
 *   {
 *       void paint(npp::Canvas &) override;
 *       void mouseDrag(const npp::MouseEvent &) override;
 *   };
 *
 * TWO THINGS EACH CAPABILITY CARRIES.
 *
 * 1. Virtual handlers. Virtual rather than duck-typed on purpose: `override` is
 *    compiler-checked, and a misspelled or wrongly-signed handler under
 *    detection would compile clean and silently never fire - the worst failure
 *    mode a UI toolkit can have. Build with -Wsuggest-override to close the
 *    remaining hole (an override written without the keyword).
 *
 * 2. Operations that are meaningless without it. repaint() lives on Paints
 *    because invalidating a component that never paints does nothing;
 *    beginRelativeDrag() lives on MouseEvents; takeFocus() on FocusEvents.
 *    That is the whole point of naming capabilities separately - a decorative
 *    label should not offer a repaint() that quietly does nothing.
 *
 * State that belongs to a capability lives in the capability, so a component
 * without MouseEvents has no cursor field at all.
 *
 * Reaching the shared implementation goes through detail::CoreAccess: a
 * capability is a SIBLING of ComponentCore under Component, not a subclass, so
 * it has no protected access of its own. One friend declaration on the core
 * serves every capability, present and future. (C++26's P2893 variadic friends
 * would let the core befriend the pack directly; on C++20 the access key is
 * the way.)
 */

#ifndef NEUIPLUSPLUS_CAPABILITIES_H
#define NEUIPLUSPLUS_CAPABILITIES_H

#include "cursor.h"
#include "events.h"

namespace neuiplusplus
{

class Canvas;
class ComponentCore;

namespace detail
{
// The single friend of ComponentCore. Capabilities route through it rather than
// each needing friendship of their own.
struct CoreAccess
{
    template <class C> static void repaint(const C &c) { c.repaintImpl(); }
    template <class C> static void setCursor(C &c, Cursor k) { c.setCursorImpl(k); }
    template <class C> static bool beginRelativeDrag(C &c) { return c.beginRelativeDragImpl(); }
    template <class C> static void endRelativeDrag(C &c) { c.endRelativeDragImpl(); }
    template <class C> static void takeFocus(C &c) { c.takeFocusImpl(); }
};
} // namespace detail

// ---------------------------------------------------------------------------

template <class Derived> struct Paints
{
    virtual ~Paints() = default;
    virtual void paint(Canvas &g) = 0;

    // Only components that paint can usefully be invalidated.
    void repaint() const { detail::CoreAccess::repaint(self()); }

  private:
    const Derived &self() const { return *static_cast<const Derived *>(this); }
};

template <class Derived> struct Resizes
{
    virtual ~Resizes() = default;
    // Called after every bounds change, before the repaint.
    virtual void resized() = 0;
};

template <class Derived> struct MouseEvents
{
    virtual ~MouseEvents() = default;

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

    // A pointer shape is about the pointer, so it belongs to the capability
    // that receives it. No-op on the native hosts, which do not read the attr.
    void setCursor(Cursor k)
    {
        cursor_ = k;
        detail::CoreAccess::setCursor(self(), k);
    }
    Cursor cursor() const { return cursor_; }

    // Relative (unbounded) pointer for a value drag: the visible cursor pins
    // and hides, motion keeps arriving past the screen edge, and on end the
    // cursor is restored to where the press happened.
    //
    // CALL beginRelativeDrag FROM mouseDown, not elsewhere - neui seeds the
    // virtual position from the last dispatched mouse event, which inside a
    // down handler is that very press. Returns false when the host has no
    // pointer API, so a caller degrades to an ordinary bounded drag.
    // endRelativeDrag is safe unconditionally.
    //
    // Only for DELTA-driven drags. A control that maps position to value
    // absolutely wants the pointer bounded.
    bool beginRelativeDrag() { return detail::CoreAccess::beginRelativeDrag(self()); }
    void endRelativeDrag() { detail::CoreAccess::endRelativeDrag(self()); }

  private:
    Derived &self() { return *static_cast<Derived *>(this); }
    Cursor cursor_{Cursor::inherit};
};

template <class Derived> struct KeyboardEvents
{
    virtual ~KeyboardEvents() = default;

    // Return true to consume; unconsumed keys fall through to neui.
    virtual bool keyPressed(const KeyEvent &) { return false; }
    virtual bool keyTyped(const KeyEvent &) { return false; }
    virtual void keyUp(const KeyEvent &) {}
};

// Declaring this also makes the component a keyboard tab stop, which is what
// neui's traversal - and its accessibility walk - reads.
template <class Derived> struct FocusEvents
{
    virtual ~FocusEvents() = default;

    virtual void focusGained() {}
    virtual void focusLost() {}

    void takeFocus() { detail::CoreAccess::takeFocus(self()); }

  private:
    Derived &self() { return *static_cast<Derived *>(this); }
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_CAPABILITIES_H
