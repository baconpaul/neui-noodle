/*
 * neuiplusplus - a C++20 skin over the neui C API
 *
 * Umbrella header. What this library is, and is not:
 *
 *   IS   - value types (Color, Rect, Point), a Canvas that binds neui's
 *          painter vtable to its opaque handle with RAII state guards,
 *          capability interfaces, and a Component that owns a CUSTOMDRAW
 *          widget plus its children.
 *   NOT  - styling, look and feel, or data binding. Those are a widget
 *          library's business (sst-neuigui), not this layer's. This layer
 *          only removes the mechanical friction of driving neui from C++.
 *
 * The one shape worth knowing before reading anything else:
 *
 *   struct Knob : neuiplusplus::Component<Knob,
 *                                        neuiplusplus::Paints,
 *                                        neuiplusplus::MouseEvents>
 *   {
 *       Knob(neuiplusplus::Parent p, Model &m) : Component(p), model(m) {}
 *
 *       void paint(neuiplusplus::Canvas &g) override
 *       {
 *           auto s = g.savedState();
 *           g.fillEllipse(g.bounds().reduced(2.0f), style.body);
 *       }
 *       void mouseDown(const neuiplusplus::MouseEvent &e) override { ... }
 *       void mouseDrag(const neuiplusplus::MouseEvent &e) override { repaint(); }
 *
 *       Model &model;
 *   };
 *
 *   auto &knob = panel.add<Knob>(model);   // returns Knob &, panel owns it
 *
 * Capabilities are virtual interfaces rather than detected member functions on
 * purpose: `override` is compiler-checked and a duck-typed handler name is not.
 * The templates are confined to add<T> and bindingsFor<T>, where the payoff is
 * resolving the capability set with no RTTI. See interfaces.h.
 *
 * Build with -Wsuggest-override (clang: -Winconsistent-missing-override).
 * It closes the last hole: an override written without the keyword.
 */

#ifndef NEUIPLUSPLUS_NEUIPLUSPLUS_H
#define NEUIPLUSPLUS_NEUIPLUSPLUS_H

#include "a11y.h"
#include "canvas.h"
#include "capabilities.h"
#include "color.h"
#include "component.h"
#include "cursor.h"
#include "events.h"
#include "geometry.h"

namespace neuipp = neuiplusplus;

#endif // NEUIPLUSPLUS_NEUIPLUSPLUS_H
