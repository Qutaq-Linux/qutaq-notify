#include "icons.h"
#include "osd.h"
#include "server.h"
#include "settings.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDebug>

#include <cstdio>
#include <memory>

static void usage()
{
    printf("Usage: qutaq-notify [--replace]\n"
           "       qutaq-notify --volume up|down|mute [STEP]\n"
           "       qutaq-notify --brightness up|down [STEP]\n"
           "       qutaq-notify --osd volume|brightness|NAME ICON VALUE\n"
           "       qutaq-notify --dnd on|off|toggle|status\n"
           "\n"
           "Without options: the notification daemon (org.freedesktop.Notifications), bubbles as in Unity.\n"
           "  --replace      take over from another notification daemon (if it allows that)\n"
           "  --volume       change the volume (wpctl or pactl) and show it; STEP in %% (5)\n"
           "  --brightness   change the screen brightness (brightnessctl) and show it; STEP in %% (5)\n"
           "  --osd          show a gauge bubble, VALUE 0…100\n"
           "  --dnd          \"Do not disturb\": no bubbles, notifications go to the messaging menu\n"
           "\n"
           "Settings: ~/.config/qutaq-notify/notify.conf (position, width, scale, monitor, duration, dnd).\n");
}

int main(int argc, char *argv[])
{
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    if (args.contains(QLatin1String("--help")) || args.contains(QLatin1String("-h"))) {
        usage();
        return 0;
    }

    // "Do not disturb" from the command line (or a keybinding): through the daemon, or, if it
    // isn't running, straight into notify.conf
    const int dnd = int(args.indexOf(QLatin1String("--dnd")));
    if (dnd >= 0) {
        QCoreApplication app(argc, argv);
        const QString what = args.value(dnd + 1, QStringLiteral("status"));
        const QString iface = QStringLiteral("org.qutaq.Notify");
        auto call = [&](const QString &method, const QVariantList &a = {}) {
            QDBusMessage m = QDBusMessage::createMethodCall(iface, QStringLiteral("/org/qutaq/Notify"), iface, method);
            m.setArguments(a);
            return QDBusConnection::sessionBus().call(m, QDBus::Block, 2000);
        };
        bool on = false;
        QDBusMessage r;
        if (what == QLatin1String("on") || what == QLatin1String("off"))
            r = call(QStringLiteral("SetDoNotDisturb"), {what == QLatin1String("on")});
        else if (what == QLatin1String("toggle"))
            r = call(QStringLiteral("ToggleDoNotDisturb"));
        else if (what == QLatin1String("status"))
            r = call(QStringLiteral("DoNotDisturb"));
        else {
            usage();
            return 2;
        }
        if (r.type() == QDBusMessage::ErrorMessage) {
            // No daemon: change the setting, it applies when qutaq-notify starts
            on = notifySetting(QStringLiteral("dnd"), false).toBool();
            if (what == QLatin1String("toggle"))
                on = !on;
            else if (what != QLatin1String("status"))
                on = what == QLatin1String("on");
            if (what != QLatin1String("status"))
                notifySettingSet(QStringLiteral("dnd"), on);
        } else {
            if (what == QLatin1String("on") || what == QLatin1String("off"))
                on = what == QLatin1String("on");
            else if (!r.arguments().isEmpty())
                on = r.arguments().first().toBool();
        }
        printf("%s\n", on ? "on" : "off");
        return 0;
    }

    // Key helpers: no window of their own, they talk to the daemon over D-Bus
    const int vol = int(args.indexOf(QLatin1String("--volume")));
    const int bri = int(args.indexOf(QLatin1String("--brightness")));
    const int osd = int(args.indexOf(QLatin1String("--osd")));
    if (vol >= 0 || bri >= 0 || osd >= 0) {
        QCoreApplication app(argc, argv);
        if (osd >= 0) {
            if (osd + 3 >= args.size()) {
                usage();
                return 2;
            }
            return Osd::show(args.at(osd + 1), args.at(osd + 2), args.at(osd + 3).toInt());
        }
        const int at = vol >= 0 ? vol : bri;
        const QString action = args.value(at + 1);
        if (action != QLatin1String("up") && action != QLatin1String("down") && action != QLatin1String("mute")) {
            usage();
            return 2;
        }
        bool ok = false;
        int step = args.value(at + 2).toInt(&ok);
        if (!ok || step <= 0)
            step = 5;
        return vol >= 0 ? Osd::volume(action, step) : Osd::brightness(action, step);
    }

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("qutaq-notify"));
    app.setQuitOnLastWindowClosed(false);
    setupIconTheme();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        fprintf(stderr, "qutaq-notify: no D-Bus session bus\n");
        return 1;
    }
    NotificationServer server;
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"), &server,
                            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
        fprintf(stderr, "qutaq-notify: could not export /org/freedesktop/Notifications\n");
        return 1;
    }
    QutaqNotifyBus qutaqBus(&server);
    bus.registerObject(QStringLiteral("/org/qutaq/Notify"), &qutaqBus,
                       QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
    bus.registerService(QStringLiteral("org.qutaq.Notify"));
    const bool replace = args.contains(QLatin1String("--replace"));
    const auto reply = bus.interface()->registerService(
        QStringLiteral("org.freedesktop.Notifications"),
        replace ? QDBusConnectionInterface::ReplaceExistingService : QDBusConnectionInterface::DontQueueService,
        QDBusConnectionInterface::AllowReplacement);
    if (!reply.isValid() || reply.value() != QDBusConnectionInterface::ServiceRegistered) {
        fprintf(stderr, "qutaq-notify: another notification daemon is running (mako, dunst, xfce4-notifyd…).\n"
                        "Stop it, or start qutaq-notify --replace.\n");
        return 1;
    }
    // Another daemon took over with --replace: leave quietly
    const QString me = bus.baseService();
    auto *watcher = new QDBusServiceWatcher(QStringLiteral("org.freedesktop.Notifications"), bus,
                                            QDBusServiceWatcher::WatchForOwnerChange, &app);
    QObject::connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, &app,
                     [me](const QString &, const QString &oldOwner, const QString &newOwner) {
                         if (oldOwner == me && newOwner != me) {
                             qInfo() << "qutaq-notify: replaced by another daemon";
                             QCoreApplication::quit();
                         }
                     });
    qInfo() << "qutaq-notify: ready";
    return app.exec();
}
