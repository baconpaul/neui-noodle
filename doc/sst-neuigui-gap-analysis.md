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

1. a small number of `NEUI_W_CUSTOMDRAW` surfaces — **one per `NamedPanel`** is
   the sweet spot; see §5,
2. inside each, as much `juce::Component`-equivalent machinery as that panel
   actually needs (a flat hit-test loop, not a full tree),
3. the existing sst-jucegui components repainted through a thin `Graphics`
   shim over `neui_painter_api_t`,
4. all layout in zoom-independent **design units**, multiplied out at the
   boundary — §5.1.

Against that plan the neui gaps are **three parity bugs, seven small API
additions, and two architectural changes** — consolidated as a hand-over list in
**§9**. Most of the small ones are things neui already does internally and
doesn't expose; the two architectural ones (render-passive child widgets, and an
accessibility seam) are a single programme, and together they decide whether the
per-control granularity that accessibility wants is affordable at all.

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

### 2.6 No client-settable UI scale (not a blocker — a should-be-free)

Unlike 2.1–2.5 you can work around this completely in the client (§5.1), so it
does not block anything. It is listed here because the fix is unusually cheap
and would delete client code rather than add it.

**Zoom is DPI scale.** neui already owns exactly one mechanism for turning
client logical coordinates into physical ones; a UI zoom is a second multiplier
on that same axis. Two things make exposing it look small:

- `NEUI_API_METRICS` already has `ui_scale` — the concept exists and is wired
  (iOS Dynamic Type drives it). It is read-only and **process-global**.
- On win32, *every* logical↔physical conversion funnels through one function,
  `Session::get_dpi_for_widget`. All 81 `LogicalToPhysical` sites read from it,
  and so do the reverse conversions — mouse coordinates in `ChildSubclassProc`
  and the widget size in the CUSTOMDRAW paint path. Returning
  `real_dpi × frame_zoom` from it would deliver zoom to geometry, input and
  painting in a single change.

**Ask:** `set_ui_scale(session, frame, factor)` — per-frame rather than
process-global — multiplied into the effective DPI. Native control fonts
(HFONT / NSFont) are applied separately and would not follow, but everything in
sst-neuigui is CUSTOMDRAW so that does not bite. The risk to watch is any
conversion that does *not* go through the one function: those become
inconsistent-by-one-multiplier bugs, which is exactly where this would break.

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
there (`AccessibilityHandler`, `KeyboardTraverser`, `FocusDebugger`) and JUCE
wires it to the platform screen readers. neui has none — no `WM_GETOBJECT`, no
`IAccessible`, no `NSAccessibility` in either native host, and no client API to
declare a role or value. How much of that you have to rebuild depends entirely
on the granularity choice, which is why accessibility ends up being the thing
that decides §5 rather than a footnote to it.

## 5. Granularity: how many CUSTOMDRAW widgets?

The obvious framing is "one surface vs. one widget per control", and the
obvious tiebreaker is zoom. **Both of those are wrong**, and it is worth being
explicit about why, because the first draft of this document got it wrong.

### 5.1 Zoom does not decide it

two-filters does `setTransform(AffineTransform::scaled(zoomFactor))` on the
whole editor, which looks like it forces a single surface. It doesn't. Keep all
layout in float **design units** and multiply at the boundary — two places, one
source of truth:

```cpp
// Geometry: round EDGES, not position-and-size independently.
void place(neui_widget_t w, float x, float y, float dw, float dh, float z) {
    int x0 = (int)std::lround(x * z),        y0 = (int)std::lround(y * z);
    int x1 = (int)std::lround((x + dw) * z), y1 = (int)std::lround((y + dh) * z);
    ui.set_pos(w, x0, y0, x1 - x0, y1 - y0);
}

// Paint: scale once, then draw in design units.
void paint(const neui_event_paint_t &e, float z) {
    e.painter_api->scale(e.p, z, z);
    render(Canvas{e}, e.width / z, e.height / z);   // component code is zoom-blind
}
```

The second one is the prize: **the ported sst-jucegui paint code needs no zoom
awareness at all.** It draws at design size and the painter scales, so fonts
rasterize at the true size and stay crisp — no bitmap scaling anywhere.

Rules that come with it:

- **Snap edges, not rects.** Rounding position and size independently makes
  adjacent panels gap or overlap by a pixel at fractional zoom. Snapping edges
  makes neighbours share one.
- **Mouse events arrive in zoomed logical px** — divide by `z` on the way in.
- **Hairlines vanish.** A 1.0-unit stroke at z=0.75 is 0.75px; use
  `max(1.0f/z, w)`.
- **Changing zoom is a `set_pos` per widget.** Nine calls at panel granularity,
  two hundred at control granularity, and win32 exposes no
  `BeginDeferWindowPos`, so expect a flash at the fine end.
- Inter-widget alignment quantizes to integers, so a continuous graphic
  spanning two widgets will show a seam at fractional zoom. Keep such things
  inside one widget.

Adopt this discipline regardless of granularity: it is also what makes moving
between granularities — or onto §2.6 if it ever lands — nearly free.

### 5.2 Accessibility is the real tiebreaker

With zoom neutralised, the strongest argument is one that points the *other*
way, toward more widgets:

**A native view per component is by far the easiest path to accessibility.** An
HWND / NSView per control means the platform supplies the accessibility tree,
focus tracking and hit-testing, and you only fill in role, name and value per
control. Collapse to one surface and you must build a parallel accessibility
tree by hand — UIA fragment providers with full navigation on Windows,
`NSAccessibilityElement` children on macOS. That machinery *is*
`juce::AccessibilityHandler`, and it is not small.

The catch, verified in the tree: **neui has no accessibility anything.** No
`WM_GETOBJECT`, no `IAccessible`, no `NSAccessibility` overrides in either
native host, and no client-facing API to declare a role or value. Its TODO has
"Tier B native focus parity — real focus-proxy HWNDs per widget" and stops
there. So a widget-per-control build today inherits the tree and focus, and a
screen reader reads every control as an anonymous "window".

**Ask (belongs with §2):** a client seam to declare accessibility role / name /
value / state per CUSTOMDRAW, which the hosts surface via `WM_GETOBJECT` and
`NSAccessibility`. Without it, widget-per-control buys the substrate but not
the outcome; with it, accessibility becomes nearly free at that granularity and
stays expensive at every coarser one.

### 5.3 The three options

| | One surface | **Per panel (~9)** | Per control (~200) |
|---|---|---|---|
| Component tree you write | Full `juce::Component` equivalent, ~1.5–2.5k lines | Flat hit-test loop per panel, ~10 lines of hover | Almost none |
| Hover / focus / clip | All yours | neui gives it per panel; within-panel is yours | All from neui |
| Invalidate granularity | Whole editor | One panel (~300×200) | One control |
| Zoom | `painter->scale` | §5.1, 9 `set_pos` | §5.1, 200 `set_pos`, may flash |
| Backend contexts | 1 | ~9 | **~200** — each painted widget owns a `paint_ctx` (`WidgetData::paint_ctx`), i.e. a D2D render target each |
| Accessibility | Hand-built tree | Hand-built tree | **Platform tree free — but blocked on the seam above** |
| Overlays (tooltip, modal) | Free | `NEUI_ATTR_OVERLAY` widget on top | `NEUI_ATTR_OVERLAY` widget on top |

### 5.4 The one hard constraint: a DXGI swap chain per painted widget

If accessibility is the destination, the detour through a hand-built component
tree is the *expensive* route — panel granularity means writing hit-test, hover
and focus code that per-control granularity would delete. So the honest question
is not "why not go all the way", it is "what stops you". Exactly one thing does,
and it is contained.

`d2d_build_hwnd_target` (`backends/d2d/d2d_backend.cpp`) allocates, **per
painted widget**:

- an `ID2D1DeviceContext` (off the shared, process-global `g_d2d_device` — fine),
- an `IDXGISwapChain1` from `CreateSwapChainForHwnd`, `BufferCount = 2`,
  `FLIP_SEQUENTIAL`,
- an `ID2D1Bitmap1` aliased to the back buffer, and a solid-colour brush.

At 200 painted controls that is 200 swap chains, ~400 back buffers, a `Present`
per widget per frame, and 200 independently DWM-composited surfaces. Rough
memory at 2× DPI on 100×100 logical controls is ~64 MB of back buffer alone;
the presentation and composition load is the part that actually worries me, and
the creation latency at window-show is a third concern.

**macOS has no equivalent problem.** Its per-view context is a CoreGraphics
context over the view's `drawRect:` — no swap chain, no GPU allocation. So this
is a win32-backend-shaped constraint, not a neui-wide one.

**And neui already has the right architecture — in its other host.** The
crossplatform host paints a whole frame in one pass: `Session::paint_frame`
walks the tree, applies per-widget transform and clip from the cached
`abs_x/abs_y`, and issues one `begin_frame`/`end_frame` against **one context
per frame**. The fix is not new architecture; it is letting the win32 native
host reuse the architecture neui already ships. See §9 for the concrete shape.

The principle underneath: **decouple "has a native HWND" from "owns a render
context".** A child HWND is worth having for input, focus and accessibility. It
is not worth having for pixels.

**Recommendation.** Go to **peer per accessible element** — a widget for
anything a screen reader should announce (knob, button, switch, slider, meter),
and everything decorative (labels, rules, dividers, panel chrome, plot
backgrounds) painted by its parent. That makes the widget tree *identical* to
the accessibility tree, which is what you want anyway, and is the same call JUCE
makes when it marks decorative components inaccessible. For two-filters that is
~60–100 widgets rather than ~200, with no throwaway hit-test code.

Gate it on one measurement, which costs twenty minutes: **put 100 and then 200
`CUSTOMDRAW` widgets in a window on Windows and watch memory, show latency and
resize smoothness.** If it holds, go straight to per-element and skip the
component tree entirely. If it doesn't, fall back to panel granularity with the
§5.1 discipline — and §9's Tier-3 change is what would lift the ceiling
permanently.

Either way you use maybe 15–20% of neui: `CUSTOMDRAW`, the painter, the
mouse/key stream, frame management, clipboard, and (once it exists) embedding.
The widget catalogue, grid, compound/behavior assets and tree APIs go unused.
That is worth raising with its author, because **"be the canvas + window + input
layer under a toolkit" is a different product from "be the toolkit"**, and neui
is currently much more the second. The asks in §2 are close to exactly the list
the first product needs and the second doesn't prioritise.

## 6. two-filters checklist

What the editor needs, and where it lands:

| Need | Status |
|---|---|
| Knob, VSlider, ToggleButton, MultiSwitch, MenuButton, JogUpDownButton, GlyphButton, Label, RuledLabel, TextPushButton | Port — paint code, §3 shim |
| NamedPanel (header, tabs, hamburger, toggle) | Port |
| WindowPanel, ScreenHolder / ModalBase (about screen) | Port; z-order via an `NEUI_ATTR_OVERLAY` widget on top |
| ToolTip | Port — it's an in-window component, no desktop window needed |
| VUMeter | Port + **timer** (§2.1) |
| Continuous / Discrete data bindings | Straight port, no JUCE |
| StyleSheet + light/dark built-ins | Straight port |
| Preset menu, param context menus | **Blocked on §2.3** |
| Value type-in | Own text editing (§4) |
| Load/save patch | **Blocked on §2.5** |
| Zoom | Client-side design units (§5.1); free if §2.6 lands |
| CLAP plugin window | **Blocked on §2.2** |
| Accessibility | Regression vs JUCE; cost depends on granularity (§5.2) |

## 7. Staged plan

- **Phase 0 — prove the shim.** One `CUSTOMDRAW` panel, a hand-written
  `Graphics` shim, and exactly two components: a `Knob` bound to a `Continuous`,
  and a `NamedPanel` around it, in design units with a zoom slider. Style them
  from a ported StyleSheet. This answers "does the paint code port
  mechanically?" and "is §5.1 zoom convincing?" in a day or two, and it is the
  cheapest possible test of the whole thesis.
- **Phase 1 — panel granularity for real.** A panel base class: flat hit-test,
  hover, capture, and the `place()` boundary. Port 5–6 more components. Decide
  §5.3 here with real numbers rather than on paper.
- **Phase 2 — the neui asks.** Timer and text metrics first (they block
  everything), then menus, then embedding. Each is a small, well-scoped PR
  upstream, and §2/§3 are written to be handed over as-is. The accessibility
  seam (§5.2) belongs here too, and its outcome decides whether control
  granularity is ever worth moving to.
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

---

## 9. Summary: changes to neui

Consolidated ask list, written to be handed over. Framing first, because it
matters: **none of this is a criticism of neui's current design.** For the
use case neui is built around — a window of native controls plus a couple of
custom-drawn canvases — every choice below is correct. These are the deltas
between that and "be the substrate a custom widget toolkit is built on", which
is a different product, and one neui's own TODO already points at under
*Audio-plugin / drawable framework*. Most of these are prerequisites for that
programme rather than requests from outside it.

Ordered by leverage.

### Tier 1 — bugs / host parity (small, no design work)

| # | Change | Why |
|---|---|---|
| 1 | **`buttonmap` is hardcoded `0` on win32-native `MOUSE_MOVE`** (`ChildSubclassProc`, `hosts/win32/window.cpp`). The xpl host forwards the real `wParam`; macOS-native sets `NEUI_MK_LBUTTON` explicitly. | `d/events.h` documents the `NEUI_MK_*` bits as matching Win32's, so the natural drag check `m.buttonmap & NEUI_MK_LBUTTON` silently never fires on Windows' default host. Bit us immediately. |
| 2 | **No `NEUI_EVENT_RESIZE` from the crossplatform host on macOS** — `platform_macos.mm` has no `windowDidResize:` plumbing, while win32 / Linux / iOS xpl and macOS-native all fire it. | Layout freezes on resize under the xpl host on macOS only. |
| 3 | **Wheel events carry no modifier bits.** `neui_event_wheel_t` has no `buttonmap`. | Already on neui's TODO — blocks `fine_modifier` on wheel. Same fix serves clients doing Shift-for-fine by hand. |

### Tier 2 — small API additions (each is a self-contained PR)

| # | Change | Why |
|---|---|---|
| 4 | **Timer / idle.** `add_timer(session, ms)` + a `TIMER` event, or an `on_idle` client callback. | A plugin editor must drain its audio→UI queue and animate meters at ~60 Hz. Today the only repaint driver is `invalidate`, so a standalone app has to abandon `run()` for a hand-rolled `pump_once()` loop. neui already runs timers internally (Linux timerfd heartbeat, macOS runloop) — this is exposure, not implementation. |
| 5 | **Text metrics.** `measure_text` returns width only. Add ascent / descent / line height, and/or an alignment argument on `draw_text`. | You cannot vertically centre text correctly today. 23 of sst-jucegui's draw-text calls pass a `Justification`. The single most-felt painter gap. |
| 6 | **Painter primitives:** rounded rectangle (fill + stroke), ellipse (fill + stroke), `draw_line`. | 25 / 19 / 6 call sites respectively in sst-jucegui. The backends can already do rounded rects — the compound `rect` layer exposes `corner_radius` — so this is plumbing, not capability. |
| 7 | **Cursor.** A public `set_cursor` seam. | Knobs hide the cursor while dragging; splitters want a resize cursor; `NamedPanel` sets one over its header. `NEUI_CURSOR_EW_RESIZE` already exists in `hosts/crossplatform/platform.h`, host-private. neui's own TODO blocks the behavior asset's `cursor` prop on exactly this. Pair it with **unbounded / warped pointer movement** so a vertical drag doesn't stop at the screen edge. |
| 8 | **Rich popup menus.** Expose the `MENUBAR` tree model as a popup — submenus, checkmarks, section headers, per-item enable — dispatched async by item id. | Today: a flat `const char* const*`, blocking, returns an index. A preset browser is inherently nested. The model already exists in the tree API; it just isn't reachable for popups. |
| 9 | **Per-frame client UI scale.** `set_ui_scale(session, frame, factor)`, multiplied into the effective DPI. | Zoom is DPI scale. `NEUI_API_METRICS::ui_scale` already exists but is read-only and process-global. On win32 *every* logical↔physical conversion funnels through `Session::get_dpi_for_widget` — including mouse coords in `ChildSubclassProc` and the paint size in the CUSTOMDRAW path — so returning `real_dpi × frame_zoom` from it delivers zoom to geometry, input and painting at once. Watch for any conversion that bypasses it; those become off-by-one-multiplier bugs. |
| 10 | **File dialog.** Open / save, alongside `message_box` and `toast` in `NEUI_API_NOTIFY`. | Patch load/save. Currently platform code in every client. |

### Tier 3 — architectural (the two that decide whether this is possible at all)

**11. Render-passive child widgets — decouple "has an HWND" from "owns a render context".**

`d2d_build_hwnd_target` gives every painted widget its own `IDXGISwapChain1`
(`CreateSwapChainForHwnd`, 2 buffers) plus an `ID2D1DeviceContext` and back-buffer
bitmap. That is exactly right for a handful of canvases and becomes the binding
constraint at a hundred-plus painted controls: a `Present` per widget per frame
and a DWM-composited surface each. macOS has no equivalent cost (its per-view
context is a CoreGraphics context over `drawRect:`).

The proposal is not new architecture — **it is the architecture the
crossplatform host already has.** `Session::paint_frame` walks the tree,
applies per-widget transform and clip from cached `abs_x/abs_y`, and issues one
`begin_frame`/`end_frame` per *frame*. Applied to win32-native:

- one swap chain per top-level frame; a child painted widget's context becomes
  `{ shared device context, offset, size, clip }`,
- `begin_frame` on a child sets transform + pushes clip; `end_frame` pops;
  `Present` happens once per frame,
- the frame coalesces child invalidations into one repaint per tick — neui
  already promises "one repaint per event-loop tick at most", so the machinery
  is close,
- child HWNDs stop painting entirely and exist for input, focus and
  accessibility only.

Worth noting: this is the same feature as neui's existing TODO item *"Tier B
native focus parity — real focus-proxy HWNDs per widget"*, reached from the
rendering side instead of the focus side. They want the same thing — a native
child window that is semantic rather than pictorial.

The fiddly part is `WS_CLIPCHILDREN`: for the frame to paint through child
regions, children must not exclude the parent's drawing, so they need to no-op
`WM_PAINT` / `WM_ERASEBKGND` and the parent must not clip them out.

**12. Accessibility seam.**

Neither native host has any: no `WM_GETOBJECT`, no `IAccessible`, no
`NSAccessibility` overrides, and no client API to declare a role. Ask: a
per-widget client declaration of **role / name / value / state**, surfaced by
the hosts through `WM_GETOBJECT` (UIA or MSAA) and `NSAccessibility`.

This is cheap *only* at fine widget granularity, where the platform supplies the
tree, focus tracking and hit-testing and the client fills in semantics. At
coarse granularity someone has to hand-build UIA fragment providers with full
navigation — which is what `juce::AccessibilityHandler` is, and it is not small.
So **11 and 12 are one programme**: 11 makes fine granularity affordable, 12
makes it worth having. Either alone is worth much less than both.

Related, and on the same list: neui's focus story is uneven across hosts today
(its TODO notes KNOB isn't a keyboard tab-stop on macOS, and macOS Tab
participation follows the system Full Keyboard Access setting). Accessibility
needs reliable, host-consistent focus.

### If only three things

4 (timer), 5 (text metrics), and 11 (render-passive children). The first two
unblock everything immediately and are small; the third is the one that decides
whether a custom widget toolkit on neui can scale to a real plugin editor.

