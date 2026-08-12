/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_COMPONENTS_POPUPMENU_H
#define NEUIPLUSPLUS_COMPONENTS_POPUPMENU_H

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <neui/d/keys.h>

#include "../Component.h"
#include "../draw/Canvas.h"
#include "../draw/Color.h"
#include "../draw/Font.h"
#include "NativeWidget.h"

/**
 * @file
 * @brief @ref neuiplusplus::PopupMenu - a client-drawn menu with a live text field.
 *
 * WHY THIS EXISTS. neui already has a rich context menu:
 * `NEUI_W_POPUPMENU` + `widgets->popup_tree_menu`, described with the same
 * `NEUI_API_TREE` calls as a menu bar. It gives you submenus to any depth,
 * per-item enable/disable, checkmarks, shortcut labels and command routing, all
 * rendered by the platform. **Use it** whenever it is enough - it is native, it
 * is free, and it dismisses and keyboard-navigates the way the OS does.
 *
 * Two things it cannot do, both of which an audio-plugin menu wants:
 *
 * 1. **Per-item typography.** The tree model carries text, not style, so there
 *    is no bold section header.
 * 2. **A widget inside a row.** The rows are model entries, not widgets, so
 *    there is nowhere to put a focusable type-in - the pattern sst-jucegui gets
 *    from `juce::PopupMenu::CustomComponent`.
 *
 * So this is the client-drawn alternative, and it is only worth its cost when
 * one of those two is what you need.
 *
 * WHAT IT IS. A stack of `CUSTOMDRAW` panels plus a full-frame scrim, all
 * children of the FRAME rather than of each other - a submenu has to paint
 * outside its parent's bounds, and neui clips a child to its parent. The panels
 * are created once and reused, because `ComponentCore` has no remove-a-child.
 *
 * KNOWN LIMITS, this being a spike:
 * - Panel width is fixed (@ref MenuStyle::width) rather than measured. Text
 *   measurement needs a live `Canvas`, which only exists inside `paint`.
 * - It does not escape the frame. A native menu can extend past the window;
 *   this cannot. For a plugin editor that is usually the behaviour you want
 *   anyway, but it is a real difference.
 * - Dismissal is scrim-click, Escape or a pick. NOT focus-loss, deliberately:
 *   the type-in takes focus away from the panel the moment you click it, so
 *   dismiss-on-blur would make the field unusable. A real menu also closes when
 *   the window deactivates; that needs a signal this does not have.
 *
 * In the type-in, Enter commits and closes and Escape reverts and closes. Both
 * work by claiming just those two keys and letting every other keystroke fall
 * through to neui - see the warning on @ref NativeWidget::onKeyPressed.
 */

namespace neuiplusplus
{

/** @brief One row. Build with the named constructors rather than by hand. */
struct MenuItem
{
    enum class Kind
    {
        entry,
        header, ///< a bold, non-interactive section title
        separator,
        submenu,
        typeIn ///< a label plus a real focusable NEUI_W_INPUTBOX
    };

    Kind kind{Kind::entry};
    std::string text;
    bool checked{false};
    bool enabled{true};
    std::function<void()> action;
    std::vector<MenuItem> children;                    ///< Kind::submenu
    std::function<void(const std::string &)> onCommit; ///< Kind::typeIn
    std::string value;                                 ///< Kind::typeIn initial text

    static MenuItem makeHeader(std::string t)
    {
        MenuItem m;
        m.kind = Kind::header;
        m.text = std::move(t);
        m.enabled = false;
        return m;
    }
    static MenuItem makeSeparator()
    {
        MenuItem m;
        m.kind = Kind::separator;
        m.enabled = false;
        return m;
    }
    static MenuItem makeEntry(std::string t, std::function<void()> a = {}, bool enabled = true)
    {
        MenuItem m;
        m.text = std::move(t);
        m.action = std::move(a);
        m.enabled = enabled;
        return m;
    }
    static MenuItem makeToggle(std::string t, bool checked, std::function<void()> a = {},
                               bool enabled = true)
    {
        MenuItem m = makeEntry(std::move(t), std::move(a), enabled);
        m.checked = checked;
        return m;
    }
    static MenuItem makeSubmenu(std::string t, std::vector<MenuItem> kids, bool enabled = true)
    {
        MenuItem m;
        m.kind = Kind::submenu;
        m.text = std::move(t);
        m.children = std::move(kids);
        m.enabled = enabled;
        return m;
    }
    static MenuItem makeTypeIn(std::string label, std::string initial,
                               std::function<void(const std::string &)> commit)
    {
        MenuItem m;
        m.kind = Kind::typeIn;
        m.text = std::move(label);
        m.value = std::move(initial);
        m.onCommit = std::move(commit);
        return m;
    }
};

/** @brief Colours, fonts and metrics. Everything the menu draws reads from here. */
struct MenuStyle
{
    Color background{Color::rgb(0x2A, 0x2A, 0x30)};
    Color border{Color::rgb(0x50, 0x50, 0x5A)};
    Color text{Color::rgb(0xDE, 0xDE, 0xE4)};
    Color textDisabled{Color::rgb(0x70, 0x70, 0x7A)};
    Color headerText{Color::rgb(0x9A, 0xC8, 0xFF)};
    Color highlight{Color::rgb(0x3E, 0x5C, 0x86)};
    Color separator{Color::rgb(0x45, 0x45, 0x4E)};
    Color scrim{Color::rgba(0, 0, 0, 40)};

    Font font{"", 13.0f};
    Font headerFont{"", 12.0f, FontWeight::bold};

    float width{236.0f};
    float rowHeight{23.0f};
    float headerHeight{25.0f};
    float separatorHeight{9.0f};
    float typeInHeight{30.0f};
    float gutter{22.0f}; ///< the checkmark column
    float padding{4.0f};
    float shadowInset{0.0f};
};

/**
 * @brief One level of the menu. Owned and driven by @ref PopupMenu.
 *
 * Holds a NON-OWNING pointer to its item vector: the controller owns the whole
 * tree for as long as the menu is up, and does not mutate it, so submenu levels
 * can point straight into `parent[i].children`.
 */
class MenuPanel : public Component<MenuPanel, interfaces::Paints, interfaces::MouseEvents,
                                   interfaces::KeyboardEvents, interfaces::FocusEvents>
{
  public:
    explicit MenuPanel(Parent p) : Component(p)
    {
        setVisible(false);
        // A menu is a group of choices; without this the panel reaches an AT as
        // an anonymous custom-draw box. NOT Role::none - that would delete the
        // type-in child from the tree along with it.
        setAccessibleRole(Role::group);
        setAccessibleName("Menu");
    }

    /// @name Driven by the controller
    /// @{
    std::function<void(int row)> onHoverRow;  ///< -1 when nothing is hovered
    std::function<void(int row)> onActivate;  ///< a click or Enter on an enabled row
    std::function<void()> onDismissRequested; ///< Escape
    /// @}

    void setStyle(const MenuStyle *s) { style_ = s; }

    /** @brief Point at a level of the item tree and lay out for it. */
    void setItems(const std::vector<MenuItem> *items)
    {
        items_ = items;
        hover_ = -1;
        layOutTypeIn();
    }

    const std::vector<MenuItem> *items() const { return items_; }

    /** @brief Total height of the rows, for the controller's positioning. */
    float preferredHeight() const
    {
        float h = style_->padding * 2.0f;
        if (items_)
            for (const auto &it : *items_)
                h += rowHeightFor(it);
        return h;
    }

    /**
     * @brief Commit the type-in, if this panel has one.
     *
     * IDEMPOTENT, which it has to be: the field commits on Enter, again when
     * focus leaves it, and again when the controller tears the menu down - three
     * paths that all fire for one Enter press. Only a value that differs from the
     * last committed one reaches the client.
     */
    void commitTypeIn()
    {
        if (!field_ || typeInRow_ < 0 || !items_)
            return;
        auto text = field_->text();
        if (text == lastCommitted_)
            return;
        lastCommitted_ = text;
        const auto &it = (*items_)[std::size_t(typeInRow_)];
        if (it.onCommit)
            it.onCommit(std::move(text));
    }

    /** @brief Discard whatever was typed - Escape. Leaves nothing to commit. */
    void revertTypeIn()
    {
        if (field_ && typeInRow_ >= 0)
            field_->setText(lastCommitted_);
    }

    /** @brief Move focus to the type-in if there is one, else to the panel. */
    void focusInitial()
    {
        if (field_ && typeInRow_ >= 0)
            field_->takeFocus();
        else
            takeFocus();
    }

    void setHoverRow(int r)
    {
        if (r == hover_)
            return;
        hover_ = r;
        repaint();
    }
    int hoverRow() const { return hover_; }

    /** @brief The row rect in PANEL-local design units. */
    Rect rowRect(int index) const
    {
        Rect r{style_->padding, style_->padding, style_->width - style_->padding * 2.0f, 0.0f};
        if (!items_)
            return r;
        for (int i = 0; i < int(items_->size()); ++i)
        {
            const float h = rowHeightFor((*items_)[std::size_t(i)]);
            if (i == index)
                return r.withHeight(h);
            r = r.translated(0.0f, h);
        }
        return r.withHeight(0.0f);
    }

    // ---- capability handlers ------------------------------------------------

    void paint(Canvas &g) override
    {
        const auto &s = *style_;
        g.fillRect(g.bounds(), s.background);
        g.drawRect(g.bounds(), 1.0f, s.border);
        if (!items_)
            return;

        for (int i = 0; i < int(items_->size()); ++i)
        {
            const auto &it = (*items_)[std::size_t(i)];
            const Rect r = rowRect(i);

            switch (it.kind)
            {
            case MenuItem::Kind::separator:
            {
                const float y = r.getCentreY();
                g.drawLine({r.getX() + 6.0f, y}, {r.getRight() - 6.0f, y}, 1.0f, s.separator);
                break;
            }
            case MenuItem::Kind::header:
                // The whole reason this component exists: a per-row font.
                g.drawText(it.text, r.withTrimmedLeft(8.0f), s.headerFont, s.headerText,
                           HAlign::left, VAlign::middle);
                break;

            case MenuItem::Kind::typeIn:
                g.drawText(it.text, r.withTrimmedLeft(8.0f).withWidth(labelWidth_), s.font, s.text,
                           HAlign::left, VAlign::middle);
                // The field itself is a real NEUI_W_INPUTBOX child; neui paints it.
                break;

            case MenuItem::Kind::entry:
            case MenuItem::Kind::submenu:
            {
                const bool on = (i == hover_) && it.enabled;
                if (on)
                    g.fillRect(r, s.highlight);

                const Color fg = it.enabled ? s.text : s.textDisabled;

                if (it.checked)
                    drawCheck(g, r.withWidth(s.gutter), fg);

                g.drawText(it.text, r.withTrimmedLeft(s.gutter), s.font, fg, HAlign::left,
                           VAlign::middle);

                if (it.kind == MenuItem::Kind::submenu)
                    drawArrow(g, r.withTrimmedRight(6.0f), fg);
                break;
            }
            }
        }
    }

    void mouseMove(const MouseEvent &e) override { updateHover(e.position); }
    void mouseDrag(const MouseEvent &e) override { updateHover(e.position); }

    void mouseUp(const MouseEvent &e) override
    {
        const int r = rowAt(e.position);
        if (r < 0 || !items_)
            return;
        const auto &it = (*items_)[std::size_t(r)];
        if (!it.enabled || it.kind == MenuItem::Kind::separator ||
            it.kind == MenuItem::Kind::header || it.kind == MenuItem::Kind::typeIn)
            return;
        if (onActivate)
            onActivate(r);
    }

    bool keyPressed(const KeyEvent &e) override
    {
        if (e.keyCode == NEUI_KEY_ESCAPE)
        {
            if (onDismissRequested)
                onDismissRequested();
            return true;
        }
        if (e.keyCode == NEUI_KEY_UP || e.keyCode == NEUI_KEY_DOWN)
        {
            step(e.keyCode == NEUI_KEY_DOWN ? 1 : -1);
            return true;
        }
        if (e.keyCode == NEUI_KEY_RETURN && hover_ >= 0)
        {
            if (onActivate)
                onActivate(hover_);
            return true;
        }
        return false;
    }

  private:
    float rowHeightFor(const MenuItem &it) const
    {
        switch (it.kind)
        {
        case MenuItem::Kind::separator:
            return style_->separatorHeight;
        case MenuItem::Kind::header:
            return style_->headerHeight;
        case MenuItem::Kind::typeIn:
            return style_->typeInHeight;
        default:
            return style_->rowHeight;
        }
    }

    int rowAt(Point p) const
    {
        if (!items_)
            return -1;
        for (int i = 0; i < int(items_->size()); ++i)
            if (rowRect(i).contains(p))
                return i;
        return -1;
    }

    void updateHover(Point p)
    {
        const int r = rowAt(p);
        // Every mouse move lands here; only a CHANGE of row is news. Reporting
        // unconditionally makes the controller rebuild an already-open submenu
        // on every motion event.
        if (r == hover_)
            return;
        setHoverRow(r);
        if (onHoverRow)
            onHoverRow(r);
    }

    /** @brief Keyboard traversal, skipping everything unselectable. */
    void step(int dir)
    {
        if (!items_ || items_->empty())
            return;
        const int n = int(items_->size());
        int i = hover_;
        for (int k = 0; k < n; ++k)
        {
            i = (i + dir + n) % n;
            const auto &it = (*items_)[std::size_t(i)];
            if (it.enabled && it.kind != MenuItem::Kind::separator &&
                it.kind != MenuItem::Kind::header)
            {
                setHoverRow(i);
                if (onHoverRow)
                    onHoverRow(i);
                return;
            }
        }
    }

    /**
     * @brief Place the real INPUTBOX over the type-in row.
     *
     * Created once and reused - `add<T>` has no counterpart that removes, so a
     * per-show field would grow the child list forever.
     */
    void layOutTypeIn()
    {
        typeInRow_ = -1;
        if (items_)
            for (int i = 0; i < int(items_->size()); ++i)
                if ((*items_)[std::size_t(i)].kind == MenuItem::Kind::typeIn)
                {
                    typeInRow_ = i;
                    break;
                }

        if (typeInRow_ < 0)
        {
            if (field_)
                field_->setVisible(false);
            return;
        }

        if (!field_)
        {
            field_ = &add<NativeWidget>(NEUI_W_INPUTBOX);

            // Blur commits, so tabbing or clicking away keeps what was typed.
            field_->onFocusLost = [this]() { commitTypeIn(); };

            // FIRST REFUSAL, not interception: every key this returns false for
            // goes on to neui's INPUTBOX handling, which is what keeps the field
            // typable. Only Enter and Escape are claimed.
            field_->onKeyPressed = [this](const KeyEvent &e) {
                if (e.keyCode == NEUI_KEY_RETURN)
                {
                    commitTypeIn();
                    if (onDismissRequested)
                        onDismissRequested();
                    return true;
                }
                if (e.keyCode == NEUI_KEY_ESCAPE)
                {
                    revertTypeIn();
                    if (onDismissRequested)
                        onDismissRequested();
                    return true;
                }
                return false;
            };
        }

        const auto &it = (*items_)[std::size_t(typeInRow_)];
        const Rect row = rowRect(typeInRow_);
        lastCommitted_ = it.value;
        field_->setText(it.value);
        field_->setBounds(row.withTrimmedLeft(8.0f + labelWidth_).reduced(2.0f, 4.0f));
        field_->setVisible(true);
    }

    static void drawCheck(Canvas &g, Rect r, Color c)
    {
        const float cx = r.getCentreX(), cy = r.getCentreY();
        g.beginPath();
        g.moveTo({cx - 4.0f, cy});
        g.lineTo({cx - 1.0f, cy + 3.5f});
        g.lineTo({cx + 4.5f, cy - 4.0f});
        g.strokePath(1.8f, c);
    }

    static void drawArrow(Canvas &g, Rect r, Color c)
    {
        const float x = r.getRight() - 5.0f, cy = r.getCentreY();
        g.beginPath();
        g.moveTo({x - 3.0f, cy - 4.0f});
        g.lineTo({x + 1.0f, cy});
        g.lineTo({x - 3.0f, cy + 4.0f});
        g.strokePath(1.5f, c);
    }

    static inline MenuStyle defaultStyle_{};

    const MenuStyle *style_{&defaultStyle_};
    const std::vector<MenuItem> *items_{nullptr};
    NativeWidget *field_{nullptr};
    int typeInRow_{-1};
    int hover_{-1};
    float labelWidth_{62.0f};
    std::string lastCommitted_;
};

/**
 * @brief Where a menu's panels are allowed to live.
 *
 * THE WHOLE REASON THIS IS A CHOICE. On neui's crossplatform host every menu
 * surface is painted into the frame: `Session::open_popup_menu` converts its
 * anchor to frame-local absolute coordinates and paints through
 * `paint_popup_menu`, and `show_tree_popup` paints through `paint_tree_popup`,
 * both from inside `paint_frame`. So neither of neui's own menus can leave the
 * window, and neither can @ref inFrame below.
 *
 * That is a real regression against JUCE, whose `PopupMenu` puts itself on the
 * desktop as a top-level window - which is how Surge's FX menu manages to be a
 * three-column grid larger than the entire editor. (neui's NATIVE macOS host
 * does escape, via a real `NSMenu` in `run_popup_menu_macos`, but the native
 * hosts leave `popup_tree_menu` null and implement none of EMBED / A11Y / TIMER
 * / POINTER, so a plugin cannot use them.)
 *
 * @ref desktop is the client-side workaround: one borderless `NEUI_W_PLUGWINDOW`
 * per menu level, positioned in screen coordinates. It should be retired the
 * moment the host can host a popup surface itself.
 */
enum class MenuPlacement
{
    /** Panels are children of the host frame. Cannot leave it; clamped to fit. */
    inFrame,
    /** Each level gets its own borderless top-level window. Escapes the frame. */
    desktop
};

/**
 * @brief The controller: owns the item tree, the scrim and the panel stack.
 *
 * Not a component itself. It hangs its panels off a host component - pass the
 * FRAME, so an @ref MenuPlacement::inFrame submenu can paint anywhere in the
 * window.
 *
 * @code
 * npp::PopupMenu menu{frame, style, npp::MenuPlacement::desktop};
 * menu.show({ npp::MenuItem::makeHeader("Filter Type"),
 *             npp::MenuItem::makeToggle("Lowpass", true, []{ ... }),
 *             npp::MenuItem::makeSubmenu("More", { ... }) },
 *           {x, y});
 * @endcode
 */
class PopupMenu
{
  public:
    /** @param host the frame. @param style must outlive this. */
    PopupMenu(ComponentCore &host, const MenuStyle &style,
              MenuPlacement placement = MenuPlacement::inFrame)
        : host_(&host), style_(&style), placement_(placement)
    {
        // Scrim first so it paints UNDER every in-frame panel: neui paints
        // children in creation order. It catches the click-outside that
        // dismisses - and in desktop mode it is the ONLY such catcher, since a
        // click on another application never reaches us at all.
        scrim_ = &host.add<Scrim>();
        scrim_->style = style_;
        scrim_->onClick = [this]() { dismiss(); };
    }

    /** @brief Open at @p at, in HOST-local design units, whatever the placement. */
    void show(std::vector<MenuItem> items, Point at)
    {
        items_ = std::move(items);
        depth_ = 0;

        scrim_->setBounds(host_->localBounds());
        scrim_->setVisible(true);

        auto &lv = level(0);
        lv.panel->setItems(&items_);
        placeLevel(lv, at);
        showLevel(lv);
        lv.panel->focusInitial();
        open_ = true;
    }

    void dismiss()
    {
        if (!open_)
            return;
        for (int i = 0; i <= depth_ && i < int(levels_.size()); ++i)
            levels_[std::size_t(i)]->panel->commitTypeIn();
        hideLevelsFrom(0);
        scrim_->setVisible(false);
        open_ = false;
        if (onDismiss)
            onDismiss();
    }

    bool isOpen() const { return open_; }
    MenuPlacement placement() const { return placement_; }

    /** @brief Fired after any dismissal, including a pick. */
    std::function<void()> onDismiss;

  private:
    /**
     * @brief Full-host click catcher, painted under the panels.
     *
     * A faint wash rather than nothing at all, so it is visible that the menu is
     * modal - and so the scrim is obviously the thing being clicked when
     * debugging dismissal.
     */
    class Scrim : public Component<Scrim, interfaces::Paints, interfaces::MouseEvents>
    {
      public:
        explicit Scrim(Parent p) : Component(p) { setVisible(false); }
        const MenuStyle *style{nullptr};
        std::function<void()> onClick;

        void paint(Canvas &g) override { g.fillRect(g.bounds(), style->scrim); }
        void mouseUp(const MouseEvent &) override
        {
            if (onClick)
                onClick();
        }
    };

    /**
     * @brief One open level of the cascade.
     *
     * @ref rect is HOST-LOCAL in both placements, and is the source of truth for
     * where the level sits. It cannot be recovered from `panel->bounds()`,
     * because in desktop mode the panel fills its own window and its bounds are
     * therefore (0, 0, w, h) - so submenu positioning would collapse onto the
     * origin if it read the panel.
     */
    struct Level
    {
        MenuPanel *panel{nullptr};
        std::unique_ptr<Frame> window; ///< desktop placement only
        Rect rect;                     ///< host-local, design units
        /** @brief Which of THIS level's rows has its submenu open, or -1. The
         *         guard that stops an open submenu being rebuilt. */
        int openChildRow{-1};
    };

    /**
     * @note Held by pointer so elements have STABLE ADDRESSES. `level(n)`
     *       appends, and callers legitimately hold a `Level &` for level n
     *       across the call that creates level n + 1; by value that reference
     *       would dangle on reallocation.
     */
    Level &level(int index)
    {
        while (int(levels_.size()) <= index)
        {
            const int mine = int(levels_.size());
            auto lv = std::make_unique<Level>();

            if (placement_ == MenuPlacement::desktop)
            {
                // Borderless top-level. Created off-screen at a nominal size;
                // every show repositions and resizes it. PLUGWINDOW is outside
                // the quit count, so an idle one keeps nothing alive.
                lv->window = std::make_unique<Frame>(
                    host_->session(), NEUI_W_PLUGWINDOW,
                    Rect{-10000.0f, -10000.0f, style_->width, 32.0f}, "menu");
                lv->panel = &lv->window->add<MenuPanel>();
            }
            else
            {
                lv->panel = &host_->add<MenuPanel>();
            }

            lv->panel->setStyle(style_);
            lv->panel->onHoverRow = [this, mine](int row) { hoverChanged(mine, row); };
            lv->panel->onActivate = [this, mine](int row) { activate(mine, row); };
            lv->panel->onDismissRequested = [this]() { dismiss(); };
            levels_.push_back(std::move(lv));
        }
        return *levels_[std::size_t(index)];
    }

    /**
     * @brief Host-local point -> screen logical pixels.
     *
     * @note The frame's `get_pos` is the OUTER window origin, so on a titled
     *       APPWINDOW this lands the menu low by the title-bar height. Harmless
     *       for the borderless PLUGWINDOW a plugin editor actually is, which is
     *       why it is not corrected for here.
     */
    Point toScreen(Point hostLocal) const
    {
        int fx = 0, fy = 0;
        auto &s = host_->session();
        s.widgets()->get_pos(s.raw(), host_->widget(), &fx, &fy);
        const float z = s.zoom();
        return {float(fx) + hostLocal.x * z, float(fy) + hostLocal.y * z};
    }

    /** @brief Decide @p lv's host-local rect, and move whatever carries it. */
    void placeLevel(Level &lv, Point at)
    {
        const float w = style_->width;
        const float h = lv.panel->preferredHeight();
        float x = at.x, y = at.y;

        if (placement_ == MenuPlacement::inFrame)
        {
            // Clamped, because it physically cannot overflow: neui clips a child
            // to its parent, so an unclamped panel would be cut off rather than
            // hang outside. Desktop placement needs none of this.
            const Rect host = host_->localBounds();
            if (x + w > host.getRight())
                x = std::max(host.getX(), host.getRight() - w);
            if (y + h > host.getBottom())
                y = std::max(host.getY(), host.getBottom() - h);
        }

        lv.rect = {x, y, w, h};

        if (placement_ == MenuPlacement::desktop)
        {
            // set_pos direct rather than Frame::setBounds: a frame's position is
            // SCREEN logical pixels and must not be multiplied by the zoom, but
            // its client size must be.
            auto &s = host_->session();
            const float z = s.zoom();
            const Point o = toScreen({x, y});
            s.widgets()->set_pos(s.raw(), lv.window->widget(), int(std::lround(o.x)),
                                 int(std::lround(o.y)), int(std::lround(w * z)),
                                 int(std::lround(h * z)));
            lv.panel->setBounds(Rect::fromSize(w, h));
        }
        else
        {
            lv.panel->setBounds(lv.rect);
        }
    }

    void showLevel(Level &lv)
    {
        if (lv.window)
            lv.window->show();
        lv.panel->setVisible(true);
    }

    void hoverChanged(int levelIndex, int row)
    {
        if (levelIndex != depth_)
        {
            // Hovering back up into an ancestor closes everything below it.
            hideLevelsFrom(levelIndex + 1);
            depth_ = levelIndex;
        }
        if (row < 0)
            return;

        auto &lv = level(levelIndex);
        const auto *items = lv.panel->items();
        if (!items || row >= int(items->size()))
            return;
        const auto &it = (*items)[std::size_t(row)];

        if (it.kind == MenuItem::Kind::submenu && it.enabled)
        {
            // ALREADY OPEN FOR THIS ROW - do nothing. Reopening re-runs
            // setItems, set_pos and show on a live OS window, which in desktop
            // placement flickers hard. Cheap and invisible in-frame, which is
            // why it survived until the windows arrived.
            if (lv.openChildRow == row)
                return;

            hideLevelsFrom(levelIndex + 1);

            const Rect r = lv.panel->rowRect(row);
            const Rect pb = lv.rect; // host-local in both placements
            auto &child = level(levelIndex + 1);
            child.panel->setItems(&it.children);
            placeLevel(child, {pb.getRight() - 3.0f, pb.getY() + r.getY() - style_->padding});
            showLevel(child);
            lv.openChildRow = row;
            depth_ = levelIndex + 1;
        }
        else
        {
            hideLevelsFrom(levelIndex + 1);
            lv.openChildRow = -1;
            depth_ = levelIndex;
        }
    }

    void activate(int levelIndex, int row)
    {
        auto &lv = level(levelIndex);
        const auto *items = lv.panel->items();
        if (!items || row >= int(items->size()))
            return;
        const auto &it = (*items)[std::size_t(row)];
        if (it.kind == MenuItem::Kind::submenu)
            return;              // opening it is the hover's job
        auto action = it.action; // copy: dismiss() may outlive the item vector
        dismiss();
        if (action)
            action();
    }

    /**
     * @brief Hide every level from @p levelIndex down, and keep the open-child
     *        bookkeeping true.
     *
     * A hidden level has no open child, and the level ABOVE no longer has one
     * either - miss that second part and the guard in @ref hoverChanged goes
     * stale, so a submenu closed by hovering away never reopens.
     */
    void hideLevelsFrom(int levelIndex)
    {
        for (int i = levelIndex; i < int(levels_.size()); ++i)
        {
            auto &lv = *levels_[std::size_t(i)];
            lv.panel->setVisible(false);
            if (lv.window)
                lv.window->setVisible(false);
            lv.openChildRow = -1;
        }
        if (levelIndex > 0 && levelIndex - 1 < int(levels_.size()))
            levels_[std::size_t(levelIndex - 1)]->openChildRow = -1;
    }

    ComponentCore *host_{nullptr};
    const MenuStyle *style_{nullptr};
    MenuPlacement placement_{MenuPlacement::inFrame};
    Scrim *scrim_{nullptr};
    std::vector<std::unique_ptr<Level>> levels_;
    std::vector<MenuItem> items_;
    int depth_{0};
    bool open_{false};
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_COMPONENTS_POPUPMENU_H
