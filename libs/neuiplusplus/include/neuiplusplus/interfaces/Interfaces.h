/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_INTERFACES_INTERFACES_H
#define NEUIPLUSPLUS_INTERFACES_INTERFACES_H

#include "FocusEvents.h"
#include "KeyboardEvents.h"
#include "MouseEvents.h"
#include "Paints.h"
#include "Resizes.h"

/**
 * @file
 * @brief Every capability interface, and their short names.
 *
 * A component names the capabilities it has in its base declaration and gets
 * both the handlers to override and the operations that only make sense given
 * that capability:
 *
 * @code
 * struct Knob : npp::Component<Knob, npp::Paints, npp::MouseEvents,
 *                              npp::KeyboardEvents, npp::FocusEvents>
 * {
 *     void paint(npp::Canvas &) override;
 *     void mouseDrag(const npp::MouseEvent &) override;
 * };
 * @endcode
 *
 * TWO THINGS EACH INTERFACE CARRIES.
 *
 * 1. Virtual handlers. Virtual rather than duck-typed on purpose: `override` is
 *    compiler-checked, and a misspelled or wrongly-signed handler under
 *    detection would compile clean and silently never fire - the worst failure
 *    mode a UI toolkit can have. Build with `-Wsuggest-override` (clang:
 *    `-Winconsistent-missing-override`) to close the remaining hole, an override
 *    written without the keyword.
 *
 * 2. Operations that are meaningless without it. `repaint()` lives on Paints
 *    because invalidating a component that never paints does nothing;
 *    `beginRelativeDrag()` on MouseEvents; `takeFocus()` on FocusEvents. That is
 *    the whole point of naming capabilities separately - a decorative label
 *    should not offer a `repaint()` that quietly does nothing. State that
 *    belongs to a capability lives in the capability too, so a component without
 *    MouseEvents has no cursor field at all.
 *
 * NAMES. The definitions live in `neuiplusplus::interfaces`, one per file,
 * because "Paints" and "Resizes" are generic enough to want a home of their own.
 * They are then re-exported into `neuiplusplus` below, because a base-clause
 * reading `npp::interfaces::MouseEvents` four times over buys nothing. Both
 * spellings name the same entity; use whichever suits the call site.
 */

namespace neuiplusplus
{

using interfaces::FocusEvents;
using interfaces::KeyboardEvents;
using interfaces::MouseEvents;
using interfaces::Paints;
using interfaces::Resizes;

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_INTERFACES_INTERFACES_H
