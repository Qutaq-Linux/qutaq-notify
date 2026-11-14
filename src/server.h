#pragma once

#include "notification.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

class ActiveWindowTracker;
class Bubble;
class History;
class QScreen;

// org.freedesktop.Notifications, the way NotifyOSD did it in Unity:
//   - one bubble at a time; the others wait in a queue (critical ones first);
//   - gauge bubbles (volume, brightness: hint x-canonical-private-synchronous + value) are shown
//     at once, above the current bubble, and replace each other;
//   - x-canonical-append adds the text to an open bubble of the same app and summary (chats);
//   - no action buttons (the "actions" capability isn't offered, so programs fall back to
//     a dialog or nothing); bubbles can't be clicked, they let the pointer through.
class NotificationServer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    explicit NotificationServer(QObject *parent = nullptr);

    enum CloseReason { Expired = 1, Dismissed = 2, ClosedByCall = 3, Undefined = 4 };

    bool doNotDisturb() const { return m_dnd; }
    void setDoNotDisturb(bool on); // saved in notify.conf
    History *history() const { return m_history; }

public Q_SLOTS:
    QStringList GetCapabilities();
    uint Notify(const QString &app_name, uint replaces_id, const QString &app_icon, const QString &summary,
                const QString &body, const QStringList &actions, const QVariantMap &hints, int expire_timeout);
    void CloseNotification(uint id);
    QString GetServerInformation(QString &vendor, QString &version, QString &spec_version);

Q_SIGNALS:
    void NotificationClosed(uint id, uint reason);
    void ActionInvoked(uint id, const QString &action_key);
    void doNotDisturbChanged(bool on);

private:
    void showNext();
    void layoutBubbles();
    Bubble *createBubble(const Notification &n);
    void finishBubble(Bubble *b, CloseReason reason);
    void reloadSettings(); // notify.conf changed
    QScreen *targetScreen() const;
    bool suppress(const Notification &n) const; // "Do not disturb": not shown as a bubble

    uint m_nextId = 1;
    QPointer<Bubble> m_current; // the normal bubble
    QPointer<Bubble> m_gauge;   // the synchronous one, above it
    QList<Notification> m_queue;
    History *m_history;
    ActiveWindowTracker *m_tracker;
    bool m_dnd = false;
};

// org.qutaq.Notify /org/qutaq/Notify: "Do not disturb" for the panel and the command line
//   DoNotDisturb() → b, SetDoNotDisturb(b), ToggleDoNotDisturb() → b; signal DoNotDisturbChanged(b)
class QutaqNotifyBus : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.qutaq.Notify")
public:
    explicit QutaqNotifyBus(NotificationServer *server);

public Q_SLOTS:
    bool DoNotDisturb();
    void SetDoNotDisturb(bool on);
    bool ToggleDoNotDisturb();

Q_SIGNALS:
    void DoNotDisturbChanged(bool on);

private:
    NotificationServer *m_server;
};
