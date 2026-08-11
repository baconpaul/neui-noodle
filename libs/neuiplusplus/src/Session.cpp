/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 *
 * Session: the neui handles, the widget-id -> Bindings table, the one
 * trampoline that fans neui's flat event union out to the capability
 * interfaces, and the design-unit <-> device-pixel conversion at the boundary.
 */

#include <neuiplusplus/Component.h>
#include <neuiplusplus/Session.h>

#include <algorithm>
#include <cstring>

namespace neuiplusplus
{

namespace
{
// neui packs the owning session in the high 16 bits and a dense tree slot in
// the low 16, so the slot is a direct index into the binding table.
inline std::size_t slotOf(neui_widget_t w) { return std::size_t(w.id & 0xFFFFu); }

// A mouse buttonmap is already in Modifiers' bit space (it mirrors NEUI_MK_*).
inline Modifiers modsFrom(std::uint32_t buttonmap) { return Modifiers{buttonmap}; }

// A KEY event is not: neui carries key modifiers in the unrelated NEUI_KMOD_*
// space (SHIFT 0x1 / CTRL 0x2 / ALT 0x4 / META 0x8), which overlaps the button
// bits. Translate rather than reinterpret.
inline Modifiers modsFromKeyMods(std::uint32_t kmod)
{
    std::uint32_t m = 0;
    if (kmod & NEUI_KMOD_SHIFT)
        m |= Modifiers::kShift;
    if (kmod & NEUI_KMOD_CTRL)
        m |= Modifiers::kCtrl;
    if (kmod & NEUI_KMOD_ALT)
        m |= Modifiers::kAlt;
    if (kmod & NEUI_KMOD_META)
        m |= Modifiers::kMeta;
    return Modifiers{m};
}
} // namespace

// ---------------------------------------------------------------------------
// Lifetime

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

    s->widgets_ = static_cast<neui_widget_api_t *>(host->get_interface(s->sess_, NEUI_API_WIDGETS));
    if (!s->widgets_)
        return nullptr;

    // All optional, and all crossplatform-host-only in practice. Feature detect
    // rather than assume: on Windows and macOS neui_get_api(NULL) returns the
    // native host, which exposes none of them.
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

// ---------------------------------------------------------------------------
// Dispatch table

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
    // Guard against a stale id after slot reuse: the binding must name the same
    // widget we were handed.
    return (b.self && b.self->widget().id == w.id) ? &b : nullptr;
}

void Session::setZoom(float z)
{
    zoom_ = std::max(0.1f, z);
    for (auto &b : table_)
        if (b.self)
            b.self->setBounds(b.self->bounds()); // re-applies with the new scale
}

// ---------------------------------------------------------------------------
// File dialogs

namespace
{
// The C API hands each path to a callback and owns the buffer only for the
// duration of that call, so copy on the way through.
void NEUI_ABI collectPath(void *userdata, const char *path)
{
    if (path)
        static_cast<std::vector<std::string> *>(userdata)->emplace_back(path);
}

// Builds the C description. The neui_file_filter_t array points into `opts`, so
// the returned vector must not outlive the call that uses it.
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

FileDialogResult Session::openFile(ComponentCore &ownerFrame, const FileDialogOptions &opts)
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

FileDialogResult Session::saveFile(ComponentCore &ownerFrame, const FileDialogOptions &opts)
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

// ---------------------------------------------------------------------------
// The trampoline
//
// Every neui event for every widget in this session arrives here. It finds the
// binding, converts the payload out of device pixels, and calls the one thunk
// that knows the concrete type.

bool NEUI_ABI Session::dispatch(void *token, neui_event_t *event)
{
    using detail::KeyKind;
    using detail::MouseKind;

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
        if (!b || !b->paint)
            return false;
        // Scale once here; every component then paints in design units. The
        // framework's own push_transform/pop_transform bracket cleans this up.
        pe.painter_api->scale(pe.p, z, z);
        Canvas g{pe.painter_api, pe.p, Rect::fromSize(pe.width / z, pe.height / z), pe.focused};
        b->paint(b->obj, g);
        return true;
    }

    if (event->type == NEUI_EVENT_RESIZE)
    {
        auto *b = s->lookup(event->data.resize.widget);
        if (!b || !b->resized)
            return false;
        b->resized(b->obj);
        return true;
    }

    if (event->type == NEUI_EVENT_WIDGET_FOCUS)
    {
        auto *b = s->lookup(event->data.focus.widget);
        if (!b || !b->focus)
            return false;
        b->focus(b->obj, event->data.focus.focused);
        return true;
    }

    if (event->type == NEUI_EVENT_MOUSE_WHEEL)
    {
        // data.wheel, never data.mouse: the payloads overlap in the union and
        // mouse.buttonmap sits at the same offset as wheel.delta.
        auto &we = event->data.wheel;
        auto *b = s->lookup(we.widget);
        if (!b || !b->wheel)
            return false;
        WheelEvent w;
        w.position = {we.x / z, we.y / z};
        w.delta = float(we.delta);
        w.isHorizontal = we.is_horizontal != 0;
        w.isFlipped = we.is_flipped != 0;
        w.mods = modsFrom(we.buttonmap);
        b->wheel(b->obj, w);
        return true;
    }

    // ---- keys --------------------------------------------------------------
    // The client gets first refusal on every key routed to the focused widget;
    // returning false here hands it back to neui's own handling, which is what
    // KeyboardEvents::keyPressed's bool return means.
    switch (event->type)
    {
    case NEUI_EVENT_KEYDOWN:
    case NEUI_EVENT_KEYCHAR:
    case NEUI_EVENT_KEYUP:
    {
        auto &ke = event->data.key;
        auto *kb = s->lookup(ke.widget);
        if (!kb || !kb->key)
            return false;
        KeyEvent k;
        k.mods = modsFromKeyMods(ke.modifiers);
        if (event->type == NEUI_EVENT_KEYCHAR)
        {
            // On KEYCHAR the keycode field carries a Unicode CODEPOINT, not a
            // NEUI_KEY_*; the two never share an event.
            k.character = ke.keycode;
            return kb->key(kb->obj, k, KeyKind::typed);
        }
        k.keyCode = ke.keycode;
        return kb->key(kb->obj, k,
                       event->type == NEUI_EVENT_KEYDOWN ? KeyKind::pressed : KeyKind::released);
    }
    default:
        break;
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
        b->mouse(b->obj, e, MouseKind::enter);
        break;
    case NEUI_EVENT_MOUSE_LEAVE:
        b->mouse(b->obj, e, MouseKind::exit);
        break;
    case NEUI_EVENT_MOUSE_BUTTON_DOWN:
        s->pressed_ = me.widget;
        s->downPos_ = e.position;
        e.isDragging = true;
        e.downPosition = e.position;
        e.clickCount = 1;
        b->mouse(b->obj, e, MouseKind::down);
        break;
    case NEUI_EVENT_MOUSE_MOVE:
        // A held-button drag arrives as MOVE. The latch decides, not buttonmap.
        if (e.isDragging)
            b->mouse(b->obj, e, MouseKind::drag);
        else
            b->mouse(b->obj, e, MouseKind::move);
        break;
    case NEUI_EVENT_MOUSE_BUTTON_UP:
        b->mouse(b->obj, e, MouseKind::up);
        s->pressed_ = widget_none;
        break;
    case NEUI_EVENT_MOUSE_BUTTON_DBLCLICK:
        e.clickCount = 2;
        b->mouse(b->obj, e, MouseKind::doubleClick);
        break;
    case NEUI_EVENT_MOUSE_RBUTTON_DOWN:
        b->mouse(b->obj, e, MouseKind::rightDown);
        break;
    case NEUI_EVENT_MOUSE_RBUTTON_UP:
        b->mouse(b->obj, e, MouseKind::rightUp);
        break;
    default:
        return false;
    }

    return true;
}

} // namespace neuiplusplus
