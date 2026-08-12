/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_COMPONENTS_POPUPMENU_H
#define NEUIPLUSPLUS_COMPONENTS_POPUPMENU_H

#include <algorithm>
#include <functional>
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
 * @brief The controller: owns the item tree, the scrim and the panel stack.
 *
 * Not a component itself. It hangs its panels off a host component - pass the
 * FRAME, so a submenu can paint anywhere in the window.
 *
 * @code
 * npp::PopupMenu menu{frame, style};
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
    explicit PopupMenu(ComponentCore &host, const MenuStyle &style) : host_(&host), style_(&style)
    {
        // Scrim first so it paints UNDER every panel: neui paints children in
        // creation order. It catches the click-outside that dismisses.
        scrim_ = &host.add<Scrim>();
        scrim_->style = style_;
        scrim_->onClick = [this]() { dismiss(); };
    }

    /** @brief Open at @p at, in HOST-local design units. */
    void show(std::vector<MenuItem> items, Point at)
    {
        items_ = std::move(items);
        depth_ = 0;

        scrim_->setBounds(host_->localBounds());
        scrim_->setVisible(true);

        auto &p = panel(0);
        p.setItems(&items_);
        placePanel(p, at);
        p.setVisible(true);
        p.focusInitial();
        open_ = true;
    }

    void dismiss()
    {
        if (!open_)
            return;
        for (int i = 0; i <= depth_; ++i)
            panel(i).commitTypeIn();
        hidePanelsFrom(0);
        scrim_->setVisible(false);
        open_ = false;
        if (onDismiss)
            onDismiss();
    }

    bool isOpen() const { return open_; }

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

    MenuPanel &panel(int level)
    {
        while (int(panels_.size()) <= level)
        {
            auto &p = host_->add<MenuPanel>();
            p.setStyle(style_);
            const int mine = int(panels_.size());
            p.onHoverRow = [this, mine](int row) { hoverChanged(mine, row); };
            p.onActivate = [this, mine](int row) { activate(mine, row); };
            p.onDismissRequested = [this]() { dismiss(); };
            panels_.push_back(&p);
        }
        return *panels_[std::size_t(level)];
    }

    /** @brief Position a panel at @p at, nudged to stay inside the host. */
    void placePanel(MenuPanel &p, Point at)
    {
        const Rect host = host_->localBounds();
        const float w = style_->width;
        const float h = p.preferredHeight();
        float x = at.x, y = at.y;
        if (x + w > host.getRight())
            x = std::max(host.getX(), host.getRight() - w);
        if (y + h > host.getBottom())
            y = std::max(host.getY(), host.getBottom() - h);
        p.setBounds({x, y, w, h});
    }

    void hoverChanged(int level, int row)
    {
        if (level != depth_)
        {
            // Hovering back up into an ancestor closes everything below it.
            hidePanelsFrom(level + 1);
            depth_ = level;
        }
        if (row < 0)
            return;

        auto &p = panel(level);
        const auto *items = p.items();
        if (!items || row >= int(items->size()))
            return;
        const auto &it = (*items)[std::size_t(row)];

        if (it.kind == MenuItem::Kind::submenu && it.enabled)
        {
            const Rect r = p.rowRect(row);
            const Rect pb = p.bounds();
            auto &child = panel(level + 1);
            child.setItems(&it.children);
            placePanel(child, {pb.getRight() - 3.0f, pb.getY() + r.getY() - style_->padding});
            child.setVisible(true);
            depth_ = level + 1;
        }
        else
        {
            hidePanelsFrom(level + 1);
            depth_ = level;
        }
    }

    void activate(int level, int row)
    {
        auto &p = panel(level);
        const auto *items = p.items();
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

    void hidePanelsFrom(int level)
    {
        for (int i = level; i < int(panels_.size()); ++i)
            panels_[std::size_t(i)]->setVisible(false);
    }

    ComponentCore *host_{nullptr};
    const MenuStyle *style_{nullptr};
    Scrim *scrim_{nullptr};
    std::vector<MenuPanel *> panels_;
    std::vector<MenuItem> items_;
    int depth_{0};
    bool open_{false};
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_COMPONENTS_POPUPMENU_H
