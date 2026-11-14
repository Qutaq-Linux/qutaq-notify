#include "activewindow.h"

#include <QGuiApplication>
#include <QScreen>
#include <qpa/qplatformnativeinterface.h>

void TrackedToplevel::zwlr_foreign_toplevel_handle_v1_state(wl_array *state)
{
    const bool was = activated;
    activated = false;
    const auto *s = static_cast<const uint32_t *>(state->data);
    for (size_t i = 0; i < state->size / sizeof(uint32_t); ++i)
        if (s[i] == state_activated)
            activated = true;
    if (activated != was)
        Q_EMIT activatedChanged();
}

ActiveWindowTracker::ActiveWindowTracker()
    : QWaylandClientExtensionTemplate<ActiveWindowTracker>(3)
{
    initialize();
}

void ActiveWindowTracker::zwlr_foreign_toplevel_manager_v1_toplevel(::zwlr_foreign_toplevel_handle_v1 *h)
{
    auto *t = new TrackedToplevel(h);
    m_toplevels << t;
    connect(t, &TrackedToplevel::activatedChanged, this, [this, t] {
        if (t->activated)
            m_lastActive = t;
    });
    connect(t, &TrackedToplevel::closed, this, [this, t] {
        m_toplevels.removeAll(t);
        if (m_lastActive == t)
            m_lastActive = nullptr;
        t->deleteLater();
    });
}

QScreen *ActiveWindowTracker::activeScreen() const
{
    // The active window; while nothing is (a layer surface has the keyboard), the last one
    TrackedToplevel *active = m_lastActive;
    for (TrackedToplevel *t : m_toplevels)
        if (t->activated)
            active = t;
    if (!active || active->outputs.isEmpty())
        return nullptr;
    // Qt's screens know their wl_output
    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    if (!native)
        return nullptr;
    for (QScreen *screen : QGuiApplication::screens())
        if (native->nativeResourceForScreen("output", screen) == active->outputs.last())
            return screen;
    return nullptr;
}
