#pragma once

#include "notification.h"

#include <QDateTime>
#include <QList>
#include <QObject>

// The notifications kept for the messaging menu on the panel: all of them except volume,
// brightness and transient ones, the 50 most recent, also those that came while
// "Do not disturb" was on. Saved in ~/.local/share/qutaq-notify/history.json.
struct HistoryEntry {
    uint id = 0;
    QString appName;
    QString desktopEntry;
    QString iconName;
    QString summary;
    QString body;
    QImage image;        // kept small (48 px)
    QDateTime time;
    bool unread = true;
    bool suppressed = false; // came during "Do not disturb": never shown as a bubble
};

class History : public QObject
{
    Q_OBJECT
public:
    explicit History(QObject *parent = nullptr);

    void add(const Notification &n, bool suppressed);
    void update(const Notification &n); // replaces_id / append: the same entry
    const QList<HistoryEntry> &entries() const { return m_entries; }
    int unreadCount() const;
    void markAllRead();
    void remove(uint id);
    void clear();

Q_SIGNALS:
    void changed();

private:
    void load();
    void save() const;

    QList<HistoryEntry> m_entries; // newest first
};
