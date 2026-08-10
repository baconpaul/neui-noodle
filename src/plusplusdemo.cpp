/*
 * plusplusdemo - a standalone app exercising the neuiplusplus layer.
 *
 * A deliberately crappy knob and slider, and a readout underneath showing both
 * normalised values. The point is not the controls - it is what the component
 * code looks like: no session handles, no painter handle threading, no event
 * union, no zoom arithmetic, and capability opt-in by inheritance.
 */

#include <neuiplusplus/neuiplusplus.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <numbers>
#include <string>

namespace npp = neuiplusplus;

namespace
{

constexpr float k_two_pi = 2.0f * std::numbers::pi_v<float>;

// A tiny shared palette. constexpr, so it costs nothing at runtime.
struct Palette
{
    npp::Color window = npp::Color::rgb(0x1B, 0x1F, 0x26);
    npp::Color panel = npp::Color::rgb(0x26, 0x2B, 0x33);
    npp::Color edge = npp::Color::rgb(0x3C, 0x43, 0x50);
    npp::Color ink = npp::Color::rgb(0xE6, 0xEA, 0xF0);
    npp::Color inkDim = npp::Color::rgb(0x8A, 0x94, 0xA6);
    npp::Color accent = npp::Color::rgb(0x6F, 0xD1, 0xB0);
    npp::Color hot = npp::Color::rgb(0xF5, 0xC5, 0x42);
};
constexpr Palette pal{};

// ---------------------------------------------------------------------------
// A normalised [0, 1] parameter with a change callback. Stands in for
// sst-jucegui's data::Continuous; the real thing would be an interface.
struct Value
{
    float get() const { return v; }
    void set(float nv)
    {
        nv = nv < 0.0f ? 0.0f : (nv > 1.0f ? 1.0f : nv);
        if (nv == v)
            return;
        v = nv;
        if (onChange)
            onChange();
    }
    void nudge(float d) { set(v + d); }

    std::string label;
    std::function<void()> onChange;

  private:
    float v{0.5f};
};

// ---------------------------------------------------------------------------
// Knob - drag vertically. Paintable + MouseHandling, nothing else.

struct Knob : npp::Component, npp::Paintable, npp::MouseHandling
{
    Knob(npp::Parent p, Value &value) : Component(p), val(value) {}

    void paint(npp::Canvas &g) override
    {
        const auto b = g.bounds();
        const float dia = std::min(b.getWidth(), b.getHeight() - 16.0f);
        const npp::Rect face{(b.getWidth() - dia) * 0.5f, 0.0f, dia, dia};

        g.fillEllipse(face, pal.panel);
        g.drawEllipse(face, 1.0f, hovered ? pal.hot : pal.edge);

        // Value arc. -135deg to +135deg, clockwise from bottom-left.
        const float a0 = k_two_pi * 0.375f;
        const float sweep = k_two_pi * 0.75f * val.get();
        const npp::Point c = face.getCentre();
        const float r = dia * 0.5f - 5.0f;
        g.beginPath();
        g.arcTo(c, r, a0, a0 + sweep);
        g.strokePath(4.0f, dragging ? pal.hot : pal.accent);

        // Pointer.
        const float a = a0 + sweep;
        g.drawLine(npp::Point{c.x + std::cos(a) * r * 0.35f, c.y + std::sin(a) * r * 0.35f},
                   npp::Point{c.x + std::cos(a) * r, c.y + std::sin(a) * r}, 2.0f, pal.ink);

        g.drawText(val.label, b.withTrimmedTop(dia), 12.0f, pal.inkDim, npp::HAlign::centre,
                   npp::VAlign::middle);
    }

    void mouseEnter(const npp::MouseEvent &) override
    {
        hovered = true;
        repaint();
    }
    void mouseExit(const npp::MouseEvent &) override
    {
        hovered = false;
        repaint();
    }
    void mouseDown(const npp::MouseEvent &e) override
    {
        dragging = true;
        anchorY = e.position.y;
        repaint();
    }
    void mouseDrag(const npp::MouseEvent &e) override
    {
        const float fine = e.mods.fine() ? 0.25f : 1.0f;
        val.nudge((anchorY - e.position.y) * 0.006f * fine);
        anchorY = e.position.y;
        repaint();
    }
    void mouseUp(const npp::MouseEvent &) override
    {
        dragging = false;
        repaint();
    }
    void mouseDoubleClick(const npp::MouseEvent &) override { val.set(0.5f); }
    void mouseWheel(const npp::WheelEvent &e) override { val.nudge(e.delta * 0.02f); }

    Value &val;
    float anchorY{0.0f};
    bool hovered{false};
    bool dragging{false};
};

// ---------------------------------------------------------------------------
// Slider - drag horizontally, absolute position.

struct Slider : npp::Component, npp::Paintable, npp::MouseHandling
{
    Slider(npp::Parent p, Value &value) : Component(p), val(value) {}

    void paint(npp::Canvas &g) override
    {
        const auto b = g.bounds();
        const auto track = npp::Rect{0.0f, b.getCentreY() - 3.0f, b.getWidth(), 6.0f}
                               .withTrimmedTop(0.0f)
                               .withTrimmedLeft(handleW * 0.5f)
                               .withTrimmedRight(handleW * 0.5f);

        g.fillRoundRect(track, 3.0f, pal.panel);
        g.fillRoundRect(track.withWidth(track.getWidth() * val.get()), 3.0f,
                        dragging ? pal.hot : pal.accent);

        const float hx = track.getX() + track.getWidth() * val.get() - handleW * 0.5f;
        const npp::Rect handle{hx, b.getCentreY() - 9.0f, handleW, 18.0f};
        g.fillRoundRect(handle, 2.0f, hovered ? pal.hot : pal.ink);

        g.drawText(val.label, b, 12.0f, pal.inkDim, npp::HAlign::left, npp::VAlign::bottom);
    }

    void mouseEnter(const npp::MouseEvent &) override
    {
        hovered = true;
        repaint();
    }
    void mouseExit(const npp::MouseEvent &) override
    {
        hovered = false;
        repaint();
    }
    void mouseDown(const npp::MouseEvent &e) override
    {
        dragging = true;
        setFromX(e.position.x);
    }
    void mouseDrag(const npp::MouseEvent &e) override { setFromX(e.position.x); }
    void mouseUp(const npp::MouseEvent &) override
    {
        dragging = false;
        repaint();
    }

    void setFromX(float x)
    {
        const float w = std::max(1.0f, bounds().getWidth() - handleW);
        val.set((x - handleW * 0.5f) / w);
        repaint();
    }

    static constexpr float handleW = 10.0f;

    Value &val;
    bool hovered{false};
    bool dragging{false};
};

// ---------------------------------------------------------------------------
// Readout - Paintable only. No input interfaces, so neuiplusplus leaves
// emit_events off and it is never hit-tested.

struct Readout : npp::Component, npp::Paintable
{
    Readout(npp::Parent p, Value &a, Value &b) : Component(p), knob(a), slider(b) {}

    void paint(npp::Canvas &g) override
    {
        const auto b = g.bounds();
        g.fillRoundRect(b, 3.0f, pal.panel);
        g.drawRoundRect(b, 3.0f, 1.0f, pal.edge);

        char line[128];
        std::snprintf(line, sizeof line, "%s %.3f          %s %.3f", knob.label.c_str(),
                      double(knob.get()), slider.label.c_str(), double(slider.get()));
        g.drawText(line, b, 15.0f, pal.ink, npp::HAlign::centre, npp::VAlign::middle);
    }

    Value &knob;
    Value &slider;
};

// ---------------------------------------------------------------------------
// The panel owns the controls and lays them out. Resizable, so neui's RESIZE
// lands here; Paintable for the background.

struct Panel : npp::Component, npp::Paintable, npp::Resizable
{
    Panel(npp::Parent p, Value &kv, Value &sv)
        : Component(p), knob(add<Knob>(kv)), slider(add<Slider>(sv)),
          readout(add<Readout>(kv, sv))
    {
    }

    void paint(npp::Canvas &g) override { g.fillAll(pal.window); }

    void resized() override
    {
        auto area = localBounds().reduced(16.0f);
        readout.setBounds(area.removeFromBottom(40.0f));
        area.removeFromBottom(16.0f);
        knob.setBounds(area.removeFromLeft(110.0f).withHeight(area.getHeight()));
        area.removeFromLeft(24.0f);
        slider.setBounds(area.withHeight(40.0f).translated(0.0f, area.getHeight() * 0.5f - 20.0f));
    }

    Knob &knob;
    Slider &slider;
    Readout &readout;
};

} // namespace

int main(int argc, char *argv[])
{
    bool xpl = true; // the host neui recommends for this style of UI
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--native") == 0)
            xpl = false;

    neui_init();
    neui_api_t *host = neui_get_api(xpl ? "neui.host.crossplatform" : nullptr);
    if (!host)
        host = neui_get_api(nullptr);
    if (!host)
    {
        std::fprintf(stderr, "neui: no host registered\n");
        return 1;
    }

    auto session = npp::Session::create(host);
    if (!session)
    {
        std::fprintf(stderr, "neui: could not create session\n");
        return 1;
    }

    Value knobValue, sliderValue;
    knobValue.label = "knob";
    sliderValue.label = "slider";

    npp::Frame frame{*session, NEUI_W_APPWINDOW, npp::Rect{140, 140, 520, 260},
                     "neuiplusplus demo"};

    auto &panel = frame.add<Panel>(knobValue, sliderValue);
    panel.setBounds(frame.clientBounds().atOrigin());
    panel.resized();

    // Both controls drive the same readout - the whole point of the callback.
    knobValue.onChange = [&panel] { panel.readout.repaint(); };
    sliderValue.onChange = [&panel] { panel.readout.repaint(); };

    frame.show();
    const bool ok = host->run(session->raw());
    return ok ? 0 : 1;
}
