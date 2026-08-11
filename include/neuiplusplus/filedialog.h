/*
 * neuiplusplus - a C++20 skin over the neui C API
 *
 * File open / save dialogs, over NEUI_API_NOTIFY's open_file / save_file.
 *
 * The C API delivers results through a per-path callback that fires before the
 * call returns, and hands out a UTF-8 pointer valid only for the duration of
 * that callback. Both are exactly the kind of thing this layer exists to
 * absorb: here you get a vector of std::string back.
 *
 * The dialogs are MODAL and block the calling thread, which is why they return
 * a value rather than posting an event. Call them from an input handler.
 */

#ifndef NEUIPLUSPLUS_FILEDIALOG_H
#define NEUIPLUSPLUS_FILEDIALOG_H

#include <string>
#include <vector>

namespace neuiplusplus
{

// `patterns` is a semicolon-separated glob list, e.g. "*.wav;*.aiff".
// "*" (or "*.*") is the conventional "every file" escape hatch.
struct FileFilter
{
    std::string label;
    std::string patterns;
};

struct FileDialogOptions
{
    std::string title;      // empty = host default caption
    std::string initialDir; // empty = host default / last used
    std::string initialName;// save only; ignored by openFile
    std::vector<FileFilter> filters;
    std::size_t defaultFilter{0}; // index into filters; out of range clamps to 0
    bool multiSelect{false};      // open only
    bool showHidden{false};       // a hint - native dialogs may ignore it
    bool confirmOverwrite{true};  // save only
};

// Distinguishes "the user cancelled" from "this host has no file dialog",
// which the C API deliberately separates (0 vs -1) because a client that wants
// to offer its own path entry as a fallback should do so only in the second
// case.
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
