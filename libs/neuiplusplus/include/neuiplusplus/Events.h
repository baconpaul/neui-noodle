/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_EVENTS_H
#define NEUIPLUSPLUS_EVENTS_H

#include <cstdint>

#include "draw/Geometry.h"

/**
 * @file
 * @brief The typed input payloads handed to a component.
 *
 * neui delivers one flat `neui_event_t` with a tagged union and widget-local
 * INTEGER coordinates. These are the per-category structs the dispatcher hands
 * to a component instead, already converted to design units.
 *
 * @ref neuiplusplus::Modifiers deliberately answers "which buttons and modifiers
 * are held" rather than exposing the raw mask: hosts have disagreed about how
 * that mask is populated (win32-native reported 0 on MOUSE_MOVE until neui's
 * wave-0 fixes), so MouseEvent::isDragging is computed by the dispatcher from
 * latched button state, not read from the wire.
 */

namespace neuiplusplus
{

/** @brief Held mouse buttons and keyboard modifiers, as a set of predicates. */
class Modifiers
{
  public:
    constexpr Modifiers() = default;
    explicit constexpr Modifiers(std::uint32_t bits) : bits_(bits) {}

    constexpr bool shift() const { return (bits_ & kShift) != 0; }
    constexpr bool ctrl() const { return (bits_ & kCtrl) != 0; }
    constexpr bool alt() const { return (bits_ & kAlt) != 0; }
    /** @brief Command on macOS, Win/Super elsewhere. KEY events only - neui's
     *         mouse buttonmap has no bit for it, so it reads false there. */
    constexpr bool meta() const { return (bits_ & kMeta) != 0; }
    constexpr bool leftButton() const { return (bits_ & kLeft) != 0; }
    constexpr bool rightButton() const { return (bits_ & kRight) != 0; }
    constexpr bool middleButton() const { return (bits_ & kMiddle) != 0; }

    /** @brief The conventional "fine adjust" modifier for value drags. */
    constexpr bool fine() const { return shift(); }

    constexpr std::uint32_t raw() const { return bits_; }
    constexpr bool operator==(const Modifiers &) const = default;

    /// @name Bit values
    /// Mirror the `NEUI_MK_*` values so a mouse buttonmap passes straight
    /// through without a translation table. Key events arrive in the unrelated
    /// `NEUI_KMOD_*` space and the dispatcher maps them onto these.
    /// @{
    static constexpr std::uint32_t kLeft = 0x0001;
    static constexpr std::uint32_t kRight = 0x0002;
    static constexpr std::uint32_t kShift = 0x0004;
    static constexpr std::uint32_t kCtrl = 0x0008;
    static constexpr std::uint32_t kMiddle = 0x0010;
    static constexpr std::uint32_t kAlt = 0x0020;
    /// no NEUI_MK_ equivalent; set on key events only
    static constexpr std::uint32_t kMeta = 0x0040;
    /// @}

  private:
    std::uint32_t bits_{0};
};

/** @brief One mouse event. Which KIND it is is answered by which handler was called. */
struct MouseEvent
{
    Point position{};     ///< component-local, design units
    Point downPosition{}; ///< where the current drag began; == position if not dragging
    Modifiers mods{};
    int clickCount{0};      ///< 2 on a double click
    bool isDragging{false}; ///< latched by the dispatcher, not read off the wire

    constexpr Point dragDelta() const { return position - downPosition; }
};

/**
 * @brief One wheel notch.
 *
 * Three different answers to "which way did it go" live here, because they are
 * genuinely different questions - see @ref delta, @ref physicalDelta and
 * @ref valueDelta.
 */
struct WheelEvent
{
    Point position{};
    /**
     * @brief The delta AS DELIVERED; positive = up / left.
     *
     * Correct for CONTENT scrolling: moving the content with your fingers is
     * exactly what natural scrolling is for, so a scrolling consumer uses this
     * and ignores the rest of this struct.
     */
    float delta{0.0f};
    bool isHorizontal{false};
    /**
     * @brief True when the OS already inverted @ref delta from the physical gesture.
     *
     * macOS "Natural scrolling", which is on by default. Honest about what it
     * can promise: true means definitely inverted, false means not inverted OR
     * this platform cannot tell - only macOS can answer at all (win32 applies
     * the setting in the touchpad driver, Linux in libinput, neither queryable).
     */
    bool isFlipped{false};
    Modifiers mods{};

    /**
     * @brief The direction the user's fingers actually travelled.
     *
     * A VALUE control has no content to move, so it wants the direction before
     * the OS inverted it. Undoes the two sign changes a client cannot otherwise
     * see: the platform layer's Shift->horizontal flip (deliberately narrow -
     * only a horizontal notch WITH Shift, since a genuine tilt-wheel gesture is
     * real), then the natural-scrolling inversion. Mirrors neui's own
     * `wheel_physical_delta`, which is host-internal.
     */
    float physicalDelta() const
    {
        float d = delta;
        if (isHorizontal && mods.shift())
            d = -d;
        if (isFlipped)
            d = -d;
        return d;
    }

    /**
     * @brief How far a VALUE should move for this notch, sign included.
     *
     * THE CONVENTION HERE IS UP-INCREASES: fingers or wheel up raises a knob or
     * fader.
     *
     * @warning This DELIBERATELY DISAGREES WITH UPSTREAM as of neui 19c0c57,
     * which chose up-decreases (`hosts/shared/wheel_direction.h::wheel_value_sign`)
     * and applies it to the built-in KNOB and SLIDER. So a component built on
     * this library and a native neui one in the same UI currently feel opposite -
     * the very thing that commit set out to end. A known, temporary divergence:
     *   - tested by hand on a Mac trackpad, up-decreases feels backwards;
     *   - upstream's own commit message records that the evidence for
     *     up-decreases being "the audio-plugin convention" was false - it rested
     *     on the behavior runtime's comment claiming to match the built-in KNOB
     *     while doing the reverse, and three of the four built-in paths were
     *     up-increases before the change.
     *
     * Pending resolution with upstream. Like theirs, the sign lives in exactly
     * one place, so realigning is a one-line edit.
     */
    float valueDelta() const { return physicalDelta(); }
};

/** @brief One key event. @p character is 0 for non-text keys. */
struct KeyEvent
{
    std::uint32_t keyCode{0};   ///< NEUI_KEY_*
    std::uint32_t character{0}; ///< unicode codepoint, 0 for non-text keys
    Modifiers mods{};
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_EVENTS_H
