#include "icons.h"

#include <QDebug>
#include <QIcon>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

void setupIconTheme()
{
    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));
    const QString current = QIcon::themeName();
    if (!current.isEmpty() && current != QLatin1String("hicolor"))
        return;

    QString name;
    // 1. GTK 3/4: Manjaro Xfce writes the theme there too
    const QString cfg = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    for (const char *f : {"/gtk-3.0/settings.ini", "/gtk-4.0/settings.ini"}) {
        QSettings s(cfg + QLatin1String(f), QSettings::IniFormat);
        name = s.value(QStringLiteral("Settings/gtk-icon-theme-name")).toString();
        if (!name.isEmpty())
            break;
    }
    // 2. Xfce (xsettings) and GNOME
    auto run = [](const QString &prog, const QStringList &args) {
        if (QStandardPaths::findExecutable(prog).isEmpty())
            return QString();
        QProcess p;
        p.start(prog, args);
        if (!p.waitForFinished(1500))
            return QString();
        QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        out.remove('\'');
        return out;
    };
    if (name.isEmpty())
        name = run(QStringLiteral("xfconf-query"), {QStringLiteral("-c"), QStringLiteral("xsettings"),
                                                    QStringLiteral("-p"), QStringLiteral("/Net/IconThemeName")});
    if (name.isEmpty())
        name = run(QStringLiteral("gsettings"), {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
                                                 QStringLiteral("icon-theme")});
    if (!name.isEmpty()) {
        QIcon::setThemeName(name);
        qInfo().noquote() << "icon theme:" << name;
    }
}

