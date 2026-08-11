/*
 * neuiplusplus - a C++20 skin over the neui C API
 *
 * ComponentCore owns one NEUI_W_CUSTOMDRAW widget and the children below it.
 * Component<Derived, Caps...> is what you actually inherit: it adds the
 * capabilities you name, each parameterised on your own type.
 *
 *   struct Knob : npp::Component<Knob, npp::Paints, npp::MouseEvents> { ... };
 *
 * WHY VARIADIC. A component's API surface then matches what it does: no
 * repaint() on something that never paints, no cursor field on something that
 * takes no input. Capability state lives in the capability, so that is literal
 * rather than a convention.
 *
 * OWNERSHIP. C++ owns the tree; neui mirrors it. A component creates its neui
 * widget on construction and destroys it on destruction, and children are
 * unique_ptr members held here in the core. ~ComponentCore clears the child
 * list FIRST and only then destroys its own widget, so neui's
 * destroy-parent-cascades-to-children path never fires and nothing
 * double-destroys. (The order matters and is not automatic: children_ is a
 * member, so it would otherwise be destroyed after this destructor body, i.e.
 * after the parent widget was already gone.)
 *
 * Components are NON-MOVABLE. The dispatch table stores `this`; relocating a
 * live component would dangle it. Same constraint juce::Component has.
 *
 * CONSTRUCTION. Everything is built through add<T>(...), never a bare `new`.
 * That is not ceremony - it is what makes the design work without RTTI. Inside
 * add<T> the concrete type is statically known, so the capability set is
 * resolved with is_base_of_v and bound as plain function pointers. A
 * dynamic_cast in the core constructor could not see them: the derived object
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
#include "capabilities.h"
#include "cursor.h"
#include "events.h"
#include "filedialog.h"
#include "geometry.h"

namespace neuiplusplus
{

class ComponentCore;
class Session;

// Passed as the first constructor argument of every component. A distinct type
// rather than a bare reference so the parenting argument cannot be confused
// with a component's own first parameter.
struct Parent
{
    ComponentCore &of;
};

namespace detail
{

// One component's resolved capability set, as plain function pointers bound in
// add<T> where the concrete type is known. Function pointers rather than
// interface pointers because the capabilities are templates - Paints<Knob> and
// Paints<Slider> share no base to store.
struct Bindings
{
    ComponentCore *self{nullptr};
    void *obj{nullptr};

    void (*paint)(void *, Canvas &){nullptr};
    void (*resized)(void *){nullptr};
    void (*mouse)(void *, const MouseEvent &, MouseKind){nullptr};
    void (*wheel)(void *, const WheelEvent &){nullptr};
    bool (*key)(void *, const KeyEvent &, KeyKind){nullptr};
    void (*focus)(void *, bool){nullptr};

    // Drives neui's emit_events. A component that handles no input is never
    // hit-tested.
    bool wantsInput() const { return mouse != nullptr || key != nullptr || focus != nullptr; }
};

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
        t.keyUp(e);
        return true;
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

template <class T> Bindings bindingsFor(T &t)
{
    static_assert(std::is_base_of_v<ComponentCore, T>,
                  "components must derive from Component<Self, ...>");
    static_assert(std::is_base_of_v<Paints<T>, T> || std::is_base_of_v<Resizes<T>, T> ||
                      std::is_base_of_v<MouseEvents<T>, T> ||
                      std::is_base_of_v<KeyboardEvents<T>, T> ||
                      std::is_base_of_v<FocusEvents<T>, T>,
                  "a component that names no capability would never be called - "
                  "did you mean Component<Self, Paints>?");

    Bindings b;
    b.self = static_cast<ComponentCore *>(&t);
    b.obj = &t;
    if constexpr (std::is_base_of_v<Paints<T>, T>)
        b.paint = &paintThunk<T>;
    if constexpr (std::is_base_of_v<Resizes<T>, T>)
        b.resized = &resizedThunk<T>;
    if constexpr (std::is_base_of_v<MouseEvents<T>, T>)
    {
        b.mouse = &mouseThunk<T>;
        b.wheel = &wheelThunk<T>;
    }
    if constexpr (std::is_base_of_v<KeyboardEvents<T>, T>)
        b.key = &keyThunk<T>;
    if constexpr (std::is_base_of_v<FocusEvents<T>, T>)
        b.focus = &focusThunk<T>;
    return b;
}

} // namespace detail

/*
 * Everything true of any component, whatever it does: identity, geometry,
 * visibility, the child tree, and accessibility. Operations that depend on a
 * capability are protected here and surfaced by that capability - see
 * capabilities.h.
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

    // ---- identity ----------------------------------------------------------

    neui_widget_t widget() const { return widget_; }
    Session &session() const { return *session_; }
    ComponentCore *parent() const { return parent_; }

    // ---- geometry, in design units -----------------------------------------
    // setBounds applies the frame zoom and snaps EDGES (not position and size
    // independently) so neighbours share a pixel boundary at fractional zoom.

    Rect bounds() const { return bounds_; }
    Rect localBounds() const { return bounds_.atOrigin(); }
    void setBounds(Rect r);

    // ---- state -------------------------------------------------------------

    void setVisible(bool);
    bool isVisible() const { return visible_; }
    void setEnabled(bool);
    bool isEnabled() const { return enabled_; }

    // ---- accessibility -----------------------------------------------------
    // Universal: even a pure decoration has something to declare (Role::none).
    // Declaring an ACTIONABLE role (slider, button...) obliges the component to
    // handle that role's keys - see a11y.h.

    void setAccessibleRole(Role);
    void setAccessibleName(const char *);
    void setAccessibleDescription(const char *);
    void setAccessibleValueRange(float min, float max, float step = 0.0f);
    void setAccessibleValue(float normalized);
    void setAccessibleValueText(const char *);
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

    const std::vector<std::unique_ptr<ComponentCore>> &children() const { return children_; }

  protected:
    // Root construction (a frame owns its own widget rather than being a
    // child). Records the bounds without pushing them back through set_pos -
    // the frame was already created at that rect, and its position is
    // screen-relative.
    struct RootTag
    {
    };
    ComponentCore(Session &s, neui_widget_t w, Rect bounds, RootTag);

    // Capability-gated operations. Reachable only through detail::CoreAccess,
    // which every capability routes through; see capabilities.h for why one
    // friend serves them all.
    void repaintImpl() const;
    void setCursorImpl(Cursor);
    bool beginRelativeDragImpl();
    void endRelativeDragImpl();
    void takeFocusImpl();

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

/*
 * What a component inherits. `Derived` is its own type (CRTP, so capabilities
 * can reach back into it); `Caps` are the capability templates it wants.
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

/*
 * Session owns the neui handles, the id -> component table, and the zoom.
 *
 * The dispatch table is a flat vector indexed by the LOW 16 BITS of the widget
 * id: neui packs the owning session in the high half and a dense tree slot in
 * the low half, so lookup is an array index rather than a hash. (Slots are
 * reused after destroy, so entries are cleared on the way out - neui does not
 * yet detect stale ids across a reuse.)
 */
class Session
{
  public:
    // For a plugin the host should be "neui.host.crossplatform" - it is the
    // only one implementing NEUI_API_EMBED, and the only one that resolves
    // cursors, timers, relative pointer and accessibility.
    static std::unique_ptr<Session> create(neui_api_t *host);
    ~Session();

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    neui_session_t raw() const { return sess_; }
    neui_widget_api_t *widgets() const { return widgets_; }
    neui_attr_api_t *attrs() const { return attrs_; }
    neui_pointer_api_t *pointer() const { return pointer_; }
    neui_notify_api_t *notify() const { return notify_; }
    neui_a11y_api_t *a11y() const { return a11y_; }

    // The user zoom. Design units are multiplied by this on the way out to
    // neui and divided on the way in.
    float zoom() const { return zoom_; }
    void setZoom(float z);

    // Modal file dialogs. `ownerFrame` must be a Frame - neui anchors the
    // dialog to a top-level window. False `supported` means this host has no
    // file-dialog surface, the only case where offering your own path entry
    // makes sense.
    FileDialogResult openFile(ComponentCore &ownerFrame, const FileDialogOptions &);
    FileDialogResult saveFile(ComponentCore &ownerFrame, const FileDialogOptions &);
    bool hasFileDialog() const;

  private:
    friend class ComponentCore;
    Session() = default;

    void bind(neui_widget_t w, const detail::Bindings &b);
    void unbind(neui_widget_t w);
    detail::Bindings *lookup(neui_widget_t w);

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
    // "am I dragging" is tracked here from DOWN/UP rather than read off the
    // wire.
    neui_widget_t pressed_{widget_none};
    Point downPos_{};
};

/*
 * A top-level frame (APPWINDOW / PLUGWINDOW / DIALOG). Not created through
 * add<T> - it has no parent component - so it is the one place a component is
 * constructed directly. It names no capabilities: the frame paints nothing and
 * takes no input, its children do.
 */
class Frame : public Component<Frame>
{
  public:
    Frame(Session &s, const char *widgetType, Rect bounds, const char *title);

    void show();
    // The usable content area in design units, excluding any host menubar band.
    Rect clientBounds() const;
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_COMPONENT_H
