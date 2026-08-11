/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_A11Y_H
#define NEUIPLUSPLUS_A11Y_H

#include <neui/neui.h>

/**
 * @file
 * @brief Accessibility declarations, over `NEUI_API_A11Y`.
 *
 * A CUSTOMDRAW widget defaults to `ROLE_GROUP` and neui deliberately will not
 * guess anything better - a hand-painted control could be a slider, a button, an
 * XY pad or a drag source, and telling a screen-reader user "slider" about
 * something that is not one is worse than saying "group". So a custom widget set
 * declares its own roles; that is what this header is for.
 *
 * THE OBLIGATION THAT COMES WITH A ROLE. Declaring a role makes the AT offer
 * that role's actions, and on a hand-painted widget only you can perform them.
 * They arrive as ordinary key events:
 *
 *   | role                                              | keys          |
 *   | ------------------------------------------------- | ------------- |
 *   | button / toggleButton / checkbox / radioButton     | `NEUI_KEY_SPACE` |
 *   | slider                                            | LEFT / RIGHT  |
 *
 * Declare an actionable role and ignore those keys and the AT offers an action
 * that does nothing, which is worse than not declaring it. Here that means such
 * a component must also name interfaces::KeyboardEvents.
 *
 * @warning @ref Role::none removes the node AND ITS WHOLE SUBTREE. It means
 * "decoration", not "unimportant" - putting it on a container panel takes every
 * control inside it out of the tree too. A container left alone reports
 * `ROLE_GROUP`, which is exactly right for it.
 *
 * The roles below are the subset a custom audio widget set actually needs; the
 * full enum in `<neui/d/a11y.h>` covers lists, trees, tables, tabs and menus,
 * which are neui's own widgets' business rather than ours.
 */

namespace neuiplusplus
{

/** @brief What a component announces itself as. @see the obligation note above. */
enum class Role
{
    derived = NEUI_A11Y_ROLE_DEFAULT, ///< let the framework decide
    none = NEUI_A11Y_ROLE_NONE,       ///< decorative: hide this node AND its subtree
    group = NEUI_A11Y_ROLE_GROUP,
    staticText = NEUI_A11Y_ROLE_STATIC_TEXT,
    button = NEUI_A11Y_ROLE_BUTTON,
    toggleButton = NEUI_A11Y_ROLE_TOGGLE_BUTTON,
    checkbox = NEUI_A11Y_ROLE_CHECKBOX,
    slider = NEUI_A11Y_ROLE_SLIDER,
    meter = NEUI_A11Y_ROLE_METER, ///< read-only measured level (VU, gain)
    progress = NEUI_A11Y_ROLE_PROGRESS,
    image = NEUI_A11Y_ROLE_IMAGE
};

/**
 * @brief What changed, for ComponentCore::notifyAccessible.
 *
 * neui raises these itself for its own widgets; a hand-painted control whose
 * value lives in client state has to say so.
 */
enum class A11yChange
{
    value = NEUI_A11Y_CHANGE_VALUE,
    name = NEUI_A11Y_CHANGE_NAME,
    state = NEUI_A11Y_CHANGE_STATE,
    structure = NEUI_A11Y_CHANGE_STRUCTURE,
    selection = NEUI_A11Y_CHANGE_SELECTION
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_A11Y_H
