/*
 * neuiplusplus - a C++20 skin over the neui C API
 *
 * Capability interfaces. A component composes the ones it needs:
 *
 *   struct Knob : neuiplusplus::Component,
 *                 neuiplusplus::Paintable,
 *                 neuiplusplus::MouseHandling
 *   {
 *       void paint(Canvas &) override;
 *       void mouseDown(const MouseEvent &) override;
 *       void mouseDrag(const MouseEvent &) override;
 *   };
 *
 * Why interfaces rather than concepts + if-constexpr detection: `override` is
 * checked and a duck-typed handler name is not. A misspelled or wrongly-signed
 * `mouseDown` under detection compiles clean and silently never fires, which is
 * the worst failure mode a UI toolkit can have. Build with
 * -Wsuggest-override / -Winconsistent-missing-override to close the remaining
 * hole (an override written without the keyword).
 *
 * These are pure interfaces - no state, no data members - so a component
 * inherits several by plain multiple inheritance with no diamond and no
 * virtual bases. Component is the only base carrying state.
 *
 * The set is small on purpose. neui delivers paint, resize, ten mouse events,
 * three key events and focus to a CUSTOMDRAW widget; there is no lifecycle /
 * look-and-feel notification surface to model. For scale: the busiest widget
 * in sst-jucegui (ContinuousParamEditor) overrides thirteen hooks, and every
 * one of them appears below.
 */

#ifndef NEUIPLUSPLUS_INTERFACES_H
#define NEUIPLUSPLUS_INTERFACES_H

#include "events.h"

namespace neuiplusplus
{

class Canvas;

// Draws itself. Separate from Component so a pure container costs no paint
// dispatch and declares that it draws nothing.
struct Paintable
{
    virtual ~Paintable() = default;
    virtual void paint(Canvas &g) = 0;
};

// Lays its children out. Called after every bounds change, before the repaint.
struct Resizable
{
    virtual ~Resizable() = default;
    virtual void resized() = 0;
};

// Inheriting this is what makes the component hit-testable at all - the
// dispatcher leaves neui's emit_events off for a component that handles no
// input, which is also the natural "decorative" marker for accessibility.
struct MouseHandling
{
    virtual ~MouseHandling() = default;

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
};

struct KeyboardHandling
{
    virtual ~KeyboardHandling() = default;

    // Return true to consume. Unconsumed keys fall through to neui.
    virtual bool keyPressed(const KeyEvent &) { return false; }
    virtual bool keyTyped(const KeyEvent &) { return false; }
    virtual void keyUp(const KeyEvent &) {}
};

// Implement to react to focus; inheriting it also makes the component a
// keyboard tab stop, which is what neui's traversal - and later its
// accessibility walk - reads.
struct FocusHandling
{
    virtual ~FocusHandling() = default;

    virtual void focusGained() {}
    virtual void focusLost() {}
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_INTERFACES_H
