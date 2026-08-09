// neui-noodle - a three-component sandbox on top of the neui framework.
//
//   HoverBox    : a painted region that goes red while the pointer is inside it
//   SplitHandle : a three-dot grip that goes yellow on hover and drags the
//                 boundary between the two panes
//   SineWave    : a plotted sine whose phase follows a horizontal drag
//
// All three are NEUI_W_CUSTOMDRAW widgets. neui hands a painter vtable plus an
// opaque painter handle to the client inside NEUI_EVENT_WIDGET_PAINT, and the
// client draws whatever it likes in widget-local logical pixels.
//
// The neui interface is pure C - vtables of function pointers, integer
// handles, one flat event callback. Everything below is a thin C++20 skin
// over that; see doc/whatis_neui.md for the reasoning.

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX           // or windows.h's min/max macros eat std::max / std::clamp
#  include <windows.h>
#  include <shellapi.h>      // CommandLineToArgvW - see wants_xpl() below
#endif

#include <neui/neui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <numbers>
#if defined(_WIN32)
#  include <cwchar>  // wcscmp, for the wide-argv workaround in wants_xpl()
#endif

namespace
{

#if defined(__APPLE__)
constexpr const char* k_native_host = "neui.host.macos";
#elif defined(_WIN32)
constexpr const char* k_native_host = "neui.host.win32";
#else
constexpr const char* k_native_host = "neui.host.crossplatform";
#endif
constexpr const char* k_xpl_host = "neui.host.crossplatform";

constexpr float k_two_pi = 2.0f * std::numbers::pi_v<float>;

// Colours are 0xAARRGGBB everywhere in the painter API.
constexpr uint32_t k_ink_dim     = 0xFF8A94A6;
constexpr uint32_t k_panel       = 0xFF262B33;
constexpr uint32_t k_panel_edge  = 0xFF3C4350;
constexpr uint32_t k_backdrop    = 0xFF1B1F26;
constexpr uint32_t k_hot         = 0xFFD1495B;
constexpr uint32_t k_hot_edge    = 0xFFF2A0AC;
constexpr uint32_t k_yellow      = 0xFFF5C542;
constexpr uint32_t k_yellow_wash = 0xFF33301E;
constexpr uint32_t k_trace       = 0xFF6FD1B0;

// ---------------------------------------------------------------------------
// A C++ view over the (painter_api, painter) pair. Every painter call needs
// the opaque handle threaded through it, which is exactly the kind of
// bookkeeping a member function should be doing for us.
// ---------------------------------------------------------------------------
class Canvas
{
public:
  explicit Canvas(const neui_event_paint_t& e)
    : api_(e.painter_api), p_(e.p), w_(e.width), h_(e.height), focused_(e.focused)
  {
  }

  float width()   const { return w_; }
  float height()  const { return h_; }
  bool  focused() const { return focused_; }

  void fill(float x, float y, float w, float h, uint32_t argb) const
  {
    api_->fill_rect(p_, x, y, w, h, argb);
  }
  void stroke(float x, float y, float w, float h, float thickness, uint32_t argb) const
  {
    api_->draw_rect(p_, x, y, w, h, thickness, argb);
  }
  void text(float x, float y, float w, float h, const char* utf8,
            float size, uint32_t argb) const
  {
    api_->draw_text(p_, x, y, w, h, utf8, size, argb);
  }
  float measure(const char* utf8, float size) const
  {
    return api_->measure_text(p_, utf8, -1, size);
  }

  // Path building. begin() resets, then move/line, then a stroke or fill
  // commits it.
  void begin()                        const { api_->begin_path(p_); }
  void move_to(float x, float y)      const { api_->move_to(p_, x, y); }
  void line_to(float x, float y)      const { api_->line_to(p_, x, y); }
  void stroke_path(float w, uint32_t argb) const { api_->stroke_path(p_, w, argb); }

  // The painter has no circle primitive - a filled dot is a full arc sweep
  // committed with fill_path.
  void fill_circle(float cx, float cy, float r, uint32_t argb) const
  {
    api_->begin_path(p_);
    api_->arc(p_, cx, cy, r, 0.0f, k_two_pi);
    api_->fill_path(p_, argb);
  }

private:
  neui_painter_api_t* api_;
  neui_painter_t*     p_;
  float               w_, h_;
  bool                focused_;
};

// Is this MOUSE_MOVE part of a held-button drag?
//
// There is no drag event and no capture call - a drag is just MOUSE_MOVE
// between BUTTON_DOWN and BUTTON_UP. But the hosts disagree about the
// buttonmap field on MOUSE_MOVE:
//   macOS native  - sets NEUI_MK_LBUTTON explicitly on mouseDragged:
//   crossplatform - forwards the real Win32 wParam / equivalent, so MK_LBUTTON
//   win32 native  - hardcodes 0 (ChildSubclassProc, hosts/win32/window.cpp)
// So the latched `dragging` flag is the portable gate, and buttonmap is only
// consulted when the host actually populated it. Both hosts capture the mouse
// on button-down, so the matching BUTTON_UP always arrives and the flag can't
// get stuck.
bool is_drag_move(bool dragging, uint32_t buttonmap)
{
  if (!dragging) return false;
  return buttonmap == 0 || (buttonmap & NEUI_MK_LBUTTON) != 0;
}

// ---------------------------------------------------------------------------
// Session-level handles the components need in order to talk back to neui
// (invalidate and set_pos, mainly). Copied by value; both fields are borrowed.
// ---------------------------------------------------------------------------
struct Ui
{
  neui_session_t     session{};
  neui_widget_api_t* widgets{nullptr};

  neui_widget_t create(neui_widget_t parent, const char* type,
                       int x, int y, int w, int h) const
  {
    return widgets->create(session, parent, type, x, y, w, h, nullptr);
  }
  void set_pos(neui_widget_t w, int x, int y, int width, int height) const
  {
    widgets->set_pos(session, w, x, y, width, height);
  }
  void invalidate(neui_widget_t w) const { widgets->invalidate(session, w); }
  void set_text(neui_widget_t w, const char* t) const { widgets->set_text(session, w, t); }
};

// ---------------------------------------------------------------------------
// Component: one CUSTOMDRAW widget plus the state that goes with it. This is
// the piece neui deliberately does not provide - it hands out a widget id and
// a flat callback, and leaves the "which object is this event for" question
// to the client.
// ---------------------------------------------------------------------------
class Component
{
public:
  virtual ~Component() = default;

  void attach(const Ui& ui, neui_widget_t w) { ui_ = ui; id_ = w; }
  neui_widget_t widget() const { return id_; }
  uint32_t      id()     const { return id_.id; }

  virtual void paint(const Canvas&) {}
  // Return true when the event is handled, which stops neui forwarding it to
  // the widget's own internal handling.
  virtual bool mouse(neui_event_type_t, const neui_event_mouse_t&) { return false; }

protected:
  void repaint() const { ui_.invalidate(id_); }

  Ui            ui_{};
  neui_widget_t id_{widget_none};
};

// ---------------------------------------------------------------------------
// HoverBox - fills its whole area, red while hovered.
// ---------------------------------------------------------------------------
class HoverBox final : public Component
{
public:
  void paint(const Canvas& c) override
  {
    c.fill(0, 0, c.width(), c.height(), hovered_ ? k_hot : k_panel);
    c.stroke(0.5f, 0.5f, c.width() - 1.0f, c.height() - 1.0f, 1.0f,
             hovered_ ? k_hot_edge : k_panel_edge);

    const char* line = hovered_ ? "hot" : "hover me";
    const float size = 20.0f;
    const float tw   = c.measure(line, size);
    c.text((c.width() - tw) * 0.5f, (c.height() - size * 1.4f) * 0.5f,
           tw + 4.0f, size * 1.4f, line, size, hovered_ ? 0xFFFFFFFF : k_ink_dim);
  }

  bool mouse(neui_event_type_t type, const neui_event_mouse_t&) override
  {
    switch (type)
    {
    case NEUI_EVENT_MOUSE_ENTER:
      hovered_ = true;
      repaint();
      return true;
    case NEUI_EVENT_MOUSE_LEAVE:
      hovered_ = false;
      repaint();
      return true;
    default:
      return false;
    }
  }

private:
  bool hovered_{false};
};

// ---------------------------------------------------------------------------
// SplitHandle - three dots, yellow while hovered or dragging. Reports drag
// movement as a signed pixel delta; the owner decides what that means.
// ---------------------------------------------------------------------------
class SplitHandle final : public Component
{
public:
  std::function<void(int)> on_drag;

  void paint(const Canvas& c) override
  {
    const bool lit = hovered_ || dragging_;

    c.fill(0, 0, c.width(), c.height(), lit ? k_yellow_wash : k_backdrop);
    // Hairlines top and bottom so the handle reads as a seam, not a third pane.
    c.fill(0, 0, c.width(), 1.0f, k_panel_edge);
    c.fill(0, c.height() - 1.0f, c.width(), 1.0f, k_panel_edge);

    const float cx  = c.width() * 0.5f;
    const float cy  = c.height() * 0.5f;
    const float r   = dragging_ ? 4.0f : 3.0f;
    const uint32_t dot = lit ? k_yellow : k_ink_dim;
    for (int i = -1; i <= 1; ++i)
      c.fill_circle(cx + static_cast<float>(i) * k_dot_spacing, cy, r, dot);
  }

  bool mouse(neui_event_type_t type, const neui_event_mouse_t& m) override
  {
    switch (type)
    {
    case NEUI_EVENT_MOUSE_ENTER:
      hovered_ = true;
      repaint();
      return true;

    case NEUI_EVENT_MOUSE_LEAVE:
      // Not while dragging: the handle slides out from under the pointer at
      // the clamp limits, and going grey mid-drag looks broken.
      hovered_ = false;
      if (!dragging_) repaint();
      return true;

    case NEUI_EVENT_MOUSE_BUTTON_DOWN:
      dragging_ = true;
      grab_y_   = m.y;
      repaint();
      return true;

    case NEUI_EVENT_MOUSE_MOVE:
      if (!is_drag_move(dragging_, m.buttonmap)) return false;
      // m.y is local to where the handle is *now*, so this delta is really an
      // absolute "where should the seam be" measurement: it stays correct even
      // when the split clamps and the handle stops tracking the pointer.
      if (on_drag) on_drag(m.y - grab_y_);
      return true;

    case NEUI_EVENT_MOUSE_BUTTON_UP:
      if (!dragging_) return false;
      dragging_ = false;
      repaint();
      return true;

    default:
      return false;
    }
  }

private:
  static constexpr float k_dot_spacing = 30.0f;

  bool hovered_{false};
  bool dragging_{false};
  int  grab_y_{0};
};

// ---------------------------------------------------------------------------
// SineWave - plots a sine across its width; a horizontal drag shifts phase.
// ---------------------------------------------------------------------------
class SineWave final : public Component
{
public:
  void paint(const Canvas& c) override
  {
    const float w = c.width(), h = c.height();
    c.fill(0, 0, w, h, k_backdrop);
    c.stroke(0.5f, 0.5f, w - 1.0f, h - 1.0f, 1.0f, k_panel_edge);

    const float mid = h * 0.5f;
    const float amp = std::max(4.0f, h * 0.34f);

    // Zero line + amplitude rails.
    c.fill(1.0f, mid, w - 2.0f, 1.0f, 0xFF39404D);
    c.fill(1.0f, mid - amp, w - 2.0f, 1.0f, 0xFF2C323C);
    c.fill(1.0f, mid + amp, w - 2.0f, 1.0f, 0xFF2C323C);

    // One sample per logical pixel is plenty at this scale.
    const int steps = std::max(2, static_cast<int>(w));
    c.begin();
    for (int i = 0; i <= steps; ++i)
    {
      const float t = static_cast<float>(i) / static_cast<float>(steps);
      const float x = t * w;
      const float y = mid - amp * std::sin(t * k_cycles * k_two_pi + phase_);
      if (i == 0) c.move_to(x, y);
      else        c.line_to(x, y);
    }
    c.stroke_path(2.0f, dragging_ ? 0xFFA8F0D8 : k_trace);

    char label[64];
    std::snprintf(label, sizeof label, "phase %6.2f rad  (%3.0f deg)  -  drag me",
                  static_cast<double>(phase_),
                  static_cast<double>(phase_ * 180.0f / std::numbers::pi_v<float>));
    c.text(8.0f, h - 24.0f, w - 16.0f, 18.0f, label, 13.0f, k_ink_dim);
  }

  bool mouse(neui_event_type_t type, const neui_event_mouse_t& m) override
  {
    switch (type)
    {
    case NEUI_EVENT_MOUSE_BUTTON_DOWN:
      dragging_ = true;
      last_x_   = m.x;
      repaint();
      return true;

    case NEUI_EVENT_MOUSE_MOVE:
      if (!is_drag_move(dragging_, m.buttonmap)) return false;
      set_phase(phase_ + static_cast<float>(m.x - last_x_) * k_rad_per_px);
      last_x_ = m.x;
      repaint();
      return true;

    case NEUI_EVENT_MOUSE_BUTTON_UP:
      if (!dragging_) return false;
      dragging_ = false;
      repaint();
      return true;

    default:
      return false;
    }
  }

private:
  void set_phase(float p)
  {
    phase_ = std::fmod(p, k_two_pi);
    if (phase_ < 0.0f) phase_ += k_two_pi;
  }

  static constexpr float k_cycles     = 3.0f;    // wavelengths across the width
  static constexpr float k_rad_per_px = 0.012f;  // drag sensitivity

  float phase_{0.0f};
  bool  dragging_{false};
  int   last_x_{0};
};

// ---------------------------------------------------------------------------
// App - owns the session, holds the layout, and routes the single flat
// callback to components. neui ships no layout engine, so the arithmetic
// below is the whole "layout system" and relayout() is the only place that
// moves a widget.
// ---------------------------------------------------------------------------
struct App
{
  Ui            ui;
  neui_widget_t window{widget_none};
  HoverBox      box;
  SplitHandle   handle;
  SineWave      wave;

  Component* component_for(uint32_t widget_id)
  {
    if (widget_id == box.id())    return &box;
    if (widget_id == handle.id()) return &handle;
    if (widget_id == wave.id())   return &wave;
    return nullptr;
  }

  // Re-read the frame's usable rect and inset it. Called at startup and on
  // every frame resize.
  void read_content_rect()
  {
    int x = 0, y = 0, w = 0, h = 0;
    ui.widgets->get_client_rect(ui.session, window, &x, &y, &w, &h);
    cx = x + k_margin;
    cy = y + k_margin;
    cw = std::max(1, w - 2 * k_margin);
    ch = std::max(1, h - 2 * k_margin);
  }

  // split is the top pane's height; the handle sits directly below it and the
  // bottom pane takes the rest.
  void relayout()
  {
    const int panes = std::max(2 * k_min_pane, ch - k_handle_h);
    split = std::clamp(split, k_min_pane, panes - k_min_pane);

    ui.set_pos(box.widget(),    cx, cy,                          cw, split);
    ui.set_pos(handle.widget(), cx, cy + split,                  cw, k_handle_h);
    ui.set_pos(wave.widget(),   cx, cy + split + k_handle_h,     cw, panes - split);

    ui.invalidate(box.widget());
    ui.invalidate(handle.widget());
    ui.invalidate(wave.widget());
  }

  static constexpr int k_margin   = 16;
  static constexpr int k_handle_h = 22;
  static constexpr int k_min_pane = 48;

  int cx{0}, cy{0}, cw{0}, ch{0};
  int split{0};
};

// Every payload in the event union carries a .widget, but the union member
// differs per category - so pulling the id out is a switch, not a field read.
bool is_mouse_event(neui_event_type_t t)
{
  switch (t)
  {
  case NEUI_EVENT_MOUSE_MOVE:
  case NEUI_EVENT_MOUSE_ENTER:
  case NEUI_EVENT_MOUSE_LEAVE:
  case NEUI_EVENT_MOUSE_BUTTON_DOWN:
  case NEUI_EVENT_MOUSE_BUTTON_UP:
  case NEUI_EVENT_MOUSE_BUTTON_CLICK:
  case NEUI_EVENT_MOUSE_BUTTON_DBLCLICK:
  case NEUI_EVENT_MOUSE_RBUTTON_DOWN:
  case NEUI_EVENT_MOUSE_RBUTTON_UP:
    return true;
  default:
    return false;
  }
}

bool NEUI_ABI on_event(void* token, neui_event_t* event)
{
  auto* app = static_cast<App*>(token);

  // Closing the window asks first; returning true lets the host tear down.
  if (event->type == NEUI_EVENT_APP_QUIT) return true;

  if (event->type == NEUI_EVENT_WIDGET_PAINT)
  {
    if (Component* c = app->component_for(event->data.paint.widget.id))
    {
      Canvas canvas(event->data.paint);
      c->paint(canvas);
      return true;
    }
    return false;
  }

  // resize.widget is the FRAME - check it, or a second window would relayout
  // this one. (Not emitted by the crossplatform host on macOS today, so under
  // --xpl the panes keep their size when the window grows.)
  if (event->type == NEUI_EVENT_RESIZE)
  {
    if (event->data.resize.widget.id != app->window.id) return false;
    app->read_content_rect();
    app->relayout();
    return true;
  }

  if (is_mouse_event(event->type))
  {
    if (Component* c = app->component_for(event->data.mouse.widget.id))
      return c->mouse(event->type, event->data.mouse);
  }

  return false;
}

// Is --xpl on the command line?
//
// On Windows this deliberately ignores argv. The win32 host owns WinMain
// (hosts/win32/window.cpp) and calls main() with the WIDE argv from
// CommandLineToArgvW, against a local `int main(int, wchar_t**)` declaration -
// so what actually arrives in a standard `char**` parameter is an array of
// wchar_t*. Reading the command line ourselves is the honest way to get at it.
// (neui's own examples sidestep this by taking argv and immediately voiding
// it.)
#if defined(_WIN32)
bool wants_xpl(int /*argc*/, char** /*argv*/)
{
  int       wargc = 0;
  wchar_t** wargv = ::CommandLineToArgvW(::GetCommandLineW(), &wargc);
  if (!wargv) return false;
  bool found = false;
  for (int i = 1; i < wargc && !found; ++i)
    found = (std::wcscmp(wargv[i], L"--xpl") == 0);
  ::LocalFree(wargv);
  return found;
}
#else
bool wants_xpl(int argc, char** argv)
{
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--xpl") == 0) return true;
  return false;
}
#endif

void* NEUI_ABI get_interface(void* /*token*/, const char* iface)
{
  // neui asks the client for an interface by name, the mirror image of the
  // client asking the host. Only the widgets one is interesting here.
  static neui_widget_client_t widget_client{NEUI_VERSION, nullptr, on_event};
  if (std::strcmp(iface, NEUI_API_WIDGETS) == 0) return &widget_client;
  return nullptr;
}

}  // namespace

int main(int argc, char* argv[])
{
  // One call registers every host statically linked into this binary; then we
  // pick one by id (or pass nullptr for "first registered", native-first).
  neui_init();
  neui_api_t* host = neui_get_api(wants_xpl(argc, argv) ? k_xpl_host : k_native_host);
  if (!host) host = neui_get_api(nullptr);
  if (!host)
  {
    std::fprintf(stderr, "neui: no host registered\n");
    return 1;
  }

  App app;
  neui_client_t client{NEUI_VERSION, get_interface};

  // The token is handed back to us on every callback - this is neui's stand-in
  // for a `this` pointer.
  app.ui.session = host->create_session(&client, &app);
  if (!app.ui.session.session)
  {
    std::fprintf(stderr, "neui: could not create session\n");
    return 1;
  }
  app.ui.widgets =
    static_cast<neui_widget_api_t*>(host->get_interface(app.ui.session, NEUI_API_WIDGETS));
  if (!app.ui.widgets)
  {
    std::fprintf(stderr, "neui: host has no widget API\n");
    return 1;
  }

  // For a top-level frame, width/height is the CLIENT area at 96 DPI - the
  // host grows the outer window for title bar and borders itself.
  app.window = app.ui.create(widget_none, NEUI_W_APPWINDOW, 140, 140, 600, 400);
  app.ui.set_text(app.window, "neui noodle - hover, drag, split");

  // Create at any size; relayout() below puts everything where it belongs.
  app.box.attach(app.ui, app.ui.create(app.window, NEUI_W_CUSTOMDRAW, 0, 0, 10, 10));
  app.handle.attach(app.ui, app.ui.create(app.window, NEUI_W_CUSTOMDRAW, 0, 0, 10, 10));
  app.wave.attach(app.ui, app.ui.create(app.window, NEUI_W_CUSTOMDRAW, 0, 0, 10, 10));

  app.handle.on_drag = [&app](int dy) {
    app.split += dy;
    app.relayout();
  };

  app.read_content_rect();
  app.split = (app.ch - App::k_handle_h) / 3;
  app.relayout();

  app.ui.widgets->show(app.ui.session, app.window);
  const bool ok = host->run(app.ui.session);
  host->destroy(app.ui.session);
  return ok ? 0 : 1;
}
