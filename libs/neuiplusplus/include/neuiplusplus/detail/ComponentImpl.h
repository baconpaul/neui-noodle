/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_DETAIL_COMPONENTIMPL_H
#define NEUIPLUSPLUS_DETAIL_COMPONENTIMPL_H

#include <type_traits>

#include "../Events.h"
#include "../interfaces/Interfaces.h"

/**
 * @file
 * @brief How a component's capability set becomes a dispatch table. Internal.
 *
 * Nothing here is public API - it is the machinery behind
 * ComponentCore::add<T>, and it lives in its own header so Component.h can be
 * read as the description of a component rather than as a template exercise.
 *
 * THE PROBLEM. The dispatcher holds `ComponentCore *`, but the handlers live on
 * capability interfaces that are class TEMPLATES: `Paints<Knob>` and
 * `Paints<Slider>` share no base to store a pointer to, and a `dynamic_cast`
 * would need RTTI and a complete object.
 *
 * THE ANSWER. Resolve the set at the one moment the concrete type is statically
 * known - inside `add<T>`, after the object is fully constructed - with
 * `is_base_of_v`, and store the result as plain function pointers into
 * per-capability thunks. No RTTI, no virtual base, one indirect call per event.
 */

namespace neuiplusplus
{
class Canvas;
class ComponentCore;

namespace detail
{

/**
 * @brief Which mouse handler an event fans out to.
 * @note Internal: the thunk turns this back into a named call, so the kind never
 *       reaches component code.
 */
enum class MouseKind
{
    enter,
    exit,
    move,
    down,
    drag,
    up,
    doubleClick,
    rightDown,
    rightUp
};

/** @brief Which key handler an event fans out to. Internal. */
enum class KeyKind
{
    pressed,
    typed,
    released
};

/** @brief One component's resolved capability set. A null pointer means "not named". */
struct Bindings
{
    ComponentCore *self{nullptr};
    void *obj{nullptr}; ///< the DERIVED object; each thunk casts back to its own T

    void (*paint)(void *, Canvas &){nullptr};
    void (*resized)(void *){nullptr};
    void (*mouse)(void *, const MouseEvent &, MouseKind){nullptr};
    void (*wheel)(void *, const WheelEvent &){nullptr};
    bool (*key)(void *, const KeyEvent &, KeyKind){nullptr};
    void (*focus)(void *, bool){nullptr};

    /** @brief Drives neui's `emit_events`: a component handling no input is never hit-tested. */
    bool wantsInput() const { return mouse != nullptr || key != nullptr || focus != nullptr; }
};

/// @name Thunks
/// One per capability, instantiated for the concrete component type.
/// @{
template <class T> void paintThunk(void *s, Canvas &g) { static_cast<T *>(s)->paint(g); }
template <class T> void resizedThunk(void *s) { static_cast<T *>(s)->resized(); }

template <class T> void mouseThunk(void *s, const MouseEvent &e, MouseKind k)
{
    T &t = *static_cast<T *>(s);
    switch (k)
    {
    case MouseKind::enter:
        t.mouseEnter(e);
        break;
    case MouseKind::exit:
        t.mouseExit(e);
        break;
    case MouseKind::move:
        t.mouseMove(e);
        break;
    case MouseKind::down:
        t.mouseDown(e);
        break;
    case MouseKind::drag:
        t.mouseDrag(e);
        break;
    case MouseKind::up:
        t.mouseUp(e);
        break;
    case MouseKind::doubleClick:
        t.mouseDoubleClick(e);
        break;
    case MouseKind::rightDown:
        t.mouseRightButtonDown(e);
        break;
    case MouseKind::rightUp:
        t.mouseRightButtonUp(e);
        break;
    }
}

template <class T> void wheelThunk(void *s, const WheelEvent &e)
{
    static_cast<T *>(s)->mouseWheel(e);
}

template <class T> bool keyThunk(void *s, const KeyEvent &e, KeyKind k)
{
    T &t = *static_cast<T *>(s);
    switch (k)
    {
    case KeyKind::pressed:
        return t.keyPressed(e);
    case KeyKind::typed:
        return t.keyTyped(e);
    case KeyKind::released:
        // keyUp returns void, so there is no way for a component to claim a
        // release. Report it unconsumed and let neui see it too.
        t.keyUp(e);
        return false;
    }
    return false;
}

template <class T> void focusThunk(void *s, bool gained)
{
    T &t = *static_cast<T *>(s);
    if (gained)
        t.focusGained();
    else
        t.focusLost();
}
/// @}

/**
 * @brief Resolve @p t's capability set into a dispatch table.
 * @tparam T the concrete component type, complete and fully constructed.
 *
 * Called from ComponentCore::add<T> and nowhere else. A `dynamic_cast` in the
 * core constructor could not do this job: the derived object does not exist yet.
 */
template <class T> Bindings bindingsFor(T &t)
{
    static_assert(std::is_base_of_v<ComponentCore, T>,
                  "components must derive from Component<Self, ...>");
    static_assert(std::is_base_of_v<interfaces::Paints<T>, T> ||
                      std::is_base_of_v<interfaces::Resizes<T>, T> ||
                      std::is_base_of_v<interfaces::MouseEvents<T>, T> ||
                      std::is_base_of_v<interfaces::KeyboardEvents<T>, T> ||
                      std::is_base_of_v<interfaces::FocusEvents<T>, T>,
                  "a component that names no capability would never be called - "
                  "did you mean Component<Self, Paints>?");

    Bindings b;
    b.self = static_cast<ComponentCore *>(&t);
    b.obj = &t;
    if constexpr (std::is_base_of_v<interfaces::Paints<T>, T>)
        b.paint = &paintThunk<T>;
    if constexpr (std::is_base_of_v<interfaces::Resizes<T>, T>)
        b.resized = &resizedThunk<T>;
    if constexpr (std::is_base_of_v<interfaces::MouseEvents<T>, T>)
    {
        b.mouse = &mouseThunk<T>;
        b.wheel = &wheelThunk<T>;
    }
    if constexpr (std::is_base_of_v<interfaces::KeyboardEvents<T>, T>)
        b.key = &keyThunk<T>;
    if constexpr (std::is_base_of_v<interfaces::FocusEvents<T>, T>)
        b.focus = &focusThunk<T>;
    return b;
}

} // namespace detail
} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_DETAIL_COMPONENTIMPL_H
