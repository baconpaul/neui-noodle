/*
 * neuiplusplus - a C++20 skin over the neui C API
 *
 * Component owns one NEUI_W_CUSTOMDRAW widget and the children below it.
 *
 * OWNERSHIP. C++ owns the tree; neui mirrors it. A component creates its neui
 * widget on construction and destroys it on destruction, and children are
 * unique_ptr members held here in the base. ~Component clears the child list
 * FIRST and only then destroys its own widget, so a child's widget always
 * outlives none of its parent's - neui's destroy-parent-cascades-to-children
 * path never fires, and nothing double-destroys. (The order matters and is not
 * automatic: children_ is a base member, so it would otherwise be destroyed
 * after this destructor body, i.e. after the parent widget was already gone.)
 *
 * Components are NON-MOVABLE. The dispatch table stores `this`; relocating a
 * live component would dangle it. Same constraint juce::Component has.
 *
 * CONSTRUCTION. Everything is built through Component::add<T>(...), never with
 * a bare `new`. That is not ceremony - it is what makes the design work without
 * RTTI. Inside add<T> the concrete type is statically known, so the capability
 * interfaces (Paintable, MouseHandling, ...) are resolved with
 * is_base_of_v + static_cast at compile time and cached as plain pointers.
 * A dynamic_cast in the base constructor could not see them: the derived object
 * does not exist yet.
 */

#ifndef NEUIPLUSPLUS_COMPONENT_H
#define NEUIPLUSPLUS_COMPONENT_H

#include <memory>
#include <type_traits>
#include <vector>

#include <neui/neui.h>

#include "a11y.h"
#include "canvas.h"
#include "cursor.h"
#include "events.h"
#include "filedialog.h"
#include "geometry.h"
#include "interfaces.h"

namespace neuiplusplus
{

class Component;
class Session;

namespace detail
{

// The resolved capability set for one component. Every pointer is either null
// or an interface subobject of the same object as `self` - computed once, at
// add<T> time, never re-derived.
struct Bindings
{
    Component *self{nullptr};
    Paintable *paintable{nullptr};
    Resizable *resizable{nullptr};
    MouseHandling *mouse{nullptr};
    KeyboardHandling *keyboard{nullptr};
    FocusHandling *focus{nullptr};

    // Drives neui's emit_events. A component that handles no input is not
    // hit-tested at all - which is also the natural "decorative" signal for the
    // accessibility walk when neui grows one.
    bool wantsInput() const { return mouse != nullptr || keyboard != nullptr || focus != nullptr; }
};

template <class T> Bindings bindingsFor(T &t)
{
    static_assert(std::is_base_of_v<Component, T>, "components must derive from Component");
    static_assert(std::is_base_of_v<Paintable, T> || std::is_base_of_v<Resizable, T> ||
                      std::is_base_of_v<MouseHandling, T> || std::is_base_of_v<KeyboardHandling, T>,
                  "a component that implements no capability interface would never be called - "
                  "did you mean to inherit Paintable?");

    Bindings b;
    b.self = static_cast<Component *>(&t);
    if constexpr (std::is_base_of_v<Paintable, T>)
        b.paintable = static_cast<Paintable *>(&t);
    if constexpr (std::is_base_of_v<Resizable, T>)
        b.resizable = static_cast<Resizable *>(&t);
    if constexpr (std::is_base_of_v<MouseHandling, T>)
        b.mouse = static_cast<MouseHandling *>(&t);
    if constexpr (std::is_base_of_v<KeyboardHandling, T>)
        b.keyboard = static_cast<KeyboardHandling *>(&t);
    if constexpr (std::is_base_of_v<FocusHandling, T>)
        b.focus = static_cast<FocusHandling *>(&t);
    return b;
}

} // namespace detail

// Passed as the first constructor argument of every component. A distinct type
// rather than a bare Component & so the parenting argument can't be confused
// with a component's own first parameter.
struct Parent
{
    Component &of;
};

class Component
{
  public:
    explicit Component(Parent p);
    virtual ~Component();

    Component(const Component &) = delete;
    Component &operator=(const Component &) = delete;
    Component(Component &&) = delete;
    Component &operator=(Component &&) = delete;

    // ---- identity ----------------------------------------------------------

    neui_widget_t widget() const { return widget_; }
    Session &session() const { return *session_; }
    Component *parent() const { return parent_; }

    // ---- geometry, in design units -----------------------------------------
    // setBounds applies the frame zoom and snaps EDGES (not position and size
    // independently) so neighbours share a pixel boundary at fractional zoom.

    Rect bounds() const { return bounds_; }
    Rect localBounds() const { return bounds_.atOrigin(); }
    void setBounds(Rect r);

    // ---- state -------------------------------------------------------------

    void repaint() const;
    void setVisible(bool);
    bool isVisible() const { return visible_; }
    void setEnabled(bool);
    bool isEnabled() const { return enabled_; }
    void takeFocus();

    // Pointer shape while hovering this component. No-op on a component that
    // handles no input (it does not hit-test), and on the native hosts, which
    // do not read the attribute at all - see cursor.h.
    void setCursor(Cursor);
    Cursor cursor() const { return cursor_; }

    // Relative (unbounded) pointer for a value drag: the visible cursor pins
    // and hides, motion keeps arriving past the screen edge, and on end the
    // cursor is restored to where the press happened.
    //
    // CALL beginRelativeDrag FROM mouseDown, not from anywhere else - neui
    // seeds the virtual position from the last dispatched mouse event, which
    // inside a down handler is that very press. Returns false when the host has
    // no pointer API (native hosts, iOS, null platform), so a caller degrades
    // to an ordinary bounded drag. endRelativeDrag is safe unconditionally.
    //
    // Only for DELTA-driven drags. An absolute control that maps position to
    // value wants the pointer bounded, and would just pin at its limits.
    bool beginRelativeDrag();
    void endRelativeDrag();

    // ---- accessibility -----------------------------------------------------
    // Declaring an ACTIONABLE role (slider, button, checkbox...) obliges the
    // component to handle that role's keys - see a11y.h. Components that
    // declare nothing and handle no input are hidden from the AT rather than
    // announced as anonymous groups.

    void setAccessibleRole(Role);
    void setAccessibleName(const char *);
    void setAccessibleDescription(const char *);
    // Real-world range behind the normalised value, so an AT can say
    // "-6.0 dB" rather than "0.62". step 0 = continuous.
    void setAccessibleValueRange(float min, float max, float step = 0.0f);
    void setAccessibleValue(float normalized);
    void setAccessibleValueText(const char *);
    // Client-owned state changed. neui raises this itself for its own widgets,
    // but a hand-painted control's value lives in client state, so it must.
    void notifyAccessible(A11yChange);

    // ---- children ----------------------------------------------------------

    template <class T, class... Args> T &add(Args &&...args)
    {
        auto owned = std::make_unique<T>(Parent{*this}, std::forward<Args>(args)...);
        T &ref = *owned;
        // T is complete and fully constructed here - the only point at which
        // the capability set can be resolved statically.
        registerChild(ref, detail::bindingsFor<T>(ref));
        children_.push_back(std::move(owned));
        return ref;
    }

    const std::vector<std::unique_ptr<Component>> &children() const { return children_; }

  protected:
    // Root construction (a frame owns its own widget rather than being a child).
    // Records the bounds without pushing them back through set_pos - the frame
    // was already created at that rect, and its position is screen-relative.
    struct RootTag
    {
    };
    Component(Session &s, neui_widget_t w, Rect bounds, RootTag);

  private:
    void registerChild(Component &child, const detail::Bindings &b);
    void applyBounds();

    Session *session_{nullptr};
    Component *parent_{nullptr};
    neui_widget_t widget_{widget_none};
    Rect bounds_{};
    bool visible_{true};
    bool enabled_{true};
    Cursor cursor_{Cursor::inherit};
    bool roleDeclared_{false};
    std::vector<std::unique_ptr<Component>> children_;
};

/*
 * Session owns the neui handles, the id -> component table, and the zoom.
 *
 * The dispatch table is a flat vector indexed by the LOW 16 BITS of the widget
 * id: neui packs the owning session in the high half and a dense tree slot in
 * the low half, so lookup is an array index rather than a hash. (Slots are
 * reused after destroy, so entries must be cleared on the way out - neui does
 * not yet detect stale ids across a reuse.)
 */
class Session
{
  public:
    // Creates the neui session against an already-selected host. For a plugin
    // that host should be "neui.host.crossplatform" - it is the only one
    // implementing NEUI_API_EMBED, and it renders identically everywhere.
    static std::unique_ptr<Session> create(neui_api_t *host);
    ~Session();

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    neui_session_t raw() const { return sess_; }
    neui_widget_api_t *widgets() const { return widgets_; }
    neui_attr_api_t *attrs() const { return attrs_; }
    // Null unless the host exposes NEUI_API_POINTER (crossplatform host only).
    neui_pointer_api_t *pointer() const { return pointer_; }

    // Modal file dialogs. `ownerFrame` must be a Frame - neui anchors the
    // dialog to a top-level window. Blocks until the user confirms or cancels.
    // False `supported` means this host has no file-dialog surface at all,
    // which is the only case where offering your own path entry makes sense.
    FileDialogResult openFile(Component &ownerFrame, const FileDialogOptions &);
    FileDialogResult saveFile(Component &ownerFrame, const FileDialogOptions &);
    bool hasFileDialog() const;
    // Toasts + message boxes; null if the host has no notification surface.
    neui_notify_api_t *notify() const { return notify_; }
    // Null unless the host exposes NEUI_API_A11Y.
    neui_a11y_api_t *a11y() const { return a11y_; }

    // The user zoom. Design units are multiplied by this on the way out to
    // neui and divided on the way in. When neui's own NEUI_ATTR_UI_SCALE
    // lands, this collapses to a single set_float and the multiplications
    // in Component::applyBounds go away.
    float zoom() const { return zoom_; }
    void setZoom(float z);

  private:
    friend class Component;
    Session() = default;

    void bind(neui_widget_t w, const detail::Bindings &b);
    void unbind(neui_widget_t w);
    detail::Bindings *lookup(neui_widget_t w);

    // The single entry point neui calls; fans out through lookup().
    static bool NEUI_ABI dispatch(void *token, neui_event_t *event);
    static void *NEUI_ABI clientInterface(void *token, const char *iface);

    neui_api_t *host_{nullptr};
    neui_session_t sess_{};
    neui_widget_api_t *widgets_{nullptr};
    neui_attr_api_t *attrs_{nullptr};
    neui_pointer_api_t *pointer_{nullptr};
    neui_notify_api_t *notify_{nullptr};
    neui_a11y_api_t *a11y_{nullptr};
    float zoom_{1.0f};
    std::vector<detail::Bindings> table_;

    neui_client_t client_{};
    neui_widget_client_t widgetClient_{};

    // Drag latching. Hosts have disagreed about buttonmap on MOUSE_MOVE, so
    // "am I dragging" is tracked here from DOWN/UP rather than read off the wire.
    neui_widget_t pressed_{widget_none};
    Point downPos_{};
};

/*
 * A top-level frame (APPWINDOW / PLUGWINDOW / DIALOG). Not created through
 * add<T> - it has no parent component - so it is the one place a component is
 * constructed directly.
 */
class Frame : public Component
{
  public:
    Frame(Session &s, const char *widgetType, Rect bounds, const char *title);

    void show();
    // The usable content area in design units, excluding any host menubar band.
    Rect clientBounds() const;
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_COMPONENT_H
