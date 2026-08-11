/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_FILEDIALOG_H
#define NEUIPLUSPLUS_FILEDIALOG_H

#include <cstddef>
#include <string>
#include <vector>

/**
 * @file
 * @brief File open / save dialog descriptions, over `NEUI_API_NOTIFY`.
 *
 * The C API delivers results through a per-path callback that fires before the
 * call returns, and hands out a UTF-8 pointer valid only for the duration of
 * that callback. Both are exactly the kind of thing this layer exists to absorb:
 * here you get a vector of `std::string` back.
 *
 * The dialogs are MODAL and block the calling thread, which is why
 * Session::openFile returns a value rather than posting an event. Call them from
 * an input handler.
 */

namespace neuiplusplus
{

/**
 * @brief One entry in a dialog's file-type dropdown.
 * @param patterns semicolon-separated glob list, e.g. `"*.wav;*.aiff"`.
 *        `"*"` (or `"*.*"`) is the conventional "every file" escape hatch.
 */
struct FileFilter
{
    std::string label;
    std::string patterns;
};

/** @brief Everything a dialog can be told. Every field has a sane empty default. */
struct FileDialogOptions
{
    std::string title;       ///< empty = host default caption
    std::string initialDir;  ///< empty = host default / last used
    std::string initialName; ///< save only; ignored by openFile
    std::vector<FileFilter> filters;
    std::size_t defaultFilter{0}; ///< index into @ref filters; out of range clamps to 0
    bool multiSelect{false};      ///< open only
    bool showHidden{false};       ///< a hint - native dialogs may ignore it
    bool confirmOverwrite{true};  ///< save only
};

/**
 * @brief What came back.
 *
 * Distinguishes "the user cancelled" from "this host has no file dialog", which
 * the C API deliberately separates (0 vs -1) because a client that wants to
 * offer its own path entry as a fallback should do so only in the second case.
 */
struct FileDialogResult
{
    std::vector<std::string> paths;
    bool supported{true};

    bool cancelled() const { return supported && paths.empty(); }
    explicit operator bool() const { return !paths.empty(); }
    const std::string &first() const { return paths.front(); }
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_FILEDIALOG_H
