/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_INTERFACES_KEYBOARDEVENTS_H
#define NEUIPLUSPLUS_INTERFACES_KEYBOARDEVENTS_H

#include "../Events.h"

/**
 * @file
 * @brief interfaces::KeyboardEvents - "this component takes key input".
 */

namespace neuiplusplus::interfaces
{

/**
 * @brief Names a component as handling keys.
 *
 * Also the interface an ACTIONABLE accessible role obliges you to name: an
 * assistive technology performs a slider's increment or a button's press by
 * synthesising an ordinary key event, and on a hand-painted widget only you can
 * carry it out. @see A11y.h
 *
 * @tparam Derived the component's own type (CRTP).
 */
template <class Derived> struct KeyboardEvents
{
    virtual ~KeyboardEvents() = default;

    /** @return true to consume the key; unconsumed keys fall through to neui. */
    virtual bool keyPressed(const KeyEvent &) { return false; }
    /** @brief Text input, with KeyEvent::character set. @return true to consume. */
    virtual bool keyTyped(const KeyEvent &) { return false; }
    virtual void keyUp(const KeyEvent &) {}
};

} // namespace neuiplusplus::interfaces

#endif // NEUIPLUSPLUS_INTERFACES_KEYBOARDEVENTS_H
