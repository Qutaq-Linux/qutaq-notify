#pragma once

#include <QImage>
#include <QString>
#include <QVariantMap>

// One notification as received through org.freedesktop.Notifications.Notify
struct Notification {
    enum Urgency { Low = 0, Normal = 1, Critical = 2 };

    uint id = 0;
    QString appName;
    QString summary;
    QString body;      // plain text (markup removed)
    QImage image;      // image-data / image-path / app_icon, already resolved
    QString iconName;  // app_icon as sent (for the history)
    QString desktopEntry; // hint desktop-entry: the program to open from the messaging menu
    Urgency urgency = Normal;
    int expireTimeout = -1;  // as sent; NotifyOSD-style bubbles choose their own duration

    // Unity / NotifyOSD extensions
    QString synchronous;     // x-canonical-private-synchronous: "volume", "brightness"… (gauge bubbles)
    int value = -1;          // hint "value" (0…100) for a gauge
    bool append = false;     // x-canonical-append: add the body to an open bubble of the same app and summary
    bool iconOnly = false;   // x-canonical-private-icon-only
    bool transient = false;  // hint transient: not kept in the history

    bool isGauge() const { return !synchronous.isEmpty() && value >= 0; }

    // Build from the Notify() arguments
    static Notification fromDBus(const QString &appName, const QString &appIcon, const QString &summary,
                                 const QString &body, const QVariantMap &hints, int expireTimeout);
};

// body-markup → plain text: <b>, <i>, <a>, &amp; and other entities. Line breaks are kept.
QString plainFromMarkup(const QString &markup);
// A theme icon name, an absolute path or a file:// URI → an image of about `size` px
QImage imageFromIconString(const QString &icon, int size);
