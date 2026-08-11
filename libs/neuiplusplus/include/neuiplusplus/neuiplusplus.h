/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_NEUIPLUSPLUS_H
#define NEUIPLUSPLUS_NEUIPLUSPLUS_H

#include "A11y.h"
#include "Component.h"
#include "Cursor.h"
#include "Events.h"
#include "FileDialog.h"
#include "Session.h"
#include "components/Label.h"
#include "draw/Canvas.h"
#include "draw/Color.h"
#include "draw/Font.h"
#include "draw/Geometry.h"
#include "draw/Transform.h"
#include "interfaces/Interfaces.h"

/**
 * @file
 * @brief Umbrella header. Include this, or the two or three you actually use.
 *
 * @mainpage neuiplusplus
 *
 * A C++20 skin over neui's C API. What this library is, and is not:
 *
 *  - **IS** value types (@ref neuiplusplus::Color "Color",
 *    @ref neuiplusplus::Rect "Rect", @ref neuiplusplus::Point "Point",
 *    @ref neuiplusplus::Transform "Transform", @ref neuiplusplus::Font "Font"),
 *    a @ref neuiplusplus::Canvas "Canvas" binding neui's painter vtable to its
 *    opaque handle with RAII state guards, capability interfaces, and a
 *    @ref neuiplusplus::Component "Component" owning a CUSTOMDRAW widget plus
 *    its children.
 *  - **NOT** styling, look and feel, or data binding. Those are a widget
 *    library's business, not this layer's. This layer only removes the
 *    mechanical friction of driving neui from C++.
 *
 * The one shape worth knowing before reading anything else:
 *
 * @code
 * struct Knob : npp::Component<Knob, npp::Paints, npp::MouseEvents>
 * {
 *     Knob(npp::Parent p, Model &m) : Component(p), model(m) {}
 *
 *     void paint(npp::Canvas &g) override
 *     {
 *         auto s = g.savedState();
 *         g.fillEllipse(g.bounds().reduced(2.0f), style.body);
 *     }
 *     void mouseDown(const npp::MouseEvent &e) override { ... }
 *     void mouseDrag(const npp::MouseEvent &e) override { repaint(); }
 *
 *     Model &model;
 * };
 *
 * auto &knob = panel.add<Knob>(model);   // returns Knob &, panel owns it
 * @endcode
 *
 * Capabilities are virtual interfaces rather than detected member functions on
 * purpose: `override` is compiler-checked and a duck-typed handler name is not.
 * The templates are confined to `add<T>` and `bindingsFor<T>`, where the payoff
 * is resolving the capability set with no RTTI.
 * @see interfaces/Interfaces.h
 *
 * @warning Build with `-Wsuggest-override` (clang:
 * `-Winconsistent-missing-override`, MSVC: `/w14263`). It closes the last hole
 * in the design - an override written without the keyword.
 *
 * ### Where things live
 *
 *  | header                     | holds                                        |
 *  | -------------------------- | -------------------------------------------- |
 *  | `Component.h`              | ComponentCore, Component<>, Frame, Parent     |
 *  | `Session.h`                | Session - handles, dispatch table, zoom       |
 *  | `Events.h`                 | MouseEvent, WheelEvent, KeyEvent, Modifiers   |
 *  | `interfaces/`              | one capability interface per file             |
 *  | `draw/`                    | Canvas, Color, Geometry, Font, Transform      |
 *  | `components/`              | ready-made widgets                            |
 *  | `A11y.h`, `Cursor.h`, `FileDialog.h` | the small vocabularies          |
 *  | `detail/`                  | internal; nothing here is API                 |
 */

/** @brief The conventional short alias. */
namespace neuipp = neuiplusplus;

#endif // NEUIPLUSPLUS_NEUIPLUSPLUS_H
