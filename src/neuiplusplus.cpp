/*
 * neuiplusplus - implementation.
 *
 * The whole runtime is: a widget-id -> Bindings table, one trampoline that
 * fans neui's flat event union out to the capability interfaces, and the
 * design-unit <-> device-pixel conversion at the boundary.
 */

#include <neuiplusplus/component.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace neuiplusplus
{

namespace
{
// neui packs the owning session in the high 16 bits and a dense tree slot in
// the low 16, so the slot is a direct index into the binding table.
inline std::size_t slotOf(neui_widget_t w) { return std::size_t(w.id & 0xFFFFu); }

inline Modifiers modsFrom(std::uint32_t buttonmap) { return Modifiers{buttonmap}; }
} // namespace

// ---------------------------------------------------------------------------
// Session

std::unique_ptr<Session> Session::create(neui_api_t *host)
{
    if (!host)
        return nullptr;

    std::unique_ptr<Session> s{new Session()};
    s->widgetClient_.neui_version = NEUI_VERSION;
    s->widgetClient_.ondestroy = nullptr;
    s->widgetClient_.onevent = &Session::dispatch;
    s->client_.neui_version = NEUI_VERSION;
    s->client_.get_interface = &Session::clientInterface;

    s->sess_ = host->create_session(&s->client_, s.get());
    if (!s->sess_.session)
        return nullptr;

    s->widgets_ =
        static_cast<neui_widget_api_t *>(host->get_interface(s->sess_, NEUI_API_WIDGETS));
    if (!s->widgets_)
        return nullptr;

    // Both optional, and both crossplatform-host-only in practice. Feature
    // detect rather than assume: on Windows and macOS neui_get_api(NULL)
    // returns the native host, which exposes neither.
    s->attrs_ = static_cast<neui_attr_api_t *>(host->get_interface(s->sess_, NEUI_API_ATTRS));
    s->pointer_ =
        static_cast<neui_pointer_api_t *>(host->get_interface(s->sess_, NEUI_API_POINTER));
    s->notify_ = static_cast<neui_notify_api_t *>(host->get_interface(s->sess_, NEUI_API_NOTIFY));
    s->a11y_ = static_cast<neui_a11y_api_t *>(host->get_interface(s->sess_, NEUI_API_A11Y));

    s->host_ = host;
    return s;
}

Session::~Session()
{
    if (sess_.session && host_)
        host_->destroy(sess_);
}

void *NEUI_ABI Session::clientInterface(void *token, const char *iface)
{
    auto *s = static_cast<Session *>(token);
    if (s && std::strcmp(iface, NEUI_API_WIDGETS) == 0)
        return &s->widgetClient_;
    return nullptr;
}

void Session::bind(neui_widget_t w, const detail::Bindings &b)
{
    const std::size_t slot = slotOf(w);
    if (table_.size() <= slot)
        table_.resize(slot + 1);
    table_[slot] = b;
}

void Session::unbind(neui_widget_t w)
{
    const std::size_t slot = slotOf(w);
    if (slot < table_.size())
        table_[slot] = detail::Bindings{};
    if (pressed_.id == w.id)
        pressed_ = widget_none;
}

detail::Bindings *Session::lookup(neui_widget_t w)
{
    const std::size_t slot = slotOf(w);
    if (slot >= table_.size())
        return nullptr;
    auto &b = table_[slot];
    // Guard against a stale id after slot reuse: the binding must name the
    // same widget we were handed.
    return (b.self && b.self->widget().id == w.id) ? &b : nullptr;
}

void Session::setZoom(float z)
{
    zoom_ = std::max(0.1f, z);
    for (auto &b : table_)
        if (b.self)
            b.self->setBounds(b.self->bounds()); // re-applies with the new scale
}

namespace
{
// The C API hands each path to a callback and owns the buffer only for the
// duration of that call, so copy on the way through.
void NEUI_ABI collectPath(void *userdata, const char *path)
{
    if (path)
        static_cast<std::vector<std::string> *>(userdata)->emplace_back(path);
}

// Builds the C description. The neui_file_filter_t array points into `opts`,
// so the returned vector must not outlive the call that uses it.
std::vector<neui_file_filter_t> buildFilters(const FileDialogOptions &opts)
{
    std::vector<neui_file_filter_t> out;
    out.reserve(opts.filters.size());
    for (const auto &f : opts.filters)
        out.push_back({f.label.c_str(), f.patterns.c_str()});
    return out;
}

neui_file_dialog_t buildDesc(const FileDialogOptions &opts,
                             const std::vector<neui_file_filter_t> &filters, bool forSave)
{
    neui_file_dialog_t d{};
    d.title = opts.title.empty() ? nullptr : opts.title.c_str();
    d.initial_dir = opts.initialDir.empty() ? nullptr : opts.initialDir.c_str();
    d.initial_name = opts.initialName.empty() ? nullptr : opts.initialName.c_str();
    d.filters = filters.empty() ? nullptr : filters.data();
    d.filter_count = std::uint32_t(filters.size());
    d.default_filter = std::uint32_t(opts.defaultFilter);
    d.flags = 0;
    if (!forSave && opts.multiSelect)
        d.flags |= NEUI_FD_MULTISELECT;
    if (opts.showHidden)
        d.flags |= NEUI_FD_SHOW_HIDDEN;
    if (forSave && !opts.confirmOverwrite)
        d.flags |= NEUI_FD_NO_OVERWRITE_PROMPT;
    return d;
}
} // namespace

bool Session::hasFileDialog() const { return notify_ != nullptr && notify_->open_file != nullptr; }

FileDialogResult Session::openFile(Component &ownerFrame, const FileDialogOptions &opts)
{
    FileDialogResult r;
    if (!hasFileDialog())
    {
        r.supported = false;
        return r;
    }
    const auto filters = buildFilters(opts);
    const auto desc = buildDesc(opts, filters, false);
    const int n = notify_->open_file(sess_, ownerFrame.widget(), &desc, &collectPath, &r.paths);
    r.supported = (n >= 0); // -1 means the dialog could not be shown at all
    return r;
}

FileDialogResult Session::saveFile(Component &ownerFrame, const FileDialogOptions &opts)
{
    FileDialogResult r;
    if (notify_ == nullptr || notify_->save_file == nullptr)
    {
        r.supported = false;
        return r;
    }
    const auto filters = buildFilters(opts);
    const auto desc = buildDesc(opts, filters, true);
    const int n = notify_->save_file(sess_, ownerFrame.widget(), &desc, &collectPath, &r.paths);
    r.supported = (n >= 0);
    return r;
}

bool NEUI_ABI Session::dispatch(void *token, neui_event_t *event)
{
    auto *s = static_cast<Session *>(token);
    if (!s || !event)
        return false;

    if (event->type == NEUI_EVENT_APP_QUIT)
        return true;

    const float z = s->zoom_;

    if (event->type == NEUI_EVENT_WIDGET_PAINT)
    {
        auto &pe = event->data.paint;
        auto *b = s->lookup(pe.widget);
        if (!b || !b->paintable)
            return false;
        // Scale once here; every component then paints in design units. The
        // framework's own push_transform/pop_transform bracket cleans this up.
        pe.painter_api->scale(pe.p, z, z);
        Canvas g{pe.painter_api, pe.p, Rect::fromSize(pe.width / z, pe.height / z), pe.focused};
        b->paintable->paint(g);
        return true;
    }

    if (event->type == NEUI_EVENT_RESIZE)
    {
        auto *b = s->lookup(event->data.resize.widget);
        if (!b || !b->resizable)
            return false;
        b->resizable->resized();
        return true;
    }

    if (event->type == NEUI_EVENT_WIDGET_FOCUS)
    {
        auto *b = s->lookup(event->data.focus.widget);
        if (!b || !b->focus)
            return false;
        if (event->data.focus.focused)
            b->focus->focusGained();
        else
            b->focus->focusLost();
        return true;
    }

    if (event->type == NEUI_EVENT_MOUSE_WHEEL)
    {
        auto &we = event->data.wheel;
        auto *b = s->lookup(we.widget);
        if (!b || !b->mouse)
            return false;
        WheelEvent w;
        w.position = {we.x / z, we.y / z};
        w.delta = float(we.delta);
        w.isHorizontal = we.is_horizontal != 0;
        w.mods = modsFrom(we.buttonmap);
        b->mouse->mouseWheel(w);
        return true;
    }

    // ---- mouse -------------------------------------------------------------
    switch (event->type)
    {
    case NEUI_EVENT_MOUSE_MOVE:
    case NEUI_EVENT_MOUSE_ENTER:
    case NEUI_EVENT_MOUSE_LEAVE:
    case NEUI_EVENT_MOUSE_BUTTON_DOWN:
    case NEUI_EVENT_MOUSE_BUTTON_UP:
    case NEUI_EVENT_MOUSE_BUTTON_DBLCLICK:
    case NEUI_EVENT_MOUSE_RBUTTON_DOWN:
    case NEUI_EVENT_MOUSE_RBUTTON_UP:
        break;
    default:
        return false;
    }

    auto &me = event->data.mouse;
    auto *b = s->lookup(me.widget);
    if (!b || !b->mouse)
        return false;

    MouseEvent e;
    e.position = {me.x / z, me.y / z};
    e.mods = modsFrom(me.buttonmap);
    e.isDragging = (s->pressed_.id == me.widget.id);
    e.downPosition = e.isDragging ? s->downPos_ : e.position;

    switch (event->type)
    {
    case NEUI_EVENT_MOUSE_ENTER:
        b->mouse->mouseEnter(e);
        break;
    case NEUI_EVENT_MOUSE_LEAVE:
        b->mouse->mouseExit(e);
        break;
    case NEUI_EVENT_MOUSE_BUTTON_DOWN:
        s->pressed_ = me.widget;
        s->downPos_ = e.position;
        e.isDragging = true;
        e.downPosition = e.position;
        e.clickCount = 1;
        b->mouse->mouseDown(e);
        break;
    case NEUI_EVENT_MOUSE_MOVE:
        // A held-button drag arrives as MOVE. The latch decides, not buttonmap.
        if (e.isDragging)
            b->mouse->mouseDrag(e);
        else
            b->mouse->mouseMove(e);
        break;
    case NEUI_EVENT_MOUSE_BUTTON_UP:
        b->mouse->mouseUp(e);
        s->pressed_ = widget_none;
        break;
    case NEUI_EVENT_MOUSE_BUTTON_DBLCLICK:
        e.clickCount = 2;
        b->mouse->mouseDoubleClick(e);
        break;
    case NEUI_EVENT_MOUSE_RBUTTON_DOWN:
        b->mouse->mouseRightButtonDown(e);
        break;
    case NEUI_EVENT_MOUSE_RBUTTON_UP:
        b->mouse->mouseRightButtonUp(e);
        break;
    default:
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Component

Component::Component(Parent p) : session_(&p.of.session()), parent_(&p.of)
{
    widget_ = session_->widgets()->create(session_->raw(), p.of.widget(), NEUI_W_CUSTOMDRAW, 0, 0,
                                          1, 1, nullptr);
}

Component::Component(Session &s, neui_widget_t w, Rect bounds, RootTag)
    : session_(&s), widget_(w), bounds_(bounds)
{
}

Component::~Component()
{
    // Explicit: children_ is a base member, so without this it would outlive
    // the destroy() below and we would be tearing down parent-first.
    children_.clear();

    if (session_ && widget_.id != widget_none.id)
    {
        session_->unbind(widget_);
        session_->widgets()->destroy(session_->raw(), widget_);
    }
}

void Component::registerChild(Component &child, const detail::Bindings &b)
{
    session_->bind(child.widget(), b);
    // A component with no input interface is never hit-tested.
    session_->widgets()->set_emit_events(session_->raw(), child.widget(), b.wantsInput());
    if (b.focus)
        session_->widgets()->set_tab_stop(session_->raw(), child.widget(), true);

    // NO automatic role. An earlier version defaulted non-interactive
    // components to Role::none as a "decorative" convenience, which was wrong:
    // ROLE_NONE removes the node AND ITS WHOLE SUBTREE, so a plain container
    // panel took every control in the editor out of the tree with it. Roles are
    // declared explicitly, per widget, by the widget that knows what it is; a
    // container left alone reports ROLE_GROUP, which is exactly right for it.
    child.applyBounds();
}

void Component::setBounds(Rect r)
{
    bounds_ = r;
    applyBounds();
}

void Component::applyBounds()
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

void Component::repaint() const
{
    if (session_ && widget_.id != widget_none.id)
        session_->widgets()->invalidate(session_->raw(), widget_);
}

void Component::setVisible(bool v)
{
    visible_ = v;
    if (!session_)
        return;
    if (v)
        session_->widgets()->show(session_->raw(), widget_);
    else
        session_->widgets()->hide(session_->raw(), widget_);
}

void Component::setEnabled(bool e)
{
    enabled_ = e;
    if (session_)
        session_->widgets()->set_enabled(session_->raw(), widget_, e);
}

void Component::takeFocus()
{
    if (session_)
        session_->widgets()->set_focus(session_->raw(), widget_);
}

void Component::setCursor(Cursor c)
{
    cursor_ = c;
    if (session_ && session_->attrs())
        session_->attrs()->set_string(session_->raw(), widget_, NEUI_ATTR_CURSOR, cursorName(c));
}

bool Component::beginRelativeDrag()
{
    if (!session_ || !session_->pointer())
        return false;
    return session_->pointer()->begin_relative(session_->raw(), widget_);
}

void Component::endRelativeDrag()
{
    if (session_ && session_->pointer())
        session_->pointer()->end_relative(session_->raw());
}

void Component::setAccessibleRole(Role r)
{
    roleDeclared_ = true;
    if (session_ && session_->a11y())
        session_->a11y()->set_role(session_->raw(), widget_, neui_a11y_role_t(r));
}

void Component::setAccessibleName(const char *utf8)
{
    if (session_ && session_->a11y())
        session_->a11y()->set_name(session_->raw(), widget_, utf8);
}

void Component::setAccessibleDescription(const char *utf8)
{
    if (session_ && session_->a11y())
        session_->a11y()->set_description(session_->raw(), widget_, utf8);
}

void Component::setAccessibleValueRange(float min, float max, float step)
{
    if (session_ && session_->a11y())
        session_->a11y()->set_value_range(session_->raw(), widget_, min, max, step);
}

void Component::setAccessibleValue(float normalized)
{
    if (session_ && session_->a11y())
        session_->a11y()->set_value(session_->raw(), widget_, normalized);
}

void Component::setAccessibleValueText(const char *utf8)
{
    if (session_ && session_->a11y())
        session_->a11y()->set_value_text(session_->raw(), widget_, utf8);
}

void Component::notifyAccessible(A11yChange c)
{
    if (session_ && session_->a11y())
        session_->a11y()->notify(session_->raw(), widget_, neui_a11y_change_t(c));
}

// ---------------------------------------------------------------------------
// Frame

Frame::Frame(Session &s, const char *widgetType, Rect bounds, const char *title)
    : Component(s,
                s.widgets()->create(s.raw(), widget_none, widgetType, int(std::lround(bounds.getX())),
                                    int(std::lround(bounds.getY())),
                                    int(std::lround(bounds.getWidth())),
                                    int(std::lround(bounds.getHeight())), nullptr),
                bounds, Component::RootTag{})
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
