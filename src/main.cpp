// neui-noodle - a two-component sandbox on top of the neui framework.
//
//   HoverBox : a painted region that goes red while the pointer is inside it
//   SineWave : a plotted sine whose phase follows a horizontal drag
//
// Both are NEUI_W_CUSTOMDRAW widgets. neui hands a painter vtable plus an
// opaque painter handle to the client inside NEUI_EVENT_WIDGET_PAINT, and the
// client draws whatever it likes in widget-local logical pixels.
//
// The neui interface is pure C - vtables of function pointers, integer
// handles, one flat event callback. Everything below is a thin C++20 skin
// over that; see doc/whatis_neui.md for the reasoning.

#include <neui/neui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>

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
constexpr uint32_t k_ink_dim    = 0xFF8A94A6;
constexpr uint32_t k_panel      = 0xFF262B33;
constexpr uint32_t k_panel_edge = 0xFF3C4350;
constexpr uint32_t k_hot        = 0xFFD1495B;
constexpr uint32_t k_hot_edge   = 0xFFF2A0AC;
constexpr uint32_t k_trace      = 0xFF6FD1B0;

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

private:
  neui_painter_api_t* api_;
  neui_painter_t*     p_;
  float               w_, h_;
  bool                focused_;
};

// ---------------------------------------------------------------------------
// Session-level handles the components need in order to talk back to neui
// (invalidate, mainly). Copied by value; both fields are borrowed.
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
  uint32_t id() const { return id_.id; }

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
    const uint32_t body = hovered_ ? k_hot : k_panel;
    c.fill(0, 0, c.width(), c.height(), body);
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
// SineWave - plots a sine across its width; a horizontal drag shifts phase.
// ---------------------------------------------------------------------------
class SineWave final : public Component
{
public:
  void paint(const Canvas& c) override
  {
    const float w = c.width(), h = c.height();
    c.fill(0, 0, w, h, 0xFF1B1F26);
    c.stroke(0.5f, 0.5f, w - 1.0f, h - 1.0f, 1.0f, 0xFF3C4350);

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
      // A held-button drag arrives as MOUSE_MOVE with NEUI_MK_LBUTTON set;
      // there is no separate drag event and no explicit capture call.
      if (!dragging_ || !(m.buttonmap & NEUI_MK_LBUTTON)) return false;
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
// App - owns the session and routes the single flat callback to components.
// ---------------------------------------------------------------------------
struct App
{
  Ui            ui;
  neui_widget_t window{widget_none};
  HoverBox      box;
  SineWave      wave;

  Component* component_for(uint32_t widget_id)
  {
    if (widget_id == box.id())  return &box;
    if (widget_id == wave.id()) return &wave;
    return nullptr;
  }
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

  if (is_mouse_event(event->type))
  {
    if (Component* c = app->component_for(event->data.mouse.widget.id))
      return c->mouse(event->type, event->data.mouse);
  }

  return false;
}

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
  bool prefer_xpl = false;
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--xpl") == 0) prefer_xpl = true;

  // One call registers every host statically linked into this binary; then we
  // pick one by id (or pass nullptr for "first registered", native-first).
  neui_init();
  neui_api_t* host = neui_get_api(prefer_xpl ? k_xpl_host : k_native_host);
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
  app.ui.set_text(app.window, "neui noodle - hover + drag");

  // ...but read the usable rect back rather than trusting the create size: a
  // host that draws an in-frame menubar reports a shorter client here.
  int cx = 0, cy = 0, cw = 600, ch = 400;
  app.ui.widgets->get_client_rect(app.ui.session, app.window, &cx, &cy, &cw, &ch);

  constexpr int margin = 16, gap = 12;
  const int content_h = ch - 2 * margin - gap;
  const int box_h     = content_h / 3;
  const int wave_h    = content_h - box_h;
  const int content_w = cw - 2 * margin;

  app.box.attach(app.ui,
    app.ui.create(app.window, NEUI_W_CUSTOMDRAW,
                  cx + margin, cy + margin, content_w, box_h));
  app.wave.attach(app.ui,
    app.ui.create(app.window, NEUI_W_CUSTOMDRAW,
                  cx + margin, cy + margin + box_h + gap, content_w, wave_h));

  app.ui.widgets->show(app.ui.session, app.window);
  const bool ok = host->run(app.ui.session);
  host->destroy(app.ui.session);
  return ok ? 0 : 1;
}
