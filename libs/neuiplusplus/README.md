# neuiplusplus

A C++20 skin over the [neui](https://github.com/defiantnerd/neuilib) C API.

neui exposes a flat C ABI: opaque session handles, function-pointer vtables, one
tagged-union event struct, integer device pixels. That is the right shape for a
framework boundary and the wrong shape to write a hundred widgets against.
`neuiplusplus` is the layer in between.

**It is** value types (`Color`, `Rect`, `Point`, `Transform`, `Font`), a `Canvas`
that binds neui's painter vtable to its opaque handle behind RAII state guards,
capability interfaces, and a `Component` that owns a `CUSTOMDRAW` widget plus its
children.

**It is not** styling, look and feel, or data binding. Those belong to a widget
library built on top of this one. This layer only removes the mechanical friction
of driving neui from C++.

## The one shape worth knowing

```cpp
#include <neuiplusplus/neuiplusplus.h>
namespace npp = neuiplusplus;

struct Knob : npp::Component<Knob, npp::Paints, npp::MouseEvents>
{
    Knob(npp::Parent p, Model &m) : Component(p), model(m) {}

    void paint(npp::Canvas &g) override
    {
        auto s = g.savedState();
        g.fillEllipse(g.bounds().reduced(2.0f), style.body);
    }
    void mouseDown(const npp::MouseEvent &e) override { beginRelativeDrag(); }
    void mouseDrag(const npp::MouseEvent &e) override { repaint(); }

    Model &model;
};

auto &knob = panel.add<Knob>(model);   // returns Knob &, panel owns it
```

A component names its capabilities in its base declaration. That decides three
things at once: which handlers it may `override`, which operations it gets
(`repaint()` only if it paints, `setCursor()` only if it takes pointer input),
and whether neui hit-tests it at all.

Capabilities are virtual interfaces rather than detected member functions on
purpose. `override` is compiler-checked; a duck-typed handler name is not, and a
misspelled handler under detection compiles clean and silently never fires -
the worst failure mode a UI toolkit has.

> **Build with `-Wsuggest-override`** (clang: `-Winconsistent-missing-override`,
> MSVC: `/w14263`). It closes the one remaining hole: an override written
> without the keyword. The library turns it on for itself; it cannot turn it on
> for your component code without dictating your warning flags.

## Layout

| path                     | holds                                                |
| ------------------------ | ---------------------------------------------------- |
| `Component.h`            | `ComponentCore`, `Component<>`, `Frame`, `Parent`     |
| `Session.h`              | `Session` - neui handles, dispatch table, zoom        |
| `Events.h`               | `MouseEvent`, `WheelEvent`, `KeyEvent`, `Modifiers`   |
| `interfaces/`            | one capability interface per file                     |
| `draw/`                  | `Canvas`, `Color`, `Geometry`, `Font`, `Transform`    |
| `components/`            | ready-made widgets (`Label`, so far)                  |
| `A11y.h` `Cursor.h` `FileDialog.h` | the small vocabularies                      |
| `detail/`                | internal; nothing here is API                         |

The capability interfaces are defined in `neuiplusplus::interfaces`, one per
file, and re-exported into `neuiplusplus` by `interfaces/Interfaces.h` - a base
clause reading `npp::interfaces::MouseEvents` four times over buys nothing. Both
spellings name the same entity. Concrete widgets stay in `neuiplusplus` directly:
they are the library's public vocabulary, and `npp::Label` is the name people
will type.

## Design units

Everything above the neui boundary is in **design units**: zoom-independent
floats. `Session::setZoom` multiplies on the way out and divides on the way in,
and `ComponentCore::setBounds` snaps *edges* rather than position and size
independently, so neighbours share a pixel boundary at fractional zoom instead of
gapping or overlapping. Paint code never multiplies by anything.

## Ownership

C++ owns the tree; neui mirrors it. A component creates its neui widget on
construction and destroys it on destruction; children are `unique_ptr` members of
the parent, built through `add<T>` and never with a bare `new`.

That is not ceremony. Inside `add<T>` the concrete type is statically known,
which is the only moment the capability set can be resolved - with `is_base_of_v`
and function pointers, no RTTI. A `dynamic_cast` in the core constructor could
not see them: the derived object does not exist yet.

Components are **non-movable**; the dispatch table stores `this`. Same constraint
`juce::Component` has.

## Building

`add_subdirectory` after neui. The library does not fetch or configure neui
itself, because which hosts are compiled in is the application's decision:

```cmake
set(NEUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NEUI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/neui)
add_subdirectory(libs/neuiplusplus)

target_link_libraries(my-ui PRIVATE neuiplusplus)
```

Configuring without the neui targets in scope is a hard `FATAL_ERROR`, not a
mysterious link failure later.

The **crossplatform host** (`"neui.host.crossplatform"`) is what this library is
written against, and what you should ask `neui_get_api` for. It is the only host
implementing embedding, timers, cursors, the relative pointer and accessibility.
On Windows and macOS `neui_get_api(NULL)` returns the *native* host instead,
which has none of them - `Session` feature-detects and degrades rather than
crashing, but a cursor that does nothing is easier to diagnose if you know why.

## Known limits

- **Concrete components are leaf types.** You derive from `Component<Self, ...>`,
  not from another component: the capability set is resolved against the exact
  type passed to `add<T>`, so a subclass of `Label` would not find
  `Paints<Subclass>` among its bases. Compose instead - hold a `Label` as a
  child.
- **`Transform` cannot be given to the painter.** neui has no `set_transform`,
  only `translate` / `rotate` / `scale`. `Transform` is a value type for the
  arithmetic the painter cannot do for you - most usefully inverting the ops you
  pushed, so you can hit-test the shape you drew.
- **The wheel-to-value convention here disagrees with upstream neui.**
  `WheelEvent::valueDelta` is up-increases; neui's built-in KNOB and SLIDER moved
  to up-decreases in 19c0c57. Deliberate and temporary; see the comment on
  `valueDelta` in `Events.h` for the argument and for why the sign lives in
  exactly one place.

## License

MIT. See [LICENSE](LICENSE).
