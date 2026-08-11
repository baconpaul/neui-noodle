/*
 * neui-noodle - a three-component sandbox on top of neui, via neuiplusplus.
 *
 *   HoverBox    : a painted region that goes red while the pointer is inside it
 *   SplitHandle : a three-dot grip that goes yellow on hover and drags the
 *                 boundary between the two panes
 *   SineWave    : a plotted sine whose phase follows a horizontal drag
 *
 * All three are CUSTOMDRAW widgets underneath. What this file no longer
 * contains, because the library does it, is the interesting part: no widget-id
 * -> object table, no flat event callback with a switch over the payload union,
 * no `is_drag_move` helper working around the three hosts' disagreement about
 * `buttonmap` on MOUSE_MOVE, and no painter handle threaded through every draw
 * call. See doc/whatis_neui.md for what the raw C API looks like.
 */

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX // or windows.h's min/max macros eat std::max / std::clamp
#  include <windows.h>
#  include <shellapi.h> // CommandLineToArgvW - see wantsXpl() below
#endif

#include <neuiplusplus/neuiplusplus.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <numbers>
#if defined(_WIN32)
#  include <cwchar> // wcscmp, for the wide-argv workaround in wantsXpl()
#endif

namespace npp = neuiplusplus;

namespace
{

#if defined(__APPLE__)
constexpr const char *k_native_host = "neui.host.macos";
#elif defined(_WIN32)
constexpr const char *k_native_host = "neui.host.win32";
#else
constexpr const char *k_native_host = "neui.host.crossplatform";
#endif
constexpr const char *k_xpl_host = "neui.host.crossplatform";

constexpr float k_two_pi = 2.0f * std::numbers::pi_v<float>;

struct Palette
{
    npp::Color inkDim = npp::Color::fromARGB(0xFF8A94A6);
    npp::Color ink = npp::Color::fromARGB(0xFFFFFFFF);
    npp::Color panel = npp::Color::fromARGB(0xFF262B33);
    npp::Color panelEdge = npp::Color::fromARGB(0xFF3C4350);
    npp::Color backdrop = npp::Color::fromARGB(0xFF1B1F26);
    npp::Color hot = npp::Color::fromARGB(0xFFD1495B);
    npp::Color hotEdge = npp::Color::fromARGB(0xFFF2A0AC);
    npp::Color yellow = npp::Color::fromARGB(0xFFF5C542);
    npp::Color yellowWash = npp::Color::fromARGB(0xFF33301E);
    npp::Color trace = npp::Color::fromARGB(0xFF6FD1B0);
    npp::Color traceHot = npp::Color::fromARGB(0xFFA8F0D8);
    npp::Color zeroLine = npp::Color::fromARGB(0xFF39404D);
    npp::Color rail = npp::Color::fromARGB(0xFF2C323C);
};
constexpr Palette pal{};

// ---------------------------------------------------------------------------
// HoverBox - fills its whole area, red while hovered.

struct HoverBox : npp::Component<HoverBox, npp::Paints, npp::MouseEvents>
{
    using Component::Component;

    void paint(npp::Canvas &g) override
    {
        g.fillAll(hovered ? pal.hot : pal.panel);
        // Inset by half a pixel: a stroke is centred on its path, so a rect
        // drawn flush to the edge loses its outer half.
        g.drawRect(g.bounds().reduced(0.5f), 1.0f, hovered ? pal.hotEdge : pal.panelEdge);
        g.drawText(hovered ? "hot" : "hover me", g.bounds(), 20.0f,
                   hovered ? pal.ink : pal.inkDim, npp::HAlign::centre, npp::VAlign::middle);
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

    bool hovered{false};
};

// ---------------------------------------------------------------------------
// SplitHandle - three dots, yellow while hovered or dragging. Reports drag
// movement as a signed delta; the owner decides what that means.

struct SplitHandle : npp::Component<SplitHandle, npp::Paints, npp::MouseEvents>
{
    explicit SplitHandle(npp::Parent p) : Component(p)
    {
        // Resolved by the crossplatform host only - the native hosts do not read
        // NEUI_ATTR_CURSOR - so this shows up under --xpl and nowhere else.
        setCursor(npp::Cursor::nsResize);
    }

    std::function<void(float)> onDrag;

    void paint(npp::Canvas &g) override
    {
        const bool lit = hovered || dragging;
        const auto b = g.bounds();

        g.fillAll(lit ? pal.yellowWash : pal.backdrop);
        // Hairlines top and bottom so the handle reads as a seam, not a third pane.
        g.fillRect(b.withHeight(1.0f), pal.panelEdge);
        g.fillRect(b.withY(b.getBottom() - 1.0f).withHeight(1.0f), pal.panelEdge);

        const npp::Point c = b.getCentre();
        const float r = dragging ? 4.0f : 3.0f;
        for (int i = -1; i <= 1; ++i)
        {
            const float cx = c.x + float(i) * k_dot_spacing;
            g.fillEllipse({cx - r, c.y - r, 2.0f * r, 2.0f * r}, lit ? pal.yellow : pal.inkDim);
        }
    }

    void mouseEnter(const npp::MouseEvent &) override
    {
        hovered = true;
        repaint();
    }
    void mouseExit(const npp::MouseEvent &) override
    {
        // Not while dragging: the handle slides out from under the pointer at
        // the clamp limits, and going grey mid-drag looks broken.
        hovered = false;
        if (!dragging)
            repaint();
    }
    void mouseDown(const npp::MouseEvent &) override
    {
        dragging = true;
        repaint();
    }
    void mouseDrag(const npp::MouseEvent &e) override
    {
        // dragDelta is measured against the press position in the handle's OWN
        // coordinates, and the handle moves as the split moves - so this is
        // really an absolute "where should the seam be" reading. It stays
        // correct even when the split clamps and the handle stops tracking the
        // pointer.
        if (onDrag)
            onDrag(e.dragDelta().y);
    }
    void mouseUp(const npp::MouseEvent &) override
    {
        dragging = false;
        repaint();
    }

    static constexpr float k_dot_spacing = 30.0f;

    bool hovered{false};
    bool dragging{false};
};

// ---------------------------------------------------------------------------
// SineWave - plots a sine across its width; a horizontal drag shifts phase.

struct SineWave : npp::Component<SineWave, npp::Paints, npp::MouseEvents>
{
    explicit SineWave(npp::Parent p) : Component(p) { setCursor(npp::Cursor::ewResize); }

    void paint(npp::Canvas &g) override
    {
        const auto b = g.bounds();
        const float w = b.getWidth(), h = b.getHeight();
        g.fillAll(pal.backdrop);
        g.drawRect(b.reduced(0.5f), 1.0f, pal.panelEdge);

        const float mid = h * 0.5f;
        const float amp = std::max(4.0f, h * 0.34f);

        // Zero line + amplitude rails.
        g.fillRect({1.0f, mid, w - 2.0f, 1.0f}, pal.zeroLine);
        g.fillRect({1.0f, mid - amp, w - 2.0f, 1.0f}, pal.rail);
        g.fillRect({1.0f, mid + amp, w - 2.0f, 1.0f}, pal.rail);

        // One sample per design unit is plenty at this scale.
        const int steps = std::max(2, int(w));
        g.beginPath();
        for (int i = 0; i <= steps; ++i)
        {
            const float t = float(i) / float(steps);
            const npp::Point pt{t * w, mid - amp * std::sin(t * k_cycles * k_two_pi + phase)};
            if (i == 0)
                g.moveTo(pt);
            else
                g.lineTo(pt);
        }
        g.strokePath(2.0f, dragging ? pal.traceHot : pal.trace);

        char label[64];
        std::snprintf(label, sizeof label, "phase %6.2f rad  (%3.0f deg)  -  drag me",
                      double(phase), double(phase * 180.0f / std::numbers::pi_v<float>));
        g.drawText(label, b.reduced(8.0f), 13.0f, pal.inkDim, npp::HAlign::left,
                   npp::VAlign::bottom);
    }

    void mouseDown(const npp::MouseEvent &) override
    {
        dragging = true;
        phaseAtDown = phase;
        repaint();
    }
    void mouseDrag(const npp::MouseEvent &e) override
    {
        // MINUS, so the wave travels WITH the pointer. The plot is
        // sin(kx + phase) with k positive, and a feature of that curve sits at
        // x = (constant - phase) / k - so RAISING the phase slides the wave
        // LEFT. Dragging right therefore has to lower it.
        //
        // Anchored on the press rather than accumulated per move: phase wraps at
        // 2pi, and summing deltas across a wrap loses the relationship to where
        // the drag started.
        setPhase(phaseAtDown - e.dragDelta().x * k_rad_per_px);
        repaint();
    }
    void mouseUp(const npp::MouseEvent &) override
    {
        dragging = false;
        repaint();
    }

    void setPhase(float p)
    {
        phase = std::fmod(p, k_two_pi);
        if (phase < 0.0f)
            phase += k_two_pi;
    }

    static constexpr float k_cycles = 3.0f;      // wavelengths across the width
    static constexpr float k_rad_per_px = 0.012f; // drag sensitivity

    float phase{0.0f};
    float phaseAtDown{0.0f};
    bool dragging{false};
};

// ---------------------------------------------------------------------------
// Noodle - owns the three components and holds the split. neui ships no layout
// engine, so resized() is the whole "layout system"; it is also the only place
// that moves a widget.

struct Noodle : npp::Component<Noodle, npp::Paints, npp::Resizes>
{
    explicit Noodle(npp::Parent p)
        : Component(p), box(add<HoverBox>()), handle(add<SplitHandle>()), wave(add<SineWave>())
    {
        handle.onDrag = [this](float dy) {
            split += dy;
            resized();
        };
    }

    void paint(npp::Canvas &g) override { g.fillAll(pal.backdrop); }

    void resized() override
    {
        auto area = localBounds().reduced(k_margin);
        const float panes = std::max(2.0f * k_min_pane, area.getHeight() - k_handle_h);

        if (split <= 0.0f) // first layout: a third to the hover box
            split = panes / 3.0f;
        split = std::clamp(split, k_min_pane, panes - k_min_pane);

        box.setBounds(area.removeFromTop(split));
        handle.setBounds(area.removeFromTop(k_handle_h));
        wave.setBounds(area);

        box.repaint();
        handle.repaint();
        wave.repaint();
    }

    static constexpr float k_margin = 16.0f;
    static constexpr float k_handle_h = 22.0f;
    static constexpr float k_min_pane = 48.0f;

    HoverBox &box;
    SplitHandle &handle;
    SineWave &wave;
    float split{0.0f}; // the top pane's height; the handle sits directly below
};

// Is --xpl on the command line?
//
// On Windows this deliberately ignores argv. The win32 host owns WinMain
// (hosts/win32/window.cpp) and calls main() with the WIDE argv from
// CommandLineToArgvW, against a local `int main(int, wchar_t**)` declaration -
// so what actually arrives in a standard `char**` parameter is an array of
// wchar_t*. Reading the command line ourselves is the honest way to get at it.
// (neui's own examples sidestep this by taking argv and immediately voiding it.)
#if defined(_WIN32)
bool wantsXpl(int /*argc*/, char ** /*argv*/)
{
    int wargc = 0;
    wchar_t **wargv = ::CommandLineToArgvW(::GetCommandLineW(), &wargc);
    if (!wargv)
        return false;
    bool found = false;
    for (int i = 1; i < wargc && !found; ++i)
        found = (std::wcscmp(wargv[i], L"--xpl") == 0);
    ::LocalFree(wargv);
    return found;
}
#else
bool wantsXpl(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--xpl") == 0)
            return true;
    return false;
}
#endif

} // namespace

int main(int argc, char *argv[])
{
    // One call registers every host statically linked into this binary; then we
    // pick one by id. This sandbox defaults to the NATIVE host - exercising it
    // is the point - which is the opposite of what a plugin UI should do.
    neui_init();
    const char *hostId = wantsXpl(argc, argv) ? k_xpl_host : k_native_host;
    neui_api_t *host = neui_get_api(hostId);
    if (!host)
    {
        hostId = "(first registered)";
        host = neui_get_api(nullptr);
    }
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
    // Not read off the session: every host implements NEUI_API_ATTRS, but only
    // the crossplatform one RESOLVES NEUI_ATTR_CURSOR, and nothing distinguishes
    // those two facts at runtime. On Linux the two ids are the same host.
    std::fprintf(stderr, "host=%s  cursors=%s\n", hostId,
                 std::strcmp(hostId, k_xpl_host) == 0 ? "yes" : "no (crossplatform host only)");

    // For a top-level frame, width/height is the CLIENT area at 96 DPI - the
    // host grows the outer window for title bar and borders itself.
    npp::Frame frame{*session, NEUI_W_APPWINDOW, npp::Rect{140, 140, 600, 400},
                     "neui noodle - hover, drag, split"};

    auto &noodle = frame.add<Noodle>();
    // setBounds fires resized(), which lays the three panes out.
    noodle.setBounds(frame.clientBounds().atOrigin());

    // RESIZE is delivered to the FRAME and nowhere else, so this is the only
    // hook there is. The native hosts fire it; the crossplatform host does not
    // on every platform, so under --xpl the panes may keep their size when the
    // window grows.
    frame.onResize = [&noodle](npp::Rect client) { noodle.setBounds(client.atOrigin()); };

    frame.show();
    return host->run(session->raw()) ? 0 : 1;
}
