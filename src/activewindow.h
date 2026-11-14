#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QWaylandClientExtensionTemplate>

#include "qwayland-wlr-foreign-toplevel-management-unstable-v1.h"

class QScreen;

// One window, only for its outputs and whether it is active
class TrackedToplevel : public QObject, public QtWayland::zwlr_foreign_toplevel_handle_v1
{
    Q_OBJECT
public:
    explicit TrackedToplevel(::zwlr_foreign_toplevel_handle_v1 *h) : QtWayland::zwlr_foreign_toplevel_handle_v1(h) {}
    ~TrackedToplevel() override { destroy(); }

    bool activated = false;
    QList<::wl_output *> outputs;

Q_SIGNALS:
    void closed();
    void activatedChanged();

protected:
    void zwlr_foreign_toplevel_handle_v1_state(wl_array *state) override;
    void zwlr_foreign_toplevel_handle_v1_output_enter(::wl_output *o) override { outputs.removeAll(o); outputs << o; }
    void zwlr_foreign_toplevel_handle_v1_output_leave(::wl_output *o) override { outputs.removeAll(o); }
    void zwlr_foreign_toplevel_handle_v1_closed() override { Q_EMIT closed(); }
};

// Which monitor the active window is on (monitor=active, like NotifyOSD's "focus-follow")
class ActiveWindowTracker : public QWaylandClientExtensionTemplate<ActiveWindowTracker>,
                            public QtWayland::zwlr_foreign_toplevel_manager_v1
{
    Q_OBJECT
public:
    ActiveWindowTracker();
    QScreen *activeScreen() const; // nullptr if unknown (no active window)

protected:
    void zwlr_foreign_toplevel_manager_v1_toplevel(::zwlr_foreign_toplevel_handle_v1 *h) override;

private:
    QList<TrackedToplevel *> m_toplevels;
    TrackedToplevel *m_lastActive = nullptr;
};
