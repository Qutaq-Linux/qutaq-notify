#include "osd.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVariantMap>

#include <cstdio>

namespace {
bool have(const char *program)
{
    return !QStandardPaths::findExecutable(QLatin1String(program)).isEmpty();
}

QString run(const QString &program, const QStringList &args)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(3000) || p.exitStatus() != QProcess::NormalExit)
        return {};
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QString volumeIcon(int value, bool muted)
{
    if (muted || value == 0)
        return QStringLiteral("audio-volume-muted");
    if (value < 34)
        return QStringLiteral("audio-volume-low");
    if (value < 67)
        return QStringLiteral("audio-volume-medium");
    return QStringLiteral("audio-volume-high");
}

QString brightnessIcon(int value)
{
    // NotifyOSD's names (Humanity) are tried first by the daemon ("notification-" + name)
    if (value < 34)
        return QStringLiteral("display-brightness-low");
    if (value < 67)
        return QStringLiteral("display-brightness-medium");
    return QStringLiteral("display-brightness-high");
}
} // namespace

int Osd::show(const QString &kind, const QString &icon, int value)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                      QStringLiteral("/org/freedesktop/Notifications"),
                                                      QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    QVariantMap hints{{QStringLiteral("x-canonical-private-synchronous"), kind},
                      {QStringLiteral("value"), qBound(0, value, 100)},
                      {QStringLiteral("transient"), true}};
    msg << QStringLiteral("qutaq-notify") << uint(0) << icon << kind << QString() << QStringList() << hints << int(-1);
    const QDBusMessage r = QDBusConnection::sessionBus().call(msg, QDBus::Block, 2000);
    if (r.type() == QDBusMessage::ErrorMessage) {
        fprintf(stderr, "qutaq-notify: %s\n", qPrintable(r.errorMessage()));
        return 1;
    }
    return 0;
}

int Osd::volume(const QString &action, int step)
{
    int value = -1;
    bool muted = false;
    if (have("wpctl")) {
        // PipeWire. -l 1.0: don't go above 100 % with the keys
        const QString sink = QStringLiteral("@DEFAULT_AUDIO_SINK@");
        if (action == QLatin1String("up"))
            run(QStringLiteral("wpctl"), {QStringLiteral("set-volume"), QStringLiteral("-l"), QStringLiteral("1.0"), sink,
                                          QStringLiteral("%1%+").arg(step)});
        else if (action == QLatin1String("down"))
            run(QStringLiteral("wpctl"), {QStringLiteral("set-volume"), sink, QStringLiteral("%1%-").arg(step)});
        else if (action == QLatin1String("mute"))
            run(QStringLiteral("wpctl"), {QStringLiteral("set-mute"), sink, QStringLiteral("toggle")});
        if (action == QLatin1String("up") || action == QLatin1String("down")) // a volume key unmutes, as in Unity
            run(QStringLiteral("wpctl"), {QStringLiteral("set-mute"), sink, QStringLiteral("0")});
        // "Volume: 0.45" or "Volume: 0.45 [MUTED]"
        const QString out = run(QStringLiteral("wpctl"), {QStringLiteral("get-volume"), sink});
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("([0-9.]+)")).match(out);
        if (m.hasMatch())
            value = qRound(m.captured(1).toDouble() * 100);
        muted = out.contains(QLatin1String("MUTED"));
    } else if (have("pactl")) {
        // PulseAudio (or PipeWire's pulse server)
        const QString sink = QStringLiteral("@DEFAULT_SINK@");
        if (action == QLatin1String("up") || action == QLatin1String("down")) {
            run(QStringLiteral("pactl"), {QStringLiteral("set-sink-mute"), sink, QStringLiteral("0")});
            run(QStringLiteral("pactl"), {QStringLiteral("set-sink-volume"), sink,
                                          QStringLiteral("%1%2%").arg(action == QLatin1String("up") ? '+' : '-').arg(step)});
        } else if (action == QLatin1String("mute")) {
            run(QStringLiteral("pactl"), {QStringLiteral("set-sink-mute"), sink, QStringLiteral("toggle")});
        }
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("(\\d+)%"))
                                              .match(run(QStringLiteral("pactl"), {QStringLiteral("get-sink-volume"), sink}));
        if (m.hasMatch())
            value = m.captured(1).toInt();
        if (action == QLatin1String("up") && value > 100) { // keep the keys at 100 %
            run(QStringLiteral("pactl"), {QStringLiteral("set-sink-volume"), sink, QStringLiteral("100%")});
            value = 100;
        }
        muted = run(QStringLiteral("pactl"), {QStringLiteral("get-sink-mute"), sink}).contains(QLatin1String("yes"));
    } else {
        fprintf(stderr, "qutaq-notify: neither wpctl (PipeWire) nor pactl found\n");
        return 1;
    }
    if (value < 0) {
        fprintf(stderr, "qutaq-notify: could not read the volume\n");
        return 1;
    }
    return show(QStringLiteral("volume"), volumeIcon(value, muted), muted ? 0 : value);
}

int Osd::brightness(const QString &action, int step)
{
    if (!have("brightnessctl")) {
        fprintf(stderr, "qutaq-notify: brightnessctl not found\n");
        return 1;
    }
    if (action == QLatin1String("up"))
        run(QStringLiteral("brightnessctl"), {QStringLiteral("-q"), QStringLiteral("set"), QStringLiteral("%1%+").arg(step)});
    else if (action == QLatin1String("down"))
        // -n 1%: never all the way to black
        run(QStringLiteral("brightnessctl"), {QStringLiteral("-q"), QStringLiteral("-n"), QStringLiteral("1%"), QStringLiteral("set"),
                                              QStringLiteral("%1%-").arg(step)});
    // -m: "intel_backlight,backlight,500,50%,1000"
    const QStringList f = run(QStringLiteral("brightnessctl"), {QStringLiteral("-m")}).split(',');
    if (f.size() < 4) {
        fprintf(stderr, "qutaq-notify: could not read the brightness\n");
        return 1;
    }
    const int value = QString(f.at(3)).remove('%').toInt();
    return show(QStringLiteral("brightness"), brightnessIcon(value), value);
}
