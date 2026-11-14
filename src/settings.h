#pragma once

#include <QSettings>
#include <QStandardPaths>

// ~/.config/qutaq-notify/notify.conf, read on every access: edits apply at once (the daemon
// watches the file). Keys are in the [notify] group:
//   position = top-right | top-left | top-center | bottom-right | bottom-left | bottom-center
//   width    = bubble width in px (300)
//   scale    = text and icon size (1.0)
//   monitor  = active (where the active window is) | primary | a monitor name (DP-1, HDMI-A-1…)
//   duration = how long bubbles stay, as a factor (1.0)
//   dnd      = true while "Do not disturb" is on
//   dndCritical = true: critical notifications (low battery…) show even with "Do not disturb"
inline QSettings &notifySettings()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                           + QStringLiteral("/qutaq-notify/notify.conf"),
                       QSettings::IniFormat);
    return s;
}

inline QVariant notifySetting(const QString &key, const QVariant &def = QVariant())
{
    notifySettings().sync();
    return notifySettings().value(QStringLiteral("notify/") + key, def);
}

inline void notifySettingSet(const QString &key, const QVariant &value)
{
    notifySettings().setValue(QStringLiteral("notify/") + key, value);
    notifySettings().sync();
}
