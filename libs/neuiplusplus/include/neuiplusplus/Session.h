/*
 * neuiplusplus - a C++20 skin over the neui C API
 * SPDX-License-Identifier: MIT
 */

#ifndef NEUIPLUSPLUS_SESSION_H
#define NEUIPLUSPLUS_SESSION_H

#include <memory>
#include <vector>

#include <neui/neui.h>

#include "FileDialog.h"
#include "detail/ComponentImpl.h"
#include "draw/Geometry.h"

/**
 * @file
 * @brief @ref neuiplusplus::Session - one neui session and everything hung off it.
 *
 * A Session owns the neui handles, the widget-id -> component dispatch table,
 * and the user zoom. One per UI; a Frame and every component under it belong to
 * exactly one.
 */

namespace neuiplusplus
{

class ComponentCore;

/**
 * @brief A neui session: handles, dispatch table, zoom.
 *
 * THE DISPATCH TABLE is a flat vector indexed by the LOW 16 BITS of the widget
 * id. neui packs the owning session in the high half and a dense tree slot in
 * the low half, so lookup is an array index rather than a hash. Slots are reused
 * after a destroy, so entries are cleared on the way out and every lookup
 * re-checks the id - neui does not itself detect a stale id across a reuse.
 */
class Session
{
  public:
    /**
     * @brief Create a session on @p host.
     * @param host from `neui_get_api()`. For a plugin this should be
     *        `"neui.host.crossplatform"`: it is the only host implementing
     *        `NEUI_API_EMBED`, and the only one that resolves cursors, timers,
     *        the relative pointer and accessibility. On Windows and macOS
     *        `neui_get_api(NULL)` returns the NATIVE host, which has none of
     *        those.
     * @return null if @p host is null or the session could not be created.
     */
    static std::unique_ptr<Session> create(neui_api_t *host);
    ~Session();

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    /// @name neui handles
    /// The optional interfaces are null on a host that does not implement them.
    /// Feature-detect rather than assume - that is what these are for.
    /// @{
    neui_session_t raw() const { return sess_; }
    neui_widget_api_t *widgets() const { return widgets_; }
    neui_attr_api_t *attrs() const { return attrs_; }
    neui_pointer_api_t *pointer() const { return pointer_; }
    neui_notify_api_t *notify() const { return notify_; }
    neui_a11y_api_t *a11y() const { return a11y_; }
    /// @}

    /**
     * @brief The user zoom.
     *
     * Design units are multiplied by this on the way out to neui and divided on
     * the way in, so nothing above the boundary sees it. Setting it re-applies
     * every component's bounds at the new scale.
     */
    float zoom() const { return zoom_; }
    void setZoom(float z);

    /// @name Modal file dialogs
    /// These BLOCK until the user confirms or cancels. @p ownerFrame must be a
    /// Frame - neui anchors the dialog to a top-level window.
    /// @{
    FileDialogResult openFile(ComponentCore &ownerFrame, const FileDialogOptions &);
    FileDialogResult saveFile(ComponentCore &ownerFrame, const FileDialogOptions &);
    /** @brief False means this host has no file-dialog surface - the one case
     *         where offering your own path entry instead makes sense. */
    bool hasFileDialog() const;
    /// @}

  private:
    friend class ComponentCore;
    Session() = default;

    void bind(neui_widget_t w, const detail::Bindings &b);
    void unbind(neui_widget_t w);
    detail::Bindings *lookup(neui_widget_t w);

    static bool NEUI_ABI dispatch(void *token, neui_event_t *event);
    static void *NEUI_ABI clientInterface(void *token, const char *iface);

    neui_api_t *host_{nullptr};
    neui_session_t sess_{};
    neui_widget_api_t *widgets_{nullptr};
    neui_attr_api_t *attrs_{nullptr};
    neui_pointer_api_t *pointer_{nullptr};
    neui_notify_api_t *notify_{nullptr};
    neui_a11y_api_t *a11y_{nullptr};
    float zoom_{1.0f};
    std::vector<detail::Bindings> table_;

    neui_client_t client_{};
    neui_widget_client_t widgetClient_{};

    // Drag latching. Hosts have disagreed about buttonmap on MOUSE_MOVE, so
    // "am I dragging" is tracked here from DOWN/UP rather than read off the wire.
    neui_widget_t pressed_{widget_none};
    Point downPos_{};
};

} // namespace neuiplusplus

#endif // NEUIPLUSPLUS_SESSION_H
