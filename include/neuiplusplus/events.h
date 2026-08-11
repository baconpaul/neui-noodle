/*
 * neuiplusplus - a C++20 skin over the neui C API
 *
 * Typed input events. neui delivers one flat neui_event_t with a tagged union
 * and widget-local INTEGER coordinates; these are the per-category structs the
 * dispatcher hands to a component, already converted to design units.
 *
 * `buttons` deliberately answers "which buttons and modifiers are held" rather
 * than exposing the raw mask: hosts have disagreed about how that mask is
 * populated (win32-native reported 0 on MOUSE_MOVE until neui's wave-0 fixes),
 * so `isDragging` is computed by the dispatcher from latched button state, not
 * read from the wire.
 */

#ifndef NEUIPLUSPLUS_EVENTS_H
#define NEUIPLUSPLUS_EVENTS_H

#include <cstdint>

#include "geometry.h"

namespace neuiplusplus
{

class Modifiers
{
  public:
    constexpr Modifiers() = default;
    explicit constexpr Modifiers(std::uint32_t bits) : bits_(bits) {}

    constexpr bool shift() const { return (bits_ & kShift) != 0; }
    constexpr bool ctrl() const { return (bits_ & kCtrl) != 0; }
    constexpr bool alt() const { return (bits_ & kAlt) != 0; }
    constexpr bool leftButton() const { return (bits_ & kLeft) != 0; }
    constexpr bool rightButton() const { return (bits_ & kRight) != 0; }
    constexpr bool middleButton() const { return (bits_ & kMiddle) != 0; }

    // The conventional "fine adjust" modifier for value drags.
    constexpr bool fine() const { return shift(); }

    constexpr std::uint32_t raw() const { return bits_; }
    constexpr bool operator==(const Modifiers &) const = default;

    // Mirrors the NEUI_MK_* bit values so the dispatcher can pass them through.
    static constexpr std::uint32_t kLeft = 0x0001;
    static constexpr std::uint32_t kRight = 0x0002;
    static constexpr std::uint32_t kShift = 0x0004;
    static constexpr std::uint32_t kCtrl = 0x0008;
    static constexpr std::uint32_t kMiddle = 0x0010;
    static constexpr std::uint32_t kAlt = 0x0020;

  private:
    std::uint32_t bits_{0};
};

// Which mouse event this is. The dispatcher passes it to the generated thunk,
// which fans out to the matching MouseEvents<D> virtual - so the kind never
// reaches component code.
enum class MouseKind
{
    enter,
    exit,
    move,
    down,
    drag,
    up,
    doubleClick,
    rightDown,
    rightUp
};

enum class KeyKind
{
    pressed,
    typed,
    released
};

struct MouseEvent
{
    Point position{};       // component-local, design units
    Point downPosition{};   // where the current drag began; == position if not dragging
    Modifiers mods{};
    int clickCount{0};      // 2 on a double click
    bool isDragging{false}; // latched by the dispatcher, not read off the wire

    constexpr Point dragDelta() const { return position - downPosition; }
};

struct WheelEvent
{
    Point position{};
    // As delivered. Correct for CONTENT scrolling: moving the content with your
    // fingers is exactly what natural scrolling is for, so a scrolling consumer
    // uses this and ignores the rest.
    float delta{0.0f}; // notches; positive = up / left
    bool isHorizontal{false};
    // 1 when the OS already inverted the delta relative to the physical gesture
    // (macOS "Natural scrolling", on by default). Honest about what it can
    // promise: true means definitely inverted, false means not inverted OR this
    // platform cannot tell - only macOS can answer.
    bool isFlipped{false};
    Modifiers mods{};

    // The direction the user's fingers actually travelled, for a VALUE control -
    // a knob or fader has no content to move, so it wants the direction before
    // the OS inverted it. Undoes two sign changes the client cannot otherwise
    // see: the platform layer's Shift->horizontal flip (deliberately narrow: only
    // a horizontal notch WITH Shift, since a genuine tilt-wheel gesture is real),
    // then the natural-scrolling inversion. Mirrors neui's own
    // wheel_physical_delta, which is host-internal.
    float physicalDelta() const
    {
        float d = delta;
        if (isHorizontal && mods.shift())
            d = -d;
        if (isFlipped)
            d = -d;
        return d;
    }
};

struct KeyEvent
{
    std::uint32_t keyCode{0}; // NEUI_KEY_*
    std::uint32_t character{0}; // unicode codepoint, 0 for non-text keys
    Modifiers mods{};
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_EVENTS_H
