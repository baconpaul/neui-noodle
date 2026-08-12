/*
 * menudemo - can neui carry the menu an audio-plugin UI actually needs?
 *
 * The requirement, from sst-jucegui's MenuButton and two-filters' editor: a menu
 * that is NESTED, has CHECKMARKS, has DISABLED entries, has a BOLD HEADER, and
 * contains a FOCUSABLE TYPE-IN. Both buttons below open a menu with the same
 * logical content, so the two approaches can be compared side by side:
 *
 *   [Native menu]  NEUI_W_POPUPMENU + widgets->popup_tree_menu.
 *                  Nesting, checkmarks and enable/disable: yes, free, native
 *                  rendering, native dismissal and keyboard handling.
 *                  Bold header: NO - the tree model carries text, not style.
 *                  Type-in: NO - rows are model entries, not widgets.
 *
 *   [Custom menu]  npp::PopupMenu, client-drawn CUSTOMDRAW panels.
 *                  All five, including a real NEUI_W_INPUTBOX in a row, which
 *                  brings IME / clipboard / undo / selection with it because it
 *                  is neui's own widget rather than a hand-rolled text field.
 *
 * The readout under the buttons reports what came back, so a pick, a toggle and
 * a committed type-in are all visible without a debugger.
 */

#include <neuiplusplus/components/NativeWidget.h>
#include <neuiplusplus/components/PopupMenu.h>
#include <neuiplusplus/neuiplusplus.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace npp = neuiplusplus;

namespace
{

struct Palette
{
    npp::Color window = npp::Color::rgb(0x1B, 0x1F, 0x26);
    npp::Color panel = npp::Color::rgb(0x26, 0x2B, 0x33);
    npp::Color edge = npp::Color::rgb(0x3C, 0x43, 0x50);
    npp::Color ink = npp::Color::rgb(0xE6, 0xEA, 0xF0);
    npp::Color inkDim = npp::Color::rgb(0x8A, 0x94, 0xA6);
    npp::Color accent = npp::Color::rgb(0x6F, 0xD1, 0xB0);
};
constexpr Palette pal{};

// ---------------------------------------------------------------------------
// The state the menus read and write, so a checkmark means something.

struct FilterState
{
    int type{1};              // index into k_types
    bool keyTrack{true};      //
    bool analogMode{false};   //
    bool oversample{false};   // deliberately left disabled in the menu
    std::string cutoff{"1200.0"};

    std::function<void()> onChange;
    void changed()
    {
        if (onChange)
            onChange();
    }
};

const char *const k_types[] = {"Lowpass 12", "Lowpass 24", "Bandpass", "Highpass 24"};
constexpr int k_typeCount = 4;

// ---------------------------------------------------------------------------
// A push button. Reports its own bottom-left in FRAME coordinates so a menu can
// be anchored under it - the menu's panels are children of the frame, not of
// the button, so the button's own local space is the wrong space to answer in.

struct Button : npp::Component<Button, npp::Paints, npp::MouseEvents, npp::FocusEvents>
{
    Button(npp::Parent p, std::string t) : Component(p), text(std::move(t))
    {
        setCursor(npp::Cursor::hand);
        setAccessibleRole(npp::Role::button);
        setAccessibleName(text.c_str());
    }

    std::string text;
    std::function<void()> onClick;
    bool hovered{false};

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
    void mouseUp(const npp::MouseEvent &) override
    {
        if (onClick)
            onClick();
    }

    void paint(npp::Canvas &g) override
    {
        const auto b = g.bounds();
        g.fillRoundRect(b, 4.0f, hovered ? pal.edge : pal.panel);
        g.drawRoundRect(b, 4.0f, 1.0f, g.hasFocus() ? pal.accent : pal.edge);
        g.drawText(text, b, npp::Font{"", 13.0f}, pal.ink, npp::HAlign::centre,
                   npp::VAlign::middle);
    }
};

// ---------------------------------------------------------------------------
// Multi-line readout.

struct Readout : npp::Component<Readout, npp::Paints>
{
    explicit Readout(npp::Parent p) : Component(p)
    {
        setAccessibleRole(npp::Role::staticText);
    }

    std::vector<std::string> lines;

    void set(std::vector<std::string> l)
    {
        lines = std::move(l);
        std::string joined;
        for (const auto &s : lines)
            joined += s + ". ";
        setAccessibleName(joined.c_str());
        notifyAccessible(npp::A11yChange::name);
        repaint();
    }

    void paint(npp::Canvas &g) override
    {
        auto b = g.bounds();
        g.fillRoundRect(b, 4.0f, pal.panel);
        auto inner = b.reduced(10.0f, 8.0f);
        for (const auto &s : lines)
            g.drawText(s, inner.removeFromTop(18.0f), npp::Font{"", 12.0f}, pal.inkDim,
                       npp::HAlign::left, npp::VAlign::middle);
    }
};

// ---------------------------------------------------------------------------
// The window content.

struct Panel : npp::Component<Panel, npp::Paints, npp::Resizes>
{
    Panel(npp::Parent p, FilterState &s) : Component(p), state(s)
    {
        setAccessibleRole(npp::Role::group);
        setAccessibleName("Menu demo");
    }

    FilterState &state;
    Button &nativeBtn = add<Button>("Native menu");
    Button &customBtn = add<Button>("Custom menu");
    Readout &readout = add<Readout>();

    void resized() override
    {
        auto b = localBounds().reduced(14.0f);
        auto top = b.removeFromTop(30.0f);
        nativeBtn.setBounds(top.removeFromLeft(140.0f));
        top.removeFromLeft(10.0f);
        customBtn.setBounds(top.removeFromLeft(140.0f));
        b.removeFromTop(14.0f);
        readout.setBounds(b.removeFromTop(96.0f));
    }

    void paint(npp::Canvas &g) override { g.fillRect(g.bounds(), pal.window); }

    /** @brief Bottom-left of a child button, in FRAME coordinates. */
    npp::Point anchorUnder(const Button &b) const
    {
        return {bounds().getX() + b.bounds().getX(), bounds().getY() + b.bounds().getBottom() + 2.0f};
    }

    void refresh()
    {
        readout.set({std::string("type: ") + k_types[state.type],
                     std::string("key track: ") + (state.keyTrack ? "on" : "off") +
                         "   analog: " + (state.analogMode ? "on" : "off"),
                     std::string("oversample: ") + (state.oversample ? "on" : "off") +
                         "  (menu row is disabled)",
                     std::string("cutoff: ") + state.cutoff + " Hz"});
    }
};

// ---------------------------------------------------------------------------
// The custom menu's content. Built fresh per click so the checkmarks reflect
// current state - the same thing sst-jucegui's DiscreteParamMenuBuilder does.

std::vector<npp::MenuItem> buildCustomMenu(FilterState &state, Panel &panel)
{
    std::vector<npp::MenuItem> types;
    for (int i = 0; i < k_typeCount; ++i)
        types.push_back(npp::MenuItem::makeToggle(k_types[i], state.type == i, [&state, &panel, i] {
            state.type = i;
            panel.refresh();
        }));

    return {
        // A bold section header. THIS is the thing the native menu cannot do.
        npp::MenuItem::makeHeader("Filter"),
        npp::MenuItem::makeSubmenu("Type", std::move(types)),
        npp::MenuItem::makeSeparator(),

        npp::MenuItem::makeToggle("Key Track", state.keyTrack,
                                  [&state, &panel] {
                                      state.keyTrack = !state.keyTrack;
                                      panel.refresh();
                                  }),
        npp::MenuItem::makeToggle("Analog Mode", state.analogMode,
                                  [&state, &panel] {
                                      state.analogMode = !state.analogMode;
                                      panel.refresh();
                                  }),
        // Disabled, and stays disabled: no hover highlight, no activation.
        npp::MenuItem::makeToggle("Oversample", state.oversample, {}, /*enabled*/ false),

        npp::MenuItem::makeSeparator(),
        npp::MenuItem::makeHeader("Cutoff"),
        // A real NEUI_W_INPUTBOX living in a menu row. Type and press Enter to
        // commit and close; Escape reverts and closes; clicking away commits.
        npp::MenuItem::makeTypeIn("Hz", state.cutoff,
                                  [&state, &panel](const std::string &v) {
                                      if (v.empty() || v == state.cutoff)
                                          return;
                                      state.cutoff = v;
                                      panel.refresh();
                                  }),

        npp::MenuItem::makeSeparator(),
        npp::MenuItem::makeSubmenu("Presets",
                                   {npp::MenuItem::makeHeader("Factory"),
                                    npp::MenuItem::makeEntry("Init", [&state, &panel] {
                                        state.type = 0;
                                        state.cutoff = "1000.0";
                                        panel.refresh();
                                    }),
                                    npp::MenuItem::makeEntry("Squelch", [&state, &panel] {
                                        state.type = 2;
                                        state.cutoff = "320.0";
                                        panel.refresh();
                                    }),
                                    npp::MenuItem::makeSeparator(),
                                    npp::MenuItem::makeSubmenu(
                                        "User", {npp::MenuItem::makeEntry("(none saved)", {}, false)})}),
    };
}

// ---------------------------------------------------------------------------
// The native menu, for comparison. Rebuilt per click for the same reason.
//
// Returns the item ids so ITEM_SELECTED can be decoded - the pick arrives
// asynchronously as one event carrying the neui_item_t that tree->add gave back.

struct NativeMenuIds
{
    neui_item_t types[k_typeCount]{};
    neui_item_t keyTrack{}, analogMode{}, oversample{}, cutoffNote{};
};

NativeMenuIds buildNativeMenu(npp::Session &s, neui_widget_t menu, neui_tree_api_t *tree,
                              const FilterState &state)
{
    NativeMenuIds ids;
    tree->clear(s.raw(), menu);

    // No bold header available, so the section title has to be an ordinary
    // disabled row - which is exactly the gap this demo is measuring.
    auto title = tree->add(s.raw(), menu, tree_item_root, "Filter", nullptr);
    tree->set_enabled(s.raw(), menu, title, false);

    auto typeSub = tree->add(s.raw(), menu, tree_item_root, "Type", nullptr);
    for (int i = 0; i < k_typeCount; ++i)
    {
        ids.types[i] = tree->add(s.raw(), menu, typeSub, k_types[i], nullptr);
        tree->set_checked(s.raw(), menu, ids.types[i], state.type == i);
    }

    tree->add(s.raw(), menu, tree_item_root, "-", nullptr);

    ids.keyTrack = tree->add(s.raw(), menu, tree_item_root, "Key Track", nullptr);
    tree->set_checked(s.raw(), menu, ids.keyTrack, state.keyTrack);

    ids.analogMode = tree->add(s.raw(), menu, tree_item_root, "Analog Mode", nullptr);
    tree->set_checked(s.raw(), menu, ids.analogMode, state.analogMode);

    ids.oversample = tree->add(s.raw(), menu, tree_item_root, "Oversample", nullptr);
    tree->set_checked(s.raw(), menu, ids.oversample, state.oversample);
    tree->set_enabled(s.raw(), menu, ids.oversample, false);

    tree->add(s.raw(), menu, tree_item_root, "-", nullptr);

    // And no widget can live in a row, so the type-in degrades to a label.
    ids.cutoffNote =
        tree->add(s.raw(), menu, tree_item_root, "Cutoff: (no type-in here)", nullptr);
    tree->set_enabled(s.raw(), menu, ids.cutoffNote, false);

    return ids;
}

} // namespace

int main(int argc, char *argv[])
{
    bool xpl = true; // popup_tree_menu is crossplatform-host only
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

    auto *tree = static_cast<neui_tree_api_t *>(host->get_interface(session->raw(), NEUI_API_TREE));
    const bool hasTreePopup = tree && session->widgets()->popup_tree_menu;
    std::fprintf(stderr, "host=%s  tree-api=%s  popup_tree_menu=%s  a11y=%s\n",
                 xpl ? "crossplatform" : "native", tree ? "yes" : "no",
                 session->widgets()->popup_tree_menu ? "yes" : "no",
                 session->a11y() ? "yes" : "no");

    FilterState state;

    npp::Frame frame{*session, NEUI_W_APPWINDOW, npp::Rect{160, 160, 560, 260},
                     "neui menu demo"};

    auto &panel = frame.add<Panel>(state);
    panel.setBounds(frame.clientBounds().atOrigin());
    frame.onResize = [&panel](npp::Rect client) { panel.setBounds(client.atOrigin()); };
    state.onChange = [&panel] { panel.refresh(); };
    panel.refresh();

    // --- the custom menu ----------------------------------------------------
    // Hung off the FRAME, not the button: submenus paint outside the parent
    // panel's bounds, and neui clips a child to its parent.
    npp::MenuStyle style;
    npp::PopupMenu menu{frame, style};

    panel.customBtn.onClick = [&] {
        menu.show(buildCustomMenu(state, panel), panel.anchorUnder(panel.customBtn));
    };

    // --- the native menu ----------------------------------------------------
    neui_widget_t nativeMenu = widget_none;
    NativeMenuIds ids;

    if (hasTreePopup)
    {
        nativeMenu = session->widgets()->create(session->raw(), frame.widget(), NEUI_W_POPUPMENU, 0,
                                                0, 0, 0, nullptr);

        // ITEM_SELECTED has no component-level shape in neuiplusplus, so it
        // comes through the raw escape hatch.
        session->onRawEvent = [&](neui_event_t *ev) -> bool {
            if (ev->type != NEUI_EVENT_ITEM_SELECTED)
                return false;
            if (ev->data.item.widget.id != nativeMenu.id)
                return false;

            const std::uint32_t picked = ev->data.item.index;
            for (int i = 0; i < k_typeCount; ++i)
                if (picked == ids.types[i].id)
                    state.type = i;
            if (picked == ids.keyTrack.id)
                state.keyTrack = !state.keyTrack;
            if (picked == ids.analogMode.id)
                state.analogMode = !state.analogMode;
            panel.refresh();
            return true;
        };

        panel.nativeBtn.onClick = [&] {
            ids = buildNativeMenu(*session, nativeMenu, tree, state);
            // (x, y) are anchor-local, so 0/height puts it under the button.
            session->widgets()->popup_tree_menu(session->raw(), panel.nativeBtn.widget(), 0,
                                                int(panel.nativeBtn.bounds().getHeight()),
                                                nativeMenu);
        };
    }
    else
    {
        panel.nativeBtn.onClick = [&panel] {
            panel.readout.set({"popup_tree_menu is not available on this host.",
                               "Run without --native (crossplatform host only)."});
        };
    }

    frame.show();
    const bool ok = host->run(session->raw());
    return ok ? 0 : 1;
}
