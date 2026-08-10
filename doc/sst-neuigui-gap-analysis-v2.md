# What would `sst-neuigui` need from neui? — **v2**

Question: sst-jucegui is ~14.7k lines of MIT-licensed widgets bound to
`juce::Graphics`. Could there be an `sst-neuigui` with the same paradigm —
components, styles, data bindings — sitting on neui instead, and enough of it
to run `two-filters` (~3.1k lines of editor code)? What's missing?

---

**Version 2 — 2026-08-10, read against `bp-review` `8437efb`.**

v1 (`doc/sst-neuigui-gap-analysis.md`, read against neui `0077fe0`) is
**retained unchanged and is not superseded as a reference**: Timo's reply,
`libs/neui/plans/sst-neuigui-gap-response.md`, cites it by path, by section
(§2.2, §5.2, §5.4) and by its §9 ask numbers (#1–#12). Those citations must keep
resolving, so v1 stays exactly as he read it and revisions land here instead.

**Section and ask numbering is identical between v1 and v2 by design.** §5.4 is
still §5.4 and #11 is still #11, so his document reads correctly against either.
What changed is the *content* of those sections, and §0 — new in v2 — is the
ledger of what shipped, what is open, and which of my claims were wrong.

Changes in v2, in one paragraph: **target the crossplatform host explicitly**,
which retires the granularity cost argument entirely (on xpl only frames own
native resources); **embedding has shipped**, so two-filters moves ahead of the
remaining asks rather than behind them; **the perf ceiling changed shape** from
per-widget swap chains to whole-frame invalidate; **accessibility keeps the same
recommendation for a different reason**; and **three of my claims were wrong** —
#5's premise was inverted, #6's rationale was wrong, and #11 was right about the
wrong host.

Call-site counts are unchanged from v1 — sst-jucegui and two-filters as checked
out 2026-08-09.

---

---

## TL;DR

**Target the crossplatform host explicitly. Then neui's widget tree is the
component model, and the cost objection to using it finely disappears.**

`neui_get_api("neui.host.crossplatform")` — the README now points plugin
authors there, and it changes the shape of this whole document. On the xpl host
**only frames own a `native_handle` and a `render_ctx`**; every child widget is
a `Tree<WidgetData>` entry with cached `abs_x/abs_y`. There is no HWND, no
NSView and no swap chain per widget. So a widget is nearly free, and the
granularity question that dominated v1 answers itself.

What neui's widget tree gives you, on xpl, is `juce::Component`'s job: bounds,
parent/child, hit-testing, mouse routing with capture, hover enter/leave, focus
and tab traversal, clipping, coalesced invalidation. You don't rebuild it.
You adapt to it.

So `sst-neuigui` is:

1. **xpl host, always** — for the plugin *and* for standalone builds, so there
   is one rendering path to reason about,
2. one `NEUI_W_CUSTOMDRAW` per **accessible element** — knob, button, switch,
   slider, meter — with decoration (labels, rules, panel chrome) painted by the
   parent, so the widget tree *is* the tree neui's accessibility provider will
   walk (§5.2),
3. a thin `Component` adapter turning neui's flat `onevent` into virtual
   `paint` / mouse / key calls — an **adapter, not a framework**,
4. the existing sst-jucegui paint code through a `Graphics` shim over
   `neui_painter_api_t` — which as of Wave 1 is very nearly a 1:1 mapping (§3),
5. `StyleSheet` and `Continuous` / `Discrete` ported straight across — they
   contain no JUCE beyond `std::string`,
6. layout in zoom-independent **design units** until `NEUI_ATTR_UI_SCALE`
   lands, then delete the multiplication (§5.1).

The single biggest change since v1: **the #1 blocker is closed.**
Cross-platform DAW embedding shipped as `NEUI_API_EMBED`, so two-filters is
unblocked *ahead of* the remaining asks rather than behind all of them. What is
left is one perf question (whole-frame invalidate, §5.4), four API additions
(timer, cursor, popup menus, file dialog), and accessibility. §0 has the ledger.

---

## 0. Status at `bp-review` `8437efb`, and what I got wrong

### 0.1 The ledger

| Ask | State |
|---|---|
| §2.2 **cross-platform embedding** | **SHIPPED** — `NEUI_API_EMBED` (`d/embed.h`), win32 / macOS / Linux. `set_parent` before `show`; `event_fd` + `pump_and_tick` for the Linux run-loop case; neui owns no loop in embedded mode. |
| Host automation gestures *(I never asked)* | **SHIPPED** — `NEUI_EVENT_GESTURE_BEGIN` / `_END`, brackets a drag or a one-shot change. Maps straight to CLAP gestures and VST3 `beginEdit`/`endEdit`. Plugin-critical and I missed it. |
| #1 win32-native `buttonmap` | **FIXED**, and it was worse than I found — `RBUTTONDOWN` reported 0 too. |
| #1b win32-native **key modifiers** hardcoded 0 | **FIXED** — *I only found the mouse half of this bug family.* |
| #2 xpl-macOS `RESIZE` | **FIXED** — hooked on `NEUIView -setFrameSize:`, which covers both a standalone frame and an embedded PLUGWINDOW. |
| #3 wheel modifiers | **FIXED** — `neui_event_wheel_t` grew `buttonmap`; fine-on-wheel wired. |
| #5 text metrics + alignment | **SHIPPED** — `font_metrics(p, size, &ascent, &descent, &line_height)` and `draw_text_aligned(..., halign, valign)`. See 0.2. |
| #6 round rect / ellipse / line | **SHIPPED** — `fill_round_rect` / `draw_round_rect` / `fill_ellipse` / `draw_ellipse` / `draw_line`. See 0.2. |
| §3.5 italic | **SHIPPED** — `push_font_styled(family, weight, italic)`, resolved not synthesised. |
| #4 timer | Open — Wave 2. Harder than I said: needs a new `platform_timer_*` seam, not just exposure. |
| #7 cursor | Open — Wave 4.1, widening to a real cursor set plus pointer warping. |
| #8 rich popups | Open — Wave 4.2, as a `MENUBAR`-shaped tree dispatched async. |
| #9 per-frame UI scale | Open — Wave 3. **Note:** commit `7683adb` is titled "add user UI zoom (NEUI_ATTR_UI_SCALE)" but its tree is identical to `edf5f9c` and no such symbol exists. Do not assume it is there. |
| #10 file dialog | Open — Wave 4.3. |
| #11 render-passive children | **Withdrawn from the critical path.** Correct analysis, wrong host — see 0.2. |
| #12 accessibility | Open — Wave 6, the one real programme. See §5.2. |
| *(not mine)* xpl `invalidate` repaints the **whole frame** | Open — Wave 5. **This is the perf ceiling that actually applies to us.** |

### 0.2 Three corrections I own

**#5 — my premise was inverted.** I wrote "you cannot vertically centre text
correctly". In fact `draw_text` *already* centred vertically in the rect on all
three backends, from real font metrics (DirectWrite paragraph alignment,
`CTLineGetTypographicBounds`, `cairo_font_extents`) — centred was the *only*
thing available. The actual gap was the absence of any *choice* of alignment,
plus no metrics query. That is a different API than "add centring" would have
been, and it is what shipped.

**#6 — right conclusion, wrong reason.** I said the backends could already draw
rounded rects because the compound layer exposes `corner_radius`, so it was
"plumbing, not capability". No backend has a rounded-rect primitive; the
compound layer emulates one via `build_rounded_rect_path()` on the path API. So
it was cheaper than I thought, not for the reason I gave: all five shapes
shipped as painter-level helpers over the existing path API, zero backend
changes.

**#11 / §5.4 — correct analysis, wrong host.** The DXGI-swap-chain-per-widget
cost is real, and it is **win32-native only**. The xpl host already *is* the
architecture I proposed in #11: one `begin_frame`/`end_frame` per frame over one
context (`Session::paint_frame`), walking the tree with per-widget transform and
clip — and no child native windows to make render-passive. Since plugins are
routed to xpl, #11 is off our path entirely. It stays on file as the right spec
if standalone native-host apps ever need many painted widgets.

**And a fourth, about §5.2:** my accessibility argument inverts on xpl. I said
fine widget granularity gets you the platform's accessibility tree for free. On
xpl there is one native view per *frame*, so widget count buys nothing from the
OS — the provider tree must be synthetic no matter how you slice it. The
granularity recommendation survives, but see §5.2 for the real reason.

---

## 1. What sst-jucegui actually depends on

Counting `juce::` type references across its headers and sources:

| Group | Uses | Types | Verdict |
|---|---:|---|---|
| Value types | ~266 | `Colour` 128, `Rectangle` 57, `Colours` 41, `String` 16, `Point` 14, `MathConstants` 10 | Mechanical. Write your own or share a small header. |
| Drawing | ~182 | `Graphics` 85, `Justification` 39, `PathStrokeType` 16, `Font` 14, `ColourGradient` 10, `Path` 9, `AffineTransform` 9 | Maps onto neui's painter, modulo §3. |
| **Component + input** | **~259** | **`Component` 112, `MouseEvent` 103, `KeyPress` 26, `ModifierKeys` 13, `MouseWheelDetails` 5** | **Adapter surface, not reimplementation** — at one widget per element neui supplies the tree, hit-test, capture, hover and focus; you translate event shapes. |
| Text editing | 25 | `TextEditor` | Possibly free: with a real widget tree you can drop a neui `INPUTBOX` in and inherit IME, clipboard, undo and selection. |
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

> **Status (2026-08-10):** §2.2 has **shipped** as `NEUI_API_EMBED`; §2.6 is
> *not* implemented despite a commit title claiming it. §2.1, §2.3, §2.4 and
> §2.5 remain open as Waves 2, 4.2, 4.1 and 4.3. The analysis below is kept as
> written — it is what the asks were argued from — with the current state in
> §0.1.

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

> **Status (2026-08-10): essentially all closed by Wave 1.** Items 1–5 shipped
> as `font_metrics` + `draw_text_aligned`, `fill_round_rect` /
> `draw_round_rect` / `fill_ellipse` / `draw_ellipse` / `draw_line`, and
> `push_font_styled`. Only item 6 (wrapped multi-line at painter level) is
> deferred, and the wrap algorithm exists in the MULTILINE widget if it is ever
> wanted. **Two of the arguments below were wrong — see §0.2.** Kept as written
> for the record.

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

Smaller than it first looks, because one widget per element hands most of it
back to neui:

- **A `Component` adapter, not a component tree.** neui already owns bounds,
  parent/child, hit-testing, mouse capture, hover enter/leave, focus, clipping
  and coalesced invalidation. What you write is the piece neui deliberately
  leaves to the client: turning the flat `onevent` plus a widget id into
  `component->paint(Canvas&)` / `mouseDown(...)` / `mouseDrag(...)`. That is an
  id→object map and an event-shape translation — the same thing `src/main.cpp`
  in this repo does for three components, scaled up.
- **Value types.** `Colour`, `Rect`, `Point`, `Font`. Mechanical.
- **Layout.** neui has no layout engine, but neither does JUCE — sst-jucegui
  does manual arithmetic in `resized()`. You need a `resized()` equivalent
  driven by your own `place()` (§5.1), which is parity, not a regression.
- **The style sheet.** sst-jucegui's class/property/inheritance system is
  library-level and portable as-is — it needs a `Colour` and a `Font` type, not
  JUCE. Straight port.
- **The data bindings.** `Continuous` / `Discrete` / `WithDataListener` have no
  JUCE in them beyond `std::string`. Straight port.

And one thing gets *easier* than the single-surface route: **text editing.**
With a real widget tree, a type-in field can be an actual neui `INPUTBOX`
placed over the control, inheriting IME, clipboard, undo, selection and word
navigation rather than reimplementing them. 25 `TextEditor` uses potentially
collapse to widget creation plus a value round-trip.

**One thing gets actively worse:** accessibility. sst-jucegui does real work
there (`AccessibilityHandler`, `KeyboardTraverser`, `FocusDebugger`) and JUCE
wires it to the platform screen readers. neui has none — no `WM_GETOBJECT`, no
`IAccessible`, no `NSAccessibility` in either native host, and no client API to
declare a role or value. How much of that you have to rebuild depends entirely
on the granularity choice, which is why accessibility ends up being the thing
that decides §5 rather than a footnote to it.

## 5. Granularity: how many CUSTOMDRAW widgets?

This is the decision everything else hangs off, and it took three passes to get
right — worth recording, because the wrong turns are instructive:

1. *Zoom forces a single surface.* No — design units plus edge-snapped rounding
   handle zoom at any granularity (§5.1).
2. *Widget count forces panel granularity.* Only on the **native** hosts, where
   each painted widget owns a swap chain (§5.4). Not on xpl.
3. *Fine granularity makes accessibility free.* Only on the native hosts, where
   the platform supplies the tree. On xpl it is synthetic either way (§5.2).

What survives all three: **a widget per accessible element**, on xpl, where it
costs almost nothing and shapes the accessibility tree neui will synthesise.

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
- **Changing zoom is a `set_pos` per widget** — ~80 calls at per-element
  granularity, and win32 exposes no `BeginDeferWindowPos`, so expect a flash.
  One-time per zoom change, so probably fine; worth watching in Phase 0a.
- Inter-widget alignment quantizes to integers, so a continuous graphic
  spanning two widgets will show a seam at fractional zoom. Keep such things
  inside one widget.

Adopt this discipline regardless of granularity: it is also what makes moving
between granularities — or onto §2.6 if it ever lands — nearly free.

### 5.2 Accessibility — same recommendation, different reason

v1 argued: a native view per control means the *platform* supplies the
accessibility tree, so fine granularity makes accessibility nearly free. **On
the xpl host that argument is void.** There is one native view per
frame; widget count buys nothing from the OS, and the provider tree has to be
synthetic at any granularity.

That is not bad news — it is better scoping. Because the tree is synthetic
anyway, it gets built **once inside the xpl host** and serves win32, macOS and
Linux from one walk, instead of twice in two native hosts against two different
platform APIs. And the substrate is already there: per-widget bounds via
`abs_x/abs_y`, a `tab_stop` flag with real traversal (`collect_tab_stops` /
`focus_next`), hit-testing, enabled/visible state, and `NEUI_PARAM_VALUE` on
value-bearing widgets. Role, name, value, state, bounds and focus order are all
derivable; what is missing is two platform provider shims and one client
declaration API. That is Wave 6 — `NEUI_API_A11Y` plus a portable
`hosts/shared/a11y_tree.h`, with UIA on win32, `NSAccessibility` on macOS and
AT-SPI on Linux.

**So the recommendation is unchanged and the reason is better:** put a widget on
every accessible element, because **the widget tree is the tree neui's provider
will walk**. Decoration painted by the parent isn't just a cost saving now, it
is the `static-decorative` opt-out the provider expects.

Two design consequences for sst-neuigui, both worth building in from the start:

- **Wire `data::Continuous` straight to the a11y value API in the component
  base class.** `getValue` / `getMin` / `getMax` / `getValueAsString` is
  precisely the min / max / current / display-string quadruple Wave 6 reports
  for a continuous control. Do it once in the base and every knob, slider and
  meter is accessible by construction, with no per-component work.
- **Tab order comes from neui's traversal**, which follows widget creation
  order plus `set_tab_stop`. So create widgets in reading order, or manage the
  order explicitly — it is no longer a cosmetic detail once a screen reader is
  walking it.

A caveat worth stating plainly: **accessibility is not in neui's README, and
its TODO still lists focus parity as deferred.** The commitment lives in
`plans/sst-neuigui-gap-response.md` as Wave 6, which is a plan rather than
shipped code, and it is the largest item on that plan. Design for it, don't
schedule against it.

### 5.3 The three options — on the xpl host

| | One surface | Per panel (~9) | **Per accessible element (~60–100)** |
|---|---|---|---|
| Component code you write | Full `juce::Component` equivalent, ~1.5–2.5k lines | Flat hit-test loop per panel, hover derivation, partial focus | **Adapter only** — id→object dispatch + event-shape translation |
| Hover / focus / clip / capture | All yours | neui per panel; within-panel is yours | All from neui |
| Text entry | Hand-written | Hand-written | A real `INPUTBOX` — and on xpl it carries the full `EditHistory` (multi-level undo), which the win32 *native* one does not |
| Zoom | `painter->scale` | §5.1 | §5.1 — and no HWNDs to move, so no flash |
| Native resources | 1 frame | 1 frame | **1 frame — child widgets own no `native_handle` or `render_ctx` at all** |
| Repaint cost | Whole frame | Whole frame | Whole frame — **identical on all three, because xpl `invalidate` has no dirty rect (§5.4)** |
| Accessibility | Synthetic tree, coarse and lossy | Synthetic tree, coarse and lossy | **Synthetic tree that matches the controls** — neui builds it (Wave 6), your widget tree shapes it |
| Overlays (tooltip, modal) | Free | `NEUI_ATTR_OVERLAY` widget on top | `NEUI_ATTR_OVERLAY` widget on top |
| How much of neui you use | ~15% | ~25% | **Most of it** |

On the native hosts the right-hand column lost one row — the swap chains. On
xpl that row does not exist, and the repaint row is now *equal* across all
three rather than favouring the coarse ones. **The right-hand column wins
outright, and no measurement is needed to choose it.**

### 5.4 The one hard constraint: a DXGI swap chain per painted widget

*Superseded — retained because #11 is still the right spec for the native
hosts, and because the reasoning is what identified the real ceiling.*

The original finding stands and is confirmed: `d2d_build_hwnd_target` allocates
per painted widget an `ID2D1DeviceContext`, an `IDXGISwapChain1` from
`CreateSwapChainForHwnd` (2 buffers, `FLIP_SEQUENTIAL`), a back-buffer bitmap
and a brush. At 200 controls that is 200 swap chains, a `Present` each per
frame, and 200 DWM-composited surfaces. macOS has no equivalent cost.

**But it is win32-*native* only, and we are not on that host.** The xpl host
already is the architecture #11 proposes — `Session::paint_frame` does one
`begin_frame`/`end_frame` per frame over one context — and has no child native
windows to make render-passive. So this constraint does not apply to
sst-neuigui, and #11 should not be built on our account. The principle it
rests on is still worth stating for whoever picks it up: **decouple "has a
native HWND" from "owns a render context"** — a child HWND earns its keep for
input, focus and accessibility, not for pixels.

**The ceiling that does apply to us is the opposite shape.** On xpl,
`w_invalidate` maps *every* widget invalidation to a whole-frame repaint —
there is no dirty rect (`hosts/crossplatform/widgets.cpp`, and the comment says
so outright). A 60 Hz VU meter therefore repaints the entire editor sixty times
a second, at 2× DPI. That is neui's Wave 5, and it is the number worth having
early.

So **Phase 0a changes target**: not "how many widgets can I afford" — on xpl,
as many as I like — but "what does whole-frame invalidate cost at 60 Hz with a
realistic editor". Timo has offered to run exactly that as Wave 5's baseline
(a 100-widget CUSTOMDRAW grid at 60 Hz) and hand over the number. Take him up
on it rather than duplicating it; if it comes back comfortable, the last
performance question about this design closes.

**This is the version worth taking to neui's author**, because it is not the
"use your framework as a bare canvas" story. At per-element granularity
sst-neuigui uses *most* of neui — the widget tree, hit-testing, focus, capture,
invalidation, `INPUTBOX` for type-in, popup menus, clipboard, frames and plugin
windows — and adds the layer neui doesn't have and shouldn't: a styled,
data-bound, audio-specific *look and behaviour* library. neui stays the
component framework; sst-neuigui becomes a widget set on top of it.

That is a far more natural division than the single-surface alternative, and it
lines up with where neui's own TODO already points under *Audio-plugin /
drawable framework*. The catch is that it puts real weight on two places the
xpl host has not had to carry weight before — whole-frame invalidate (§5.4) and
the accessibility tree it must now synthesise (§5.2). Both are on Timo's plan
as Waves 5 and 6.

One upside I under-credited before: **xpl renders pixel-identically on every
platform.** For a skinned plugin editor that is exactly what you want, and it is
what sst-jucegui users already expect from JUCE. Choosing xpl is not settling
for the non-native host — for this use case it is the correct one, which is
also what neui's README now says.

## 6. two-filters checklist

What the editor needs, and where it lands:

| Need | Status |
|---|---|
| Knob, VSlider, ToggleButton, MultiSwitch, MenuButton, JogUpDownButton, GlyphButton, Label, RuledLabel, TextPushButton | Port — paint code, §3 shim |
| NamedPanel (header, tabs, hamburger, toggle) | Port |
| WindowPanel, ScreenHolder / ModalBase (about screen) | Port; z-order via an `NEUI_ATTR_OVERLAY` widget on top |
| ToolTip | Port — it's an in-window component, no desktop window needed |
| VUMeter | Port + **timer, open** (§2.1 / Wave 2). Interim: drive `invalidate` off the CLAP host timer. |
| Continuous / Discrete data bindings | Straight port, no JUCE — and wire to the a11y value API in the base class (§5.2) |
| StyleSheet + light/dark built-ins | Straight port |
| Preset menu, param context menus | **Open** (§2.3 / Wave 4.2) |
| Value type-in | A real neui `INPUTBOX` over the control — full `EditHistory` on xpl (§4) |
| Load/save patch | **Open** (§2.5 / Wave 4.3) |
| Zoom | Design units now (§5.1); delete the multiplication if Wave 3 lands |
| Host automation begin/end | **DONE** — `GESTURE_BEGIN` / `_END`, straight to CLAP gestures |
| CLAP plugin window | **DONE** — `NEUI_API_EMBED`, all three platforms |
| Accessibility | Regression vs JUCE until Wave 6; design for it now (§5.2) |

## 7. Staged plan

Reordered: embedding shipping early inverts the back half. Nothing now waits on
the whole ask list.

- **Phase 0a — ~~measure the widget ceiling~~ retired.** On xpl child widgets
  own no native resources, so the question is answered. Replace it with Timo's
  Wave 5 baseline — whole-frame invalidate at 60 Hz — which he has offered to
  run and hand over. Don't duplicate it.
- **Phase 0b — prove the shim.** A `Graphics` shim and two components: a `Knob`
  bound to a `Continuous`, and a `NamedPanel` around it, each its own widget,
  in design units, styled from a ported StyleSheet. Wave 1 shipped the pieces
  this needed — `draw_text_aligned`, `font_metrics`, `fill_round_rect`,
  `fill_ellipse`, `draw_line`, `push_font_styled` — so the shim should now be
  close to 1:1. **Nothing blocks this; start here.**
- **Phase 1 — the adapter.** A `Component` base wrapping a widget handle:
  id→object dispatch, event-shape translation, the `place()` boundary. Port
  5–6 more components. If this stays small, the thesis holds. Also unblocked —
  Wave 0 fixed the xpl-macOS `RESIZE` gap that would have broken it.
- **Phase 2 — two-filters end to end**, moved up. `NEUI_API_EMBED` plus
  `GESTURE_BEGIN`/`_END` cover the plugin shell, so the editor can be stood up
  against real automation before the remaining asks land. Expect to stub the
  preset menu (Wave 4.2), patch load/save (Wave 4.3) and cursor feedback
  (Wave 4.1), and to drive the 60 Hz idle off the CLAP host timer until Wave 2.
- **Phase 3 — close the gaps as they land.** Timer, cursor, popups, file dialog,
  UI scale. Each is independent; adopt in whatever order they arrive.
- **Phase 4 — accessibility.** Design for it from Phase 1 (widget per element,
  `Continuous` wired to the value API, deliberate tab order) but schedule
  against Wave 6, which is a plan and not yet code.

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

> **This list was handed over and answered.** Timo's verified reply and work
> plan is `libs/neui/plans/sst-neuigui-gap-response.md`; the current state of
> each item is §0.1 and the corrections to items 5, 6 and 11 are §0.2. **Items
> 1, 2, 3, 5 and 6 have shipped**, item 11 is withdrawn from our path, and one
> item that is not in this list — **xpl `invalidate` repainting the whole
> frame** — turned out to be the perf ceiling that actually applies to us.
> The numbering below is frozen so his references keep resolving.

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

~~This is cheap *only* at fine widget granularity, where the platform supplies
the tree.~~ **Corrected (§0.2).** That holds on the native hosts only. On xpl
there is one native view per frame, so the provider tree is synthetic at any
granularity — which decouples 11 from 12 entirely. **12 is now the whole
programme**, and it gets cheaper rather than dearer: one implementation inside
the xpl host serves win32, macOS and Linux, over a widget tree that already
carries bounds, tab order, hit-testing, enabled state and values. Fine
granularity still matters — it is what gives the provider walk something
meaningful to report — but it is no longer what makes the feature affordable.

Related, and on the same list: neui's focus story is uneven across hosts today
(its TODO notes KNOB isn't a keyboard tab-stop on macOS, and macOS Tab
participation follows the system Full Keyboard Access setting). Accessibility
needs reliable, host-consistent focus — and on xpl that traversal is neui's
own code rather than OS policy, which is another reason the work belongs there.

### If only three things

*As written 2026-08-09:* 4 (timer), 5 (text metrics), 11 (render-passive
children).

**Revised 2026-08-10.** 5 has shipped and 11 is off the path, so:

1. **Wave 5 — xpl partial repaint.** Whole-frame invalidate at 60 Hz is the
   only open performance question about this design, and the measurement alone
   may close it.
2. **Wave 2 — timer.** The last thing standing between a plugin editor and a
   correct idle loop that isn't borrowed from the CLAP host.
3. **Wave 6 — accessibility.** The one real programme, and the only item where
   this port is currently a regression against JUCE.

Waves 3 and 4 (UI scale, cursor, popups, file dialog) are each independently
useful and none of them block starting.

