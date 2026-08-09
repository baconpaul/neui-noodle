# What is neui, and how do you actually talk to it?

Notes taken while wiring up `src/main.cpp` (two `CUSTOMDRAW` widgets: a hover
box and a draggable sine). Everything here was read out of `libs/neui` at
submodule commit `0077fe0`; where I'm repeating the project's own docs rather
than something I exercised, I say so.

---

## 1. What it is

**neui** is an early-stage cross-platform GUI framework with a **pure C client
interface** and **native platform hosts** behind it. The pitch is the opposite
of Electron: don't ship a browser, use the platform's own windows, fonts,
colours and metrics, and let the app look like it belongs.

The tree splits three ways:

| Layer | Where | What it is |
|---|---|---|
| **Client API** | `include/neui/` | C headers. This is the whole contract. |
| **Host** | `hosts/{win32,macos,ios,crossplatform}` | Owns windows, the run loop, input, the widget tree. |
| **Backend** | `backends/{d2d,cg,cairo,null}` | Owns pixels. Direct2D / CoreGraphics / Cairo. |

On macOS you get **two** hosts in one binary:

- `neui.host.macos` — native AppKit. Every widget is a real `NSView`; buttons
  are `NSButton`s.
- `neui.host.crossplatform` ("xpl") — one surface per window, neui draws
  *everything* itself through the CoreGraphics backend.

Same client code, same API, different feel. `src/main.cpp` takes `--xpl` to
flip between them, which is the cheapest available A/B on how much "native"
actually buys you.

---

## 2. The C idiom, and how to read it

If you've written a CLAP plugin, the shape is immediately familiar — it is
essentially COM-without-COM, and the five mechanisms below are the whole trick.
Learn these and the 4,600 lines of headers stop being intimidating.

### 2.1 One entry point, then a registry

```c
neui_init();                                 // register every host linked in
neui_api_t* host = neui_get_api("neui.host.macos");   // or NULL = first/native
```

`neui_init()` fans out to per-host registration wrappers that are gated at
compile time (`NEUI_HAS_MACOSHOST` etc.). Which hosts exist is decided by your
**link line**, not by runtime discovery — this is why `CMakeLists.txt` links
`neui neui-xplhost neui-macoshost` rather than just `neui`.

### 2.2 Handles are structs wrapping a `uint32_t`

```c
typedef struct neui_session { uint32_t session; } neui_session_t;
typedef struct neui_widget  { uint32_t id; }      neui_widget_t;
typedef struct neui_item    { uint32_t id; }      neui_item_t;
```

Pass by value, treat as opaque, never mutate. The struct wrapper exists purely
so the compiler stops you handing a widget id to something wanting a session
id. `widget_none = UINT32_MAX` is the "no parent" sentinel; `widget_root = 0`.
Widget ids pack the owning session in the upper 16 bits, so a cross-session
handle is detected and silently dropped rather than corrupting anything.

### 2.3 Interfaces are structs of function pointers, fetched by name

```c
neui_widget_api_t* widgets =
  (neui_widget_api_t*) host->get_interface(sess, NEUI_API_WIDGETS);
```

Every subsystem is a separate vtable behind a versioned string id:

```
com.defiantnerd.neui.extension.widgets/0
com.defiantnerd.neui.extension.attrs/0     assets/0  tree/0  grid/0
clipboard/0  dnd/0  commands/0  renderer/0  scroll/0  notify/0  filter/0
```

A host may legitimately return `NULL` for one it doesn't implement (the doc
calls out `_FILTER` as optional), so a robust client null-checks. The `/0`
suffix is the ABI version; new methods get **appended to the end** of an
existing struct so slot offsets never move, and the header tells you which
methods are late arrivals ("vtable-appended; check the api version / pointer
before calling"). If you're consuming a prebuilt neui, that check is real work.
Against a submodule you compile yourself, it's theatre — the struct definition
and the implementation are always in sync.

### 2.4 `void* token` is the `this` pointer

There are no callbacks with closures in C. So you hand neui a `void*` when you
create the session and it hands it back on every call:

```c
app.session = host->create_session(&client, &app);   // &app is the token
...
static bool NEUI_ABI on_event(void* token, neui_event_t* e) {
  App* app = (App*)token;   // downcast, and you're back in your own world
}
```

Symmetry worth noticing: **the client also exposes a `get_interface`**, so the
host can ask the client for optional interfaces (`NEUI_API_MENU_CLIENT`,
`NEUI_API_THEME_CLIENT`, `NEUI_API_GRID_CLIENT`). It's a bidirectional
negotiation, not a one-way API.

### 2.5 One flat event callback with a tagged union

```c
typedef struct neui_event {
  neui_event_type_t type;
  union { neui_event_mouse_t mouse; neui_event_key_t key;
          neui_event_paint_t paint; /* ...18 more... */ } data;
} neui_event_t;
```

`onevent` fires for **every** widget with `emit_events` set. Returning `true`
consumes the event; `false` lets neui's own handling run (and for
`NEUI_EVENT_APP_QUIT`, `true` means "yes, really close").

The event type constants encode a category in the low 16 bits, so
`type & 0xFFFF == 0x0001` is "some mouse event" if you ever want to filter
coarsely.

> **The pitfall the project's own CLAUDE.md warns about twice:** every payload
> carries a `.widget`, and you *must* check it. Branching on `event->type`
> alone means your handler fires for the wrong widget. Note the union member
> name differs per category (`.data.mouse.widget` vs `.data.paint.widget`), so
> "get the widget id from an event" is a `switch`, not a field access — see
> `is_mouse_event()` in `main.cpp`.

---

## 3. The whole minimal program

That's genuinely it — eight calls:

```c
neui_init();
neui_api_t* host   = neui_get_api(NULL);
neui_session_t s   = host->create_session(&client, &app);
neui_widget_api_t* w = host->get_interface(s, NEUI_API_WIDGETS);
neui_widget_t win  = w->create(s, widget_none, NEUI_W_APPWINDOW, 140,140, 600,400, NULL);
neui_widget_t kid  = w->create(s, win, NEUI_W_CUSTOMDRAW, 16,16, 568,118, NULL);
w->show(s, win);
host->run(s);            // blocks until quit
host->destroy(s);
```

Widget *types* are strings (`"neui.std.customdraw"`), not an enum — extensible
without an ABI break, at the cost of typo-at-runtime instead of typo-at-compile.

`run()` blocks. If you already own an outer loop (DAW plugin, game frame), use
`pump_once()` instead, which drains pending events and returns.

---

## 4. `CUSTOMDRAW` + the painter — what this noodle actually uses

`NEUI_W_CUSTOMDRAW` is the escape hatch: a widget that emits
`NEUI_EVENT_WIDGET_PAINT` and lets you draw the whole thing.

```c
typedef struct neui_event_paint {
  neui_widget_t            widget;
  struct neui_painter_api* painter_api;  // vtable
  struct neui_painter*     p;            // opaque handle, valid ONLY in this call
  float width, height;                   // logical px
  bool  focused;
} neui_event_paint_t;
```

The painter is a **curated subset** of the render backend — shapes, text, a
path builder, clip/transform/alpha/font stacks, assets, gradients. Context
lifecycle (`begin_frame`, `create_context`, `resize`) is deliberately *not*
reachable, because a client calling `begin_frame` mid-frame would wipe the
surface. Good instinct; the API is smaller than the backend on purpose.

Conventions that matter:

- **Origin `(0,0)` is the widget's top-left**, logical pixels at 96 DPI. Same
  for paint and for mouse coordinates, on every host — including the xpl host,
  which translates frame-local input down to the target widget.
- **Colours are `0xAARRGGBB`.** No colour struct, no float channels.
- The framework brackets your paint callback in
  `push_transform / push_clip(bounds) / pop_clip / pop_transform`, so an
  unbalanced push on your side can't corrupt a sibling widget.
- Nothing repaints itself. State changed → call `widgets->invalidate(s, id)`.
  Invalidations coalesce to one repaint per loop tick.

Drag handling has no capture call and no drag event: **a held-button drag
arrives as `MOUSE_MOVE` with `NEUI_MK_LBUTTON` set in `buttonmap`.** That's
what the sine widget keys off. (Verified in `hosts/macos/window.mm`
`mouseDragged:`; AppKit's implicit per-view mouse capture means the moves keep
coming even once the pointer leaves the widget.)

Hover is easier than on raw AppKit — the host maintains the tracking area and
sends `MOUSE_ENTER` / `MOUSE_LEAVE` for you.

---

## 5. Mapping it onto C++20

The C API is deliberately un-opinionated about client structure, which leaves
exactly three jobs for a C++ layer. `src/main.cpp` does all three in about 90
lines of scaffolding (`Canvas` / `Ui` / `Component`, lines 50–140):

**a) Bind the painter handle to the vtable.** Every painter call is
`api->fill_rect(p, ...)` — two things threaded through every call site. A
`Canvas` class holding both turns that into `c.fill(...)`. This is the single
highest-value wrapper in the whole API.

**b) Turn the flat callback back into virtual dispatch.** neui gives you one
`onevent` and a widget id; C++ wants `component->paint()`. A `Component` base
with `paint(Canvas&)` / `mouse(type, payload)` plus an id→object lookup is all
it takes. With two components a linear scan is right; with fifty you'd want a
`std::unordered_map<uint32_t, Component*>` — but note the *shape* is unchanged,
because neui hands you a dense small integer, so a vector indexed by the low 16
bits would also work.

**c) Carry the session + widget-api pair.** Nearly every call starts
`(session, widget, ...)`. Bundling those into a `Ui` struct with `create()` /
`invalidate()` members removes the most repetitive noise.

What I deliberately *didn't* do: no RAII wrapper around widget handles. Widgets
are owned by the host's tree and destroyed with their parent, so a
`unique_ptr`-alike would be fighting the framework, not helping it.

C++20 features that actually paid off here: `std::numbers::pi_v<float>`,
`constexpr` colour and tuning constants, and init-statement `if` for the
lookup-and-use pattern (`if (Component* c = ...)`). `std::format` was avoided
on purpose — `snprintf` into a stack buffer is one line and has no libc++
version questions.

The one thing to be careful about: **`onevent` is `extern "C"`-shaped.** Use
`NEUI_ABI` on the definition and don't let a capturing lambda anywhere near it.
A file-scope `static` function taking the token and casting it is the honest
version, and it costs nothing.

---

## 6. Sizing, DPI, and the trap in the middle

`create()` on a frame takes the **client** size, not the outer window — the
host adds title bar and borders itself. But the project's docs are emphatic
that you should still read it back:

```c
int cx, cy, cw, ch;
widgets->get_client_rect(s, win, &cx, &cy, &cw, &ch);
```

because a host that draws its own in-frame menubar (Linux) reports a shorter
usable rect than the number you asked for, and laying children out against the
create size clips them by ~24px. `main.cpp` lays out against the returned rect
for that reason, even though on macOS with no menubar it's the identity.

Also documented: there is **no automatic layout**. No layout engine, no
constraints, no reflow on resize — `NEUI_EVENT_RESIZE` fires and re-fitting
children is your job. (The neui examples deliberately don't do it either.) For
a UI framework in 2026 this is the biggest thing that isn't in the box; whether
that's a gap or a scope decision is one of the things worth forming an opinion
on this week.

---

## 7. What else is in there (not used here)

Skimmed, not exercised — for orientation:

- **Widgets**: label, button, inputbox, multiline (with soft wrap), checkbox /
  tristate, listbox, combobox, treeview, menubar, image, slider, knob, section
  (optionally scrolling, with kinetic scroll), tabview, and a virtualised
  **grid** (cells are paint state, not widgets — a 10,000-row grid is one
  widget).
- **Attributes** (`NEUI_API_ATTRS`): a string-keyed per-widget property bag —
  `set_int` / `set_float` / `set_string` against well-known keys like
  `NEUI_PARAM_VALUE`, `NEUI_ATTR_ORIENTATION`. This is the framework's
  extensibility valve, and where the real semantics of the knob/slider live.
- **Compound / behavior / component assets**: a declarative JSON format for
  building a knob out of layers (`asset` / `text` / `rect` / `path`), with
  attribute bindings (`rotation ← neui.param.value * 4.712`) and input handlers
  (`drag_rotational`, `wheel`, `key_step`). One `create_from_component()` call
  instead of ~45 lines of hand-built compound. **This is the part most worth a
  serious look for audio UI** — it's a skinning format for parameter controls.
- Clipboard, full drag & drop (XDND v5 on Linux), SVG-style filter graphs,
  client font loading, render-to-surface, theme palette with system dark/light
  tracking, routed commands + accelerators, toasts and message boxes.

Deep docs live in `libs/neui/docs/` — `rendering-and-assets.md`,
`compound-behavior-component.md`, `attributes.md` are the three I'd read next.

---

## 8. Building it here

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/neui-noodle.app/Contents/MacOS/neui-noodle          # native AppKit host
./build/neui-noodle.app/Contents/MacOS/neui-noodle --xpl    # neui-drawn host
```

Notes:

- `MACOSX_BUNDLE` is required-ish: the AppKit host wants a real `.app` so
  `NSApp` can install a menubar and activate normally. Run the inner binary
  directly (not `open`) if you want `stdout` in your terminal.
- neui is C++17 internally and sets `CMAKE_CXX_STANDARD 17` — but inside its
  own directory scope, so it doesn't leak up. Our target is C++20 via
  `target_compile_features(... cxx_std_20)`.
- neui's examples and tests auto-default OFF when it's consumed via
  `add_subdirectory`. We force them off anyway so a stale cache can't turn a
  4-second build into a 40-second one.
- `libneui.a` legitimately appears twice on the link line (the core lib and the
  host libs reference each other's symbols on purpose). Apple's linker warns;
  `-Wl,-no_warn_duplicate_libraries` silences it, which is what neui itself
  does internally.

---

## 9. First-pass evaluation notes

Good:

- The C ABI discipline is real and consistent — versioned interface ids,
  vtable-append evolution, no exposed structs to accidentally break. This is a
  framework you could ship as a binary and still evolve.
- The painter is well-judged: exactly the draw-safe subset, with the
  footgun-shaped parts (`begin_frame`) walled off.
- Coordinate conventions are unified across hosts (widget-local, 96 DPI
  logical, top-left origin) and the headers state the contract explicitly
  rather than leaving it to be discovered.
- Two hosts on macOS from one client source is a genuinely useful property, and
  it's a strong argument that the abstraction line is drawn in the right place.

Open questions for the week:

1. **Layout.** Nothing in the box, resize is manual. For a plugin editor that's
   survivable; for an app it isn't. Is a layout API planned, or is the position
   "the client owns layout, full stop"?
2. **Widget-vs-drawing boundary.** Everything interesting for audio is
   `CUSTOMDRAW` + compound/behavior. How far does the declarative component
   format stretch before you fall back to hand-painting?
3. **`emit_events` and the flat callback** scale to a big UI how? Every event
   for every widget through one function, dispatched by id, is fine at two
   components — a real editor wants the framework to know about the routing.
   Worth checking whether the C++ scaffolding in §5 is the intended answer.
4. **Value semantics for parameters.** `NEUI_PARAM_VALUE` is normalised
   `[0..1]` with `VALUE_CHANGED` on user edits and no event on programmatic
   sets, which is the right split. How does that hold up under a
   host-automation feedback loop?
5. What does `--xpl` actually cost or gain? Worth putting real widgets (not
   just `CUSTOMDRAW`) in front of both hosts and comparing.
