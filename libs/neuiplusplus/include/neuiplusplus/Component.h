/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_COMPONENT_H
#define NEUIPLUSPLUS_COMPONENT_H

#include <memory>
#include <utility>
#include <vector>

#include <neui/neui.h>

#include "A11y.h"
#include "Cursor.h"
#include "Events.h"
#include "Session.h"
#include "detail/ComponentImpl.h"
#include "draw/Canvas.h"
#include "draw/Geometry.h"
#include "interfaces/Interfaces.h"

/**
 * @file
 * @brief @ref neuiplusplus::Component - what you inherit to make a widget.
 *
 * @ref neuiplusplus::ComponentCore owns one `NEUI_W_CUSTOMDRAW` widget and the
 * children below it. @ref neuiplusplus::Component "Component<Derived, Caps...>"
 * is what you actually inherit: it adds the capability interfaces you name, each
 * parameterised on your own type.
 *
 * @code
 * struct Knob : npp::Component<Knob, npp::Paints, npp::MouseEvents> { ... };
 * @endcode
 *
 * WHY VARIADIC. A component's API surface then matches what it does: no
 * `repaint()` on something that never paints, no cursor field on something that
 * takes no input. Capability state lives in the capability, so that is literal
 * rather than a convention. @see interfaces/Interfaces.h
 *
 * The template machinery that turns `Caps...` into a dispatch table is in
 * detail/ComponentImpl.h.
 */

namespace neuiplusplus
{

/**
 * @brief The parenting argument every component takes first.
 *
 * A distinct type rather than a bare reference so it cannot be confused with a
 * component's own first parameter. You never build one - `add<T>` does.
 */
struct Parent
{
    ComponentCore &of;
};

/**
 * @brief Everything true of any component, whatever it does.
 *
 * Identity, geometry, visibility, the child tree, and accessibility. Operations
 * that depend on a capability are protected here and surfaced by that capability
 * - see interfaces/Interfaces.h and detail/CoreAccess.h.
 *
 * OWNERSHIP. C++ owns the tree; neui mirrors it. A component creates its neui
 * widget on construction and destroys it on destruction, and children are
 * `unique_ptr` members held here. `~ComponentCore` clears the child list FIRST
 * and only then destroys its own widget, so neui's
 * destroy-parent-cascades-to-children path never fires and nothing
 * double-destroys. The order matters and is not automatic: `children_` is a
 * member, so it would otherwise be destroyed after this destructor body - that
 * is, after the parent widget was already gone.
 *
 * Components are NON-MOVABLE. The dispatch table stores `this`; relocating a
 * live component would dangle it. The same constraint `juce::Component` has.
 */
class ComponentCore
{
  public:
    explicit ComponentCore(Parent p);
    virtual ~ComponentCore();

    ComponentCore(const ComponentCore &) = delete;
    ComponentCore &operator=(const ComponentCore &) = delete;
    ComponentCore(ComponentCore &&) = delete;
    ComponentCore &operator=(ComponentCore &&) = delete;

    /// @name Identity
    /// @{
    neui_widget_t widget() const { return widget_; }
    Session &session() const { return *session_; }
    /** @return null for a Frame, which has no parent component. */
    ComponentCore *parent() const { return parent_; }
    /// @}

    /// @name Geometry, in design units
    /// @{
    /** @brief Position and size within the parent. */
    Rect bounds() const { return bounds_; }
    /** @brief The same size at the origin - what paint and hit-tests work in. */
    Rect localBounds() const { return bounds_.atOrigin(); }
    /**
     * @brief Move and resize.
     * @note Applies the frame zoom and snaps EDGES - not position and size
     *       independently - so neighbours share a pixel boundary at fractional
     *       zoom instead of gapping or overlapping.
     */
    void setBounds(Rect r);
    /// @}

    /// @name State
    /// @{
    void setVisible(bool);
    bool isVisible() const { return visible_; }
    void setEnabled(bool);
    bool isEnabled() const { return enabled_; }
    /// @}

    /// @name Accessibility
    /// Universal, because even a pure decoration has something to declare
    /// (@ref Role::none). Declaring an ACTIONABLE role obliges the component to
    /// handle that role's keys - see A11y.h before using these.
    /// @{
    void setAccessibleRole(Role);
    void setAccessibleName(const char *);
    void setAccessibleDescription(const char *);
    void setAccessibleValueRange(float min, float max, float step = 0.0f);
    void setAccessibleValue(float normalized);
    void setAccessibleValueText(const char *);
    /** @brief Tell the AT something changed. A hand-painted control's value
     *         lives in client state, so nothing else can. */
    void notifyAccessible(A11yChange);
    /// @}

    /// @name Children
    /// @{
    /**
     * @brief Construct a child of type @p T, owned by this component.
     *
     * The ONLY way to build a component - never a bare `new`. That is not
     * ceremony: inside `add<T>` the concrete type is statically known, which is
     * what lets the capability set be resolved without RTTI. @p args are
     * forwarded after the implicit Parent.
     *
     * @return a reference to the child, which lives as long as this component.
     */
    template <class T, class... Args> T &add(Args &&...args)
    {
        auto owned = std::make_unique<T>(Parent{*this}, std::forward<Args>(args)...);
        T &ref = *owned;
        // T is complete and fully constructed here - the only point at which the
        // capability set can be resolved statically.
        registerChild(ref, detail::bindingsFor<T>(ref));
        children_.push_back(std::move(owned));
        return ref;
    }

    const std::vector<std::unique_ptr<ComponentCore>> &children() const { return children_; }
    /// @}

  protected:
    /** @brief Tag for the root constructor. @see Frame. */
    struct RootTag
    {
    };
    /**
     * @brief Root construction - a Frame owns its own widget rather than being
     *        a child. Records the bounds without pushing them back through
     *        `set_pos`: the frame was already created at that rect, and its
     *        position is screen-relative.
     */
    ComponentCore(Session &s, neui_widget_t w, Rect bounds, RootTag);

    /// @name Capability-gated operations
    /// Reachable only through detail::CoreAccess, which every interface routes
    /// through; see detail/CoreAccess.h for why one friend serves them all.
    /// @{
    void repaintImpl() const;
    void setCursorImpl(Cursor);
    bool beginRelativeDragImpl();
    void endRelativeDragImpl();
    void takeFocusImpl();
    /// @}

    friend struct detail::CoreAccess;

  private:
    void registerChild(ComponentCore &child, const detail::Bindings &b);
    void applyBounds();

    Session *session_{nullptr};
    ComponentCore *parent_{nullptr};
    neui_widget_t widget_{widget_none};
    Rect bounds_{};
    bool visible_{true};
    bool enabled_{true};
    std::vector<std::unique_ptr<ComponentCore>> children_;
};

/**
 * @brief The base every component inherits.
 * @tparam Derived the component's own type - CRTP, so the interfaces can reach
 *         back into it.
 * @tparam Caps the capability interface templates it wants.
 */
template <class Derived, template <class> class... Caps>
class Component : public ComponentCore, public Caps<Derived>...
{
  public:
    explicit Component(Parent p) : ComponentCore(p) {}

  protected:
    Component(Session &s, neui_widget_t w, Rect b, ComponentCore::RootTag t)
        : ComponentCore(s, w, b, t)
    {
    }
};

/**
 * @brief A top-level window (APPWINDOW / PLUGWINDOW / DIALOG).
 *
 * Not created through `add<T>` - it has no parent component - so it is the one
 * place a component is constructed directly. It names no capabilities: the frame
 * paints nothing and takes no input, its children do.
 */
class Frame : public Component<Frame>
{
  public:
    /** @param widgetType one of the `NEUI_W_*` top-level types.
     *  @param bounds screen position and CLIENT size, in design units. */
    Frame(Session &s, const char *widgetType, Rect bounds, const char *title);

    void show();
    /**
     * @brief The usable content area in design units, excluding any host
     *        menubar band. Lay out against this, not against the create size.
     */
    Rect clientBounds() const;
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_COMPONENT_H
