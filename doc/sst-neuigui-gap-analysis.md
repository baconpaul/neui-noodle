# What would `sst-neuigui` need from neui?

Question: sst-jucegui is ~14.7k lines of MIT-licensed widgets bound to
`juce::Graphics`. Could there be an `sst-neuigui` with the same paradigm —
components, styles, data bindings — sitting on neui instead, and enough of it
to run `two-filters` (~3.1k lines of editor code)? What's missing?

Read at neui `0077fe0`, sst-jucegui and two-filters as checked out
2026-08-09. Numbers below are call-site counts from the actual trees.

---

## TL;DR

**The painter is nearly there. The component layer isn't, and never was going
to be — because sst-jucegui doesn't get its component layer from JUCE's
*widgets* either.** It gets it from `juce::Component`, which is a lightweight
in-process retained-mode drawing tree. neui's widget layer is a *native widget*
abstraction (one HWND / NSView per widget). Those are different things, and
conflating them is the trap.

So `sst-neuigui` is not "port sst-jucegui onto neui widgets". It is:

1. one full-window `NEUI_W_CUSTOMDRAW` surface,
2. a `juce::Component`-equivalent tree you write (~1500–2500 lines),
3. the existing sst-jucegui components repainted through a thin `Graphics`
   shim over `neui_painter_api_t`.

Against that plan the neui gaps are **five real blockers and about six small
painter additions**. None is architectural. The blockers are mostly things
neui already does internally but doesn't expose.

---

## 1. What sst-jucegui actually depends on

Counting `juce::` type references across its headers and sources:

| Group | Uses | Types | Verdict |
|---|---:|---|---|
| Value types | ~266 | `Colour` 128, `Rectangle` 57, `Colours` 41, `String` 16, `Point` 14, `MathConstants` 10 | Mechanical. Write your own or share a small header. |
| Drawing | ~182 | `Graphics` 85, `Justification` 39, `PathStrokeType` 16, `Font` 14, `ColourGradient` 10, `Path` 9, `AffineTransform` 9 | Maps onto neui's painter, modulo §3. |
| **Component + input** | **~259** | **`Component` 112, `MouseEvent` 103, `KeyPress` 26, `ModifierKeys` 13, `MouseWheelDetails` 5** | **This is the work. Nothing in neui covers it.** |
| Text editing | 25 | `TextEditor` | Needed for type-in. |
| Menus | 20 | `PopupMenu` | Big gap — §2.3. |
| Accessibility | ~25 | `AccessibilityHandler`, `AccessibilityRole` | Nothing in neui. |
| Timers | 6 | `Timer` | Gap — §2.1. |

And the drawing surface is *narrow*. Across all of sst-jucegui's `.cpp` the
distinct `juce::Graphics` calls are roughly:

```
130 setColour   35 fillRect   23 drawText   21 fillRoundedRectangle
 20 setFont     15 strokePath 14 fillEllipse 11 fillPath   8 fillAll
  7 setGradientFill  6 drawLine  5 drawEllipse  4 drawRoundedRectangle
  4 addTransform     3 drawRect  3 getCurrentFont
```

Sixteen methods. That is the entire graphics coupling of a 14.7k-line widget
library — which is why a shim is credible rather than wishful.

## 2. The blockers

Ordered by how much they hurt.

### 2.1 No public timer / idle callback

two-filters runs `idleTimer->startTimer(1000./60.)` to drain the audio→UI
queue, animate the VU meter, and poll rebuild flags. neui's public headers have
no timer at all — the only repaint driver is `widgets->invalidate`.

neui *has* timers internally (the Linux timerfd heartbeat driving scroll
spring-back, the macOS runloop). This is an exposure problem, not an
implementation one.

Workaround: in a CLAP plugin, `clap_host_timer_support` gives you a host timer
and you call `invalidate` from it — genuinely fine. Standalone, you'd have to
abandon `run()` for your own loop around `pump_once()`. Both work; neither is
something a UI framework should make you do.

**Ask:** `neui_timer_api` — `add_timer(session, ms) -> id`, a `TIMER` event, or
simply an `on_idle` client callback.

### 2.2 No cross-platform embedding into a host-provided parent

CLAP's `set_parent(clap_window)` hands the plugin an HWND / NSView and says
"build your UI inside this". neui goes the other way: `get_native_handle` on a
`PLUGWINDOW` hands *your* handle to someone else. The only real embedding seam
— `platform_set_embed_parent` / `platform_embed_event_fd` /
`platform_embed_pump_and_tick` — is host-internal *and Linux-only*
(`embed_parent_xid`).

You can force it: create a `PLUGWINDOW`, take `get_native_handle`, then
`SetParent` / `addSubview:` yourself. That's platform code in your plugin, on
each OS, with resize and DPI to re-derive.

**Ask:** a public "create this frame as a child of this native handle" on all
three platforms. This is the single biggest blocker for a plugin UI, and the
Linux seam shows the shape already exists.

### 2.3 Popup menus are far too weak

neui: `popup_menu(session, anchor, x, y, const char* const* items)` — a flat
NUL-terminated string array, `"-"` for a separator, **blocking**, returns a
1-based index.

two-filters' main menu has nested submenus (Factory Presets → category →
preset), section headers, checkmarks on the active zoom level, and a
`juce::PopupMenu::CustomComponent` embedding a `TextEditor` for value type-in.
`showMenuAsync` with `withParentComponent` throughout.

The irony: neui's `MENUBAR` tree API already has submenus, checkmarks
(`set_checked`), shortcuts, and per-item validation via `NEUI_API_MENU_CLIENT`.
The model exists; it just isn't reachable for popups.

**Ask:** popups built from the same tree model, dispatched async by item id.
Custom components in menus is a bigger request — worth designing out of
(a type-in overlay you draw yourself on the surface is arguably nicer anyway).

### 2.4 No cursor API

Knobs hide the cursor during drag; `NamedPanel::setOptionalCursorForNameArea`
sets one over the header; a splitter wants a resize cursor. neui has
`NEUI_CURSOR_EW_RESIZE` in `hosts/crossplatform/platform.h` — host-private.
neui's own TODO already flags this: the behavior asset's `cursor` prop is
"parsed but no-op in v1. Needs a public `set_cursor` seam first."

Also missing and needed for good knob feel: **unbounded / warped mouse
movement** (JUCE's `enableUnboundedMouseMovement`), so a vertical drag doesn't
stop at the screen edge.

### 2.5 No file dialog

two-filters loads and saves patches with `juce::FileChooser`. neui has
`message_box` and `toast` but no file chooser. Platform code, or skip it.

## 3. Painter gaps — small, real, all mechanical

Everything here is one PR each, and the backends can already do most of it
(the compound layer format exposes corner radius, so D2D/CG/Cairo have it).

1. **`draw_text` has no alignment**, and there are no **font metrics**.
   `measure_text` returns width only — no ascent, descent or line height, so
   you cannot vertically centre text correctly. 23 `drawText` sites, nearly all
   with a `Justification`. *This is the one that would annoy you daily.*
2. **No rounded rectangle** (fill or stroke). 25 sites.
3. **No ellipse** (fill or stroke). 19 sites. `arc` covers circles; ellipses
   need a scale-transform sandwich.
4. **No `draw_line`.** Trivially emulated with a 2-point path, but 6 sites.
5. **No italic** — the font stack is family + weight only.
6. **No measured multi-line / wrapped text** at painter level.

Nothing here is hard. Items 2–4 you could write once in the shim and never
think about again; item 1 needs a neui change because the information isn't
reachable from the client at all.

## 4. What you build in sst-neuigui (not neui's job)

Be honest that this is the bulk of the work:

- **The component tree.** Bounds, parent/child, z-order, hit-testing, mouse
  routing with capture, hover enter/exit, focus and tab traversal, dirty-region
  invalidation. This is `juce::Component`. ~1500–2500 lines.
- **Text editing.** 25 `TextEditor` uses — type-in fields, the value overlay.
  neui has `hosts/shared/text_edit.h` internally and real `INPUTBOX` widgets,
  but on a single surface neither is reachable; you write it or overlay a real
  neui widget on top of the canvas.
- **The style sheet.** sst-jucegui's class/property/inheritance system is
  library-level and portable as-is — it needs a `Colour` and a `Font` type, not
  JUCE. Straight port.
- **The data bindings.** `Continuous` / `Discrete` / `WithDataListener` have no
  JUCE in them beyond `std::string`. Straight port.

**One thing gets actively worse:** accessibility. sst-jucegui does real work
there (`AccessibilityHandler`, `KeyboardTraverser`, `FocusDebugger`), and JUCE
wires it to the platform screen readers. neui offers tab-stops and focus events
and no screen-reader story at all — and on a single CUSTOMDRAW surface you lose
even the tab traversal, because there is only one widget. If accessibility
matters for shipping, this is the hard problem, not the painter.

## 5. Why one surface rather than N neui widgets

Worth stating explicitly, because the other choice looks tempting:

- **Zoom.** two-filters does `setTransform(AffineTransform::scaled(zoomFactor))`
  on the whole editor. On one surface that's a `painter->scale()` and it's
  free. Across native child views it is impossible — you'd re-layout every
  widget at every zoom step and still have text hinting at the wrong size.
- sst-jucegui components are **all custom-painted anyway**. Mapping them onto
  neui widgets buys you nothing and costs an HWND/NSView each.
- Overlays, tooltips and modal screens want free z-order over the whole editor.
  One surface gives that; native child views make it a fight.
- neui's per-widget hover/enter/leave, focus and hit-testing are the things
  you'd give up — and you're rebuilding those anyway for the component tree.

The cost is that you use maybe 15% of neui: `CUSTOMDRAW`, the painter, the
mouse/key stream, window/frame management, clipboard, and (once it exists)
embedding. The widget catalogue, grid, compound/behavior assets and tree APIs
go unused.

That is a legitimate thing to notice about neui for this use case — and worth
raising with its author, because **"be the canvas + window + input layer for a
GUI toolkit built on top" is a different product than "be the GUI toolkit"**,
and neui is currently much more the second. The blockers in §2 are almost
exactly the list of things the first product needs and the second doesn't
prioritise.

## 6. two-filters checklist

What the editor needs, and where it lands:

| Need | Status |
|---|---|
| Knob, VSlider, ToggleButton, MultiSwitch, MenuButton, JogUpDownButton, GlyphButton, Label, RuledLabel, TextPushButton | Port — paint code, §3 shim |
| NamedPanel (header, tabs, hamburger, toggle) | Port |
| WindowPanel, ScreenHolder / ModalBase (about screen) | Port, needs z-order from the component tree |
| ToolTip | Port — it's an in-window component, no desktop window needed |
| VUMeter | Port + **timer** (§2.1) |
| Continuous / Discrete data bindings | Straight port, no JUCE |
| StyleSheet + light/dark built-ins | Straight port |
| Preset menu, param context menus | **Blocked on §2.3** |
| Value type-in | Own text editing (§4) |
| Load/save patch | **Blocked on §2.5** |
| Zoom | Free on one surface |
| CLAP plugin window | **Blocked on §2.2** |
| Accessibility | Regression vs JUCE (§4) |

## 7. Staged plan

- **Phase 0 — prove the surface.** One full-window `CUSTOMDRAW`, a hand-written
  `Graphics` shim, and exactly two components: a `Knob` bound to a `Continuous`,
  and a `NamedPanel` around it. Style them from a ported StyleSheet. This
  answers "does the paint code port mechanically?" in a day or two, and it is
  the cheapest possible test of the whole thesis.
- **Phase 1 — the component tree.** Bounds, hit-test, mouse routing with
  capture, hover, focus. Port 5–6 more components onto it.
- **Phase 2 — the neui asks.** Timer and text metrics first (they block
  everything), then menus, then embedding. Each is a small, well-scoped PR
  upstream, and §2/§3 are written to be handed over as-is.
- **Phase 3 — two-filters.** Only meaningful once Phase 2's embedding lands.

## 8. The part that isn't technical

two-filters says it: *"released under the MIT license, but has GPL3
dependencies, as such the combined work will be released under GPL3."* That's
JUCE.

neui is MIT. sst-jucegui is MIT. **An sst-neuigui port would let the combined
work stay MIT** — no JUCE licence, no GPL3 bind, no per-seat commercial
question for anyone building on it.

That is probably worth more than any single item in §2, and it's the reason to
take the effort estimate seriously rather than treating this as a curiosity.
