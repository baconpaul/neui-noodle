/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 *
 * ComponentCore and Frame: widget lifetime, the design-unit -> device-pixel
 * geometry boundary, and the thin forwards to neui's optional interfaces.
 */

#include <neuiplusplus/Component.h>

#include <algorithm>
#include <cmath>

namespace neuiplusplus
{

// ---------------------------------------------------------------------------
// Lifetime

ComponentCore::ComponentCore(Parent p) : session_(&p.of.session()), parent_(&p.of)
{
    widget_ = session_->widgets()->create(session_->raw(), p.of.widget(), NEUI_W_CUSTOMDRAW, 0, 0,
                                          1, 1, nullptr);
}

ComponentCore::ComponentCore(Session &s, neui_widget_t w, Rect bounds, RootTag)
    : session_(&s), widget_(w), bounds_(bounds)
{
}

ComponentCore::~ComponentCore()
{
    // Explicit: children_ is a base member, so without this it would outlive the
    // destroy() below and we would be tearing down parent-first.
    children_.clear();

    if (session_ && widget_.id != widget_none.id)
    {
        session_->unbind(widget_);
        session_->widgets()->destroy(session_->raw(), widget_);
    }
}

void ComponentCore::registerChild(ComponentCore &child, const detail::Bindings &b)
{
    session_->bind(child.widget(), b);
    // A component with no input interface is never hit-tested.
    session_->widgets()->set_emit_events(session_->raw(), child.widget(), b.wantsInput());
    if (b.focus)
        session_->widgets()->set_tab_stop(session_->raw(), child.widget(), true);

    // NO automatic role. An earlier version defaulted non-interactive components
    // to Role::none as a "decorative" convenience, which was wrong: ROLE_NONE
    // removes the node AND ITS WHOLE SUBTREE, so a plain container panel took
    // every control in the editor out of the tree with it. Roles are declared
    // explicitly, per widget, by the widget that knows what it is; a container
    // left alone reports ROLE_GROUP, which is exactly right for it.
    child.applyBounds();
}

// ---------------------------------------------------------------------------
// Geometry

void ComponentCore::setBounds(Rect r)
{
    bounds_ = r;
    applyBounds();
}

void ComponentCore::applyBounds()
{
    if (!session_ || widget_.id == widget_none.id)
        return;
    const float z = session_->zoom();
    // Snap EDGES, not position-and-size independently, so neighbours share a
    // pixel boundary at fractional zoom instead of gapping or overlapping.
    const int x0 = int(std::lround(bounds_.getX() * z));
    const int y0 = int(std::lround(bounds_.getY() * z));
    const int x1 = int(std::lround(bounds_.getRight() * z));
    const int y1 = int(std::lround(bounds_.getBottom() * z));
    session_->widgets()->set_pos(session_->raw(), widget_, x0, y0, std::max(0, x1 - x0),
                                 std::max(0, y1 - y0));
}

// ---------------------------------------------------------------------------
// State

void ComponentCore::setVisible(bool v)
{
    visible_ = v;
    if (!session_)
        return;
    if (v)
        session_->widgets()->show(session_->raw(), widget_);
    else
        session_->widgets()->hide(session_->raw(), widget_);
}

void ComponentCore::setEnabled(bool e)
{
    enabled_ = e;
    if (session_)
        session_->widgets()->set_enabled(session_->raw(), widget_, e);
}

// ---------------------------------------------------------------------------
// Capability-gated operations. Reached through detail::CoreAccess only.

void ComponentCore::repaintImpl() const
{
    if (session_ && widget_.id != widget_none.id)
        session_->widgets()->invalidate(session_->raw(), widget_);
}

void ComponentCore::takeFocusImpl()
{
    if (session_)
        session_->widgets()->set_focus(session_->raw(), widget_);
}

void ComponentCore::setCursorImpl(Cursor c)
{
    if (session_ && session_->attrs())
        session_->attrs()->set_string(session_->raw(), widget_, NEUI_ATTR_CURSOR, cursorName(c));
}

bool ComponentCore::beginRelativeDragImpl()
{
    if (!session_ || !session_->pointer())
        return false;
    return session_->pointer()->begin_relative(session_->raw(), widget_);
}

void ComponentCore::endRelativeDragImpl()
{
    if (session_ && session_->pointer())
        session_->pointer()->end_relative(session_->raw());
}

// ---------------------------------------------------------------------------
// Accessibility. Every one is a no-op when the host has no NEUI_API_A11Y, which
// is every host but crossplatform.

void ComponentCore::setAccessibleRole(Role r)
{
    if (session_ && session_->a11y())
        session_->a11y()->set_role(session_->raw(), widget_, neui_a11y_role_t(r));
}

void ComponentCore::setAccessibleName(const char *utf8)
{
    if (session_ && session_->a11y())
        session_->a11y()->set_name(session_->raw(), widget_, utf8);
}

void ComponentCore::setAccessibleDescription(const char *utf8)
{
    if (session_ && session_->a11y())
        session_->a11y()->set_description(session_->raw(), widget_, utf8);
}

void ComponentCore::setAccessibleValueRange(float min, float max, float step)
{
    if (session_ && session_->a11y())
        session_->a11y()->set_value_range(session_->raw(), widget_, min, max, step);
}

void ComponentCore::setAccessibleValue(float normalized)
{
    if (session_ && session_->a11y())
        session_->a11y()->set_value(session_->raw(), widget_, normalized);
}

void ComponentCore::setAccessibleValueText(const char *utf8)
{
    if (session_ && session_->a11y())
        session_->a11y()->set_value_text(session_->raw(), widget_, utf8);
}

void ComponentCore::notifyAccessible(A11yChange c)
{
    if (session_ && session_->a11y())
        session_->a11y()->notify(session_->raw(), widget_, neui_a11y_change_t(c));
}

// ---------------------------------------------------------------------------
// Frame

Frame::Frame(Session &s, const char *widgetType, Rect bounds, const char *title)
    : Component(
          s,
          s.widgets()->create(s.raw(), widget_none, widgetType, int(std::lround(bounds.getX())),
                              int(std::lround(bounds.getY())), int(std::lround(bounds.getWidth())),
                              int(std::lround(bounds.getHeight())), nullptr),
          bounds, ComponentCore::RootTag{})
{
    if (title)
        s.widgets()->set_text(s.raw(), widget(), title);
}

void Frame::show() { session().widgets()->show(session().raw(), widget()); }

Rect Frame::clientBounds() const
{
    int x = 0, y = 0, w = 0, h = 0;
    session().widgets()->get_client_rect(session().raw(), widget(), &x, &y, &w, &h);
    const float z = session().zoom();
    return {x / z, y / z, w / z, h / z};
}

} // namespace neuiplusplus
