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
#include <filesystem>
#include <functional>
#include <numbers>
#include <string>
#include <system_error>

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

struct Knob : npp::Component<Knob, npp::Paints, npp::MouseEvents, npp::KeyboardEvents,
                             npp::FocusEvents>
{
    Knob(npp::Parent p, Value &value) : Component(p), val(value)
    {
        setCursor(npp::Cursor::openHand); // "grab me"
        // Declaring slider makes the AT offer increment / decrement, which
        // arrive as LEFT / RIGHT on a hand-painted widget - hence
        // KeyboardHandling above. Declare the real-world range too, so the AT
        // announces something meaningful rather than "0.62".
        setAccessibleRole(npp::Role::slider);
        setAccessibleName(val.label.c_str());
        setAccessibleValueRange(0.0f, 100.0f, 1.0f);
        publishValue();
    }

    // The value lives in client state, so nothing tells the AT about it but us.
    void publishValue()
    {
        char text[32];
        std::snprintf(text, sizeof text, "%.0f%%", double(val.get() * 100.0f));
        setAccessibleValue(val.get());
        setAccessibleValueText(text);
        notifyAccessible(npp::A11yChange::value);
    }

    void paint(npp::Canvas &g) override
    {
        // Inset by a pixel: neui clips paint to the widget rect, and a stroke
        // is centred on its path, so anything drawn flush to the edge loses its
        // outer half-pixel. Applies to the ring outline and the value arc.
        const auto b = g.bounds().reduced(1.0f);
        const float dia = std::min(b.getWidth(), b.getHeight() - 16.0f);
        const npp::Rect face{b.getX() + (b.getWidth() - dia) * 0.5f, b.getY(), dia, dia};

        g.fillEllipse(face, pal.panel);
        g.drawEllipse(face, 1.0f, hovered ? pal.hot : pal.edge);
        if (focused)
            g.drawEllipse(face.expanded(1.0f), 1.0f, pal.accent);

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
        // Relative pointer for the drag: the cursor pins and hides, motion
        // keeps coming past the screen edge, and on release the pointer is put
        // back where it was grabbed. It handles the hide itself, so there is no
        // setCursor(hidden) here - the hover cursor set in the constructor
        // stands throughout.
        beginRelativeDrag();
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
        endRelativeDrag();
        repaint();
    }
    void mouseDoubleClick(const npp::MouseEvent &) override { val.set(0.5f); }
    void mouseWheel(const npp::WheelEvent &e) override
    {
        // A value control wants the direction the fingers moved, not the
        // content-scrolling direction the OS handed us.
        val.nudge(e.physicalDelta() * 0.02f);
    }

    // The actions the declared slider role obliges us to perform. An AT
    // increment / decrement arrives here as an ordinary key event.
    bool keyPressed(const npp::KeyEvent &e) override
    {
        const float step = e.mods.fine() ? 0.01f : 0.05f;
        if (e.keyCode == NEUI_KEY_RIGHT || e.keyCode == NEUI_KEY_UP)
        {
            val.nudge(step);
            return true;
        }
        if (e.keyCode == NEUI_KEY_LEFT || e.keyCode == NEUI_KEY_DOWN)
        {
            val.nudge(-step);
            return true;
        }
        return false;
    }

    void focusGained() override
    {
        focused = true;
        repaint();
    }
    void focusLost() override
    {
        focused = false;
        repaint();
    }

    Value &val;
    float anchorY{0.0f};
    bool hovered{false};
    bool dragging{false};
    bool focused{false};
};

// ---------------------------------------------------------------------------
// Slider - drag horizontally, absolute position.

struct Slider : npp::Component<Slider, npp::Paints, npp::MouseEvents, npp::KeyboardEvents,
                               npp::FocusEvents>
{
    // Deliberately NOT a relative-pointer drag: this maps position to value
    // absolutely, so it wants the pointer bounded. Relative mode is for
    // delta-driven controls like the knob.
    Slider(npp::Parent p, Value &value) : Component(p), val(value)
    {
        setCursor(npp::Cursor::ewResize); // it drags horizontally
        setAccessibleRole(npp::Role::slider);
        setAccessibleName(val.label.c_str());
        setAccessibleValueRange(0.0f, 100.0f, 1.0f);
        publishValue();
    }

    void publishValue()
    {
        char text[32];
        std::snprintf(text, sizeof text, "%.0f%%", double(val.get() * 100.0f));
        setAccessibleValue(val.get());
        setAccessibleValueText(text);
        notifyAccessible(npp::A11yChange::value);
    }

    bool keyPressed(const npp::KeyEvent &e) override
    {
        const float step = e.mods.fine() ? 0.01f : 0.05f;
        if (e.keyCode == NEUI_KEY_RIGHT || e.keyCode == NEUI_KEY_UP)
        {
            val.nudge(step);
            return true;
        }
        if (e.keyCode == NEUI_KEY_LEFT || e.keyCode == NEUI_KEY_DOWN)
        {
            val.nudge(-step);
            return true;
        }
        return false;
    }

    void focusGained() override
    {
        focused = true;
        repaint();
    }
    void focusLost() override
    {
        focused = false;
        repaint();
    }

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
        if (focused)
            g.drawRoundRect(handle.expanded(2.0f), 3.0f, 1.0f, pal.accent);

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

    void mouseWheel(const npp::WheelEvent &e) override
    {
        // A value control wants the direction the fingers moved, not the
        // content-scrolling direction the OS handed us.
        val.nudge(e.physicalDelta() * 0.02f);
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
    bool focused{false};
};

// ---------------------------------------------------------------------------
// Button - a text button with an onClick. Paintable + MouseHandling; the
// pressed/hover state is entirely local.

struct Button : npp::Component<Button, npp::Paints, npp::MouseEvents>
{
    Button(npp::Parent p, std::string caption) : Component(p), text(std::move(caption))
    {
        setCursor(npp::Cursor::hand);
    }

    void paint(npp::Canvas &g) override
    {
        const auto b = g.bounds().reduced(1.0f);
        const auto fill = pressed ? pal.accent : (hovered ? pal.edge : pal.panel);
        g.fillRoundRect(b, 3.0f, fill);
        g.drawRoundRect(b, 3.0f, 1.0f, hovered ? pal.hot : pal.edge);
        g.drawText(text, b, 13.0f, pressed ? pal.window : pal.ink, npp::HAlign::centre,
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
    void mouseDown(const npp::MouseEvent &) override
    {
        pressed = true;
        repaint();
    }
    void mouseUp(const npp::MouseEvent &e) override
    {
        const bool wasPressed = pressed;
        pressed = false;
        repaint();
        // Only fire when released inside, the usual button contract.
        if (wasPressed && localBounds().contains(e.position) && onClick)
            onClick();
    }

    std::string text;
    std::function<void()> onClick;
    bool hovered{false};
    bool pressed{false};
};

// ---------------------------------------------------------------------------
// Readout - Paintable only. No input interfaces, so neuiplusplus leaves
// emit_events off and it is never hit-tested.

struct Readout : npp::Component<Readout, npp::Paints>
{
    Readout(npp::Parent p, Value &a, Value &b) : Component(p), knob(a), slider(b)
    {
        // Not interactive, but it carries information a screen-reader user
        // wants, so it announces as static text rather than being hidden.
        setAccessibleRole(npp::Role::staticText);
        publishText();
    }

    void publishText()
    {
        std::snprintf(line, sizeof line, "%s %.3f          %s %.3f", knob.label.c_str(),
                      double(knob.get()), slider.label.c_str(), double(slider.get()));
        // A static-text node speaks its NAME, and ours is client-drawn text the
        // framework cannot see, so publish it and say it changed.
        setAccessibleName(line);
        notifyAccessible(npp::A11yChange::name);
    }

    void paint(npp::Canvas &g) override
    {
        const auto b = g.bounds();
        g.fillRoundRect(b, 3.0f, pal.panel);
        g.drawRoundRect(b, 3.0f, 1.0f, pal.edge);
        g.drawText(line, b, 15.0f, pal.ink, npp::HAlign::centre, npp::VAlign::middle);
    }

    Value &knob;
    Value &slider;
    char line[128]{};
};

// ---------------------------------------------------------------------------
// FileInfo - one line about whatever the picker last returned.

struct FileInfo : npp::Component<FileInfo, npp::Paints>
{
    explicit FileInfo(npp::Parent p) : Component(p)
    {
        // Left alone it would report as an empty GROUP, which is a node an AT
        // user has to navigate past for nothing. It carries text, so say so.
        setAccessibleRole(npp::Role::staticText);
        setAccessibleName(text.c_str());
    }

    void paint(npp::Canvas &g) override
    {
        g.drawText(text, g.bounds(), 12.0f, pal.inkDim, npp::HAlign::left, npp::VAlign::middle);
    }

    void show(const std::string &path, std::uintmax_t bytes)
    {
        const auto name = std::filesystem::path(path).filename().string();
        char line[320];
        std::snprintf(line, sizeof line, "%s  -  %.1f KB (%llu bytes)", name.c_str(),
                      double(bytes) / 1024.0, static_cast<unsigned long long>(bytes));
        text = line;
        publish();
    }
    void showMessage(std::string m)
    {
        text = std::move(m);
        publish();
    }
    void publish()
    {
        setAccessibleName(text.c_str());
        notifyAccessible(npp::A11yChange::name);
        repaint();
    }

    std::string text{"no file picked"};
};

// ---------------------------------------------------------------------------
// The panel owns the controls and lays them out. Resizable, so neui's RESIZE
// lands here; Paintable for the background.

struct Panel : npp::Component<Panel, npp::Paints, npp::Resizes>
{
    Panel(npp::Parent p, Value &kv, Value &sv)
        : Component(p), knob(add<Knob>(kv)), slider(add<Slider>(sv)),
          readout(add<Readout>(kv, sv)), pick(add<Button>("pick file")),
          fileInfo(add<FileInfo>())
    {
    }

    void paint(npp::Canvas &g) override { g.fillAll(pal.window); }

    void resized() override
    {
        auto area = localBounds().reduced(16.0f);
        fileInfo.setBounds(area.removeFromBottom(22.0f));
        area.removeFromBottom(8.0f);
        readout.setBounds(area.removeFromBottom(40.0f));
        area.removeFromBottom(16.0f);
        knob.setBounds(area.removeFromLeft(110.0f).withHeight(area.getHeight()));
        area.removeFromLeft(24.0f);
        pick.setBounds(area.removeFromRight(90.0f).withHeight(26.0f).translated(
            0.0f, area.getHeight() * 0.5f - 13.0f));
        area.removeFromRight(16.0f);
        slider.setBounds(area.withHeight(40.0f).translated(0.0f, area.getHeight() * 0.5f - 20.0f));
    }

    Knob &knob;
    Slider &slider;
    Readout &readout;
    Button &pick;
    FileInfo &fileInfo;
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

    // Both of these are crossplatform-host-only, and silently absent elsewhere -
    // report them so "the cursor did nothing" is diagnosable at a glance.
    std::fprintf(stderr, "host=%s  cursor=%s  relative-pointer=%s  file-dialog=%s  a11y=%s\n",
                 xpl ? "crossplatform" : "native", session->attrs() ? "yes" : "no",
                 session->pointer() ? "yes" : "no", session->hasFileDialog() ? "yes" : "no",
                 session->a11y() ? "yes" : "no");

    Value knobValue, sliderValue;
    knobValue.label = "knob";
    sliderValue.label = "slider";

    npp::Frame frame{*session, NEUI_W_APPWINDOW, npp::Rect{140, 140, 520, 260},
                     "neuiplusplus demo"};

    auto &panel = frame.add<Panel>(knobValue, sliderValue);
    panel.setBounds(frame.clientBounds().atOrigin());
    panel.resized();

    // Both controls drive the same readout - the whole point of the callback -
    // and each republishes its own accessible value, since a hand-painted
    // control's value lives in client state and neui cannot see it change.
    knobValue.onChange = [&panel] {
        panel.knob.publishValue();
        panel.readout.publishText();
        panel.readout.repaint();
    };
    sliderValue.onChange = [&panel] {
        panel.slider.publishValue();
        panel.readout.publishText();
        panel.readout.repaint();
    };

    panel.pick.onClick = [&panel, &frame, &session] {
        npp::FileDialogOptions opts;
        opts.title = "Pick a wave file";
        opts.filters = {{"Wave files", "*.wav"}, {"All files", "*"}};

        // Modal: this blocks until the user confirms or cancels.
        const auto picked = session->openFile(frame, opts);
        if (!picked.supported)
        {
            panel.fileInfo.showMessage("no file dialog on this host");
            return;
        }
        if (picked.cancelled())
        {
            panel.fileInfo.showMessage("cancelled");
            return;
        }

        std::error_code ec;
        const auto bytes = std::filesystem::file_size(picked.first(), ec);
        if (ec)
            panel.fileInfo.showMessage("could not stat: " + ec.message());
        else
            panel.fileInfo.show(picked.first(), bytes);
    };

    frame.show();
    const bool ok = host->run(session->raw());
    return ok ? 0 : 1;
}
