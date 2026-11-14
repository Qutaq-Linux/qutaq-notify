#include "server.h"

#include "activewindow.h"
#include "bubble.h"
#include "history.h"
#include "settings.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

namespace {
constexpr int kStackGap = 8;
}

NotificationServer::NotificationServer(QObject *parent)
    : QObject(parent)
    , m_history(new History(this))
    , m_tracker(new ActiveWindowTracker)
{
    m_tracker->setParent(this);
    // Ids go on from the history, so that a new notification never takes an old one's id
    for (const HistoryEntry &e : m_history->entries())
        m_nextId = qMax(m_nextId, e.id + 1);

    m_dnd = notifySetting(QStringLiteral("dnd"), false).toBool();
    // notify.conf edited by hand, by a settings tool or by --dnd: apply it. Editors replace
    // the file, so watch its folder too.
    const QString conf = notifySettings().fileName();
    QDir().mkpath(QFileInfo(conf).absolutePath());
    auto *watcher = new QFileSystemWatcher(this);
    watcher->addPath(QFileInfo(conf).absolutePath());
    if (QFileInfo::exists(conf))
        watcher->addPath(conf);
    auto changed = [this, watcher, conf] {
        if (QFileInfo::exists(conf) && !watcher->files().contains(conf))
            watcher->addPath(conf);
        reloadSettings();
    };
    connect(watcher, &QFileSystemWatcher::fileChanged, this, changed);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, changed);
}

void NotificationServer::reloadSettings()
{
    const bool dnd = notifySetting(QStringLiteral("dnd"), false).toBool();
    if (dnd == m_dnd)
        return;
    m_dnd = dnd;
    qInfo() << "do not disturb:" << (dnd ? "on" : "off");
    if (dnd) {
        // What was waiting goes to the messaging menu instead
        for (int i = int(m_queue.size()) - 1; i >= 0; --i)
            if (suppress(m_queue.at(i))) {
                const Notification n = m_queue.takeAt(i);
                if (!n.transient)
                    m_history->add(n, true);
            }
    }
    Q_EMIT doNotDisturbChanged(dnd);
}

void NotificationServer::setDoNotDisturb(bool on)
{
    notifySettingSet(QStringLiteral("dnd"), on);
    reloadSettings(); // at once; the file watcher would do it a moment later
}

bool NotificationServer::suppress(const Notification &n) const
{
    if (!m_dnd || n.isGauge() || !n.synchronous.isEmpty())
        return false; // volume and brightness: the user pressed the key
    if (n.urgency == Notification::Critical && notifySetting(QStringLiteral("dndCritical"), true).toBool())
        return false;
    return true;
}

// monitor = active (where the active window is), primary, or a monitor name
QScreen *NotificationServer::targetScreen() const
{
    const QString monitor = notifySetting(QStringLiteral("monitor"), QStringLiteral("active")).toString();
    if (monitor == QLatin1String("active")) {
        if (QScreen *s = m_tracker->activeScreen())
            return s;
    } else if (monitor != QLatin1String("primary")) {
        for (QScreen *s : QGuiApplication::screens())
            if (s->name() == monitor)
                return s;
    }
    return QGuiApplication::primaryScreen();
}

QStringList NotificationServer::GetCapabilities()
{
    // As NotifyOSD: no "actions", so programs don't expect buttons. "body-markup": we accept it
    // and show it as plain text.
    return {QStringLiteral("body"),
            QStringLiteral("body-markup"),
            QStringLiteral("icon-static"),
            QStringLiteral("image/svg+xml"),
            QStringLiteral("x-canonical-private-synchronous"),
            QStringLiteral("x-canonical-append"),
            QStringLiteral("x-canonical-private-icon-only"),
            QStringLiteral("x-canonical-truncation"),
            QStringLiteral("private-synchronous"),
            QStringLiteral("append"),
            QStringLiteral("private-icon-only"),
            QStringLiteral("truncation")};
}

QString NotificationServer::GetServerInformation(QString &vendor, QString &version, QString &spec_version)
{
    vendor = QStringLiteral("Qutaq");
    version = QStringLiteral("0.1");
    spec_version = QStringLiteral("1.2");
    return QStringLiteral("qutaq-notify");
}

Bubble *NotificationServer::createBubble(const Notification &n)
{
    auto *b = new Bubble(n, targetScreen());
    connect(b, &Bubble::heightChanged, this, &NotificationServer::layoutBubbles);
    connect(b, &Bubble::expired, this, [this, b] {
        Q_EMIT NotificationClosed(b->notification().id, Expired);
    });
    connect(b, &Bubble::finished, this, [this, b] {
        const bool wasCurrent = b == m_current;
        b->deleteLater();
        if (wasCurrent)
            m_current = nullptr;
        if (b == m_gauge)
            m_gauge = nullptr;
        layoutBubbles();
        if (wasCurrent)
            QTimer::singleShot(150, this, &NotificationServer::showNext); // a short pause between bubbles
    });
    return b;
}

void NotificationServer::layoutBubbles()
{
    // The gauge nearest to the screen edge (on top), the normal bubble after it
    const int gaugeH = m_gauge && !m_gauge->isClosing() ? m_gauge->bubbleHeight() + kStackGap : 0;
    if (m_gauge)
        m_gauge->setStackOffset(0);
    if (m_current)
        m_current->setStackOffset(gaugeH);
}

void NotificationServer::showNext()
{
    if (m_current || m_queue.isEmpty())
        return;
    const Notification n = m_queue.takeFirst();
    m_current = createBubble(n);
    layoutBubbles();
    m_current->showBubble();
}

uint NotificationServer::Notify(const QString &app_name, uint replaces_id, const QString &app_icon,
                                const QString &summary, const QString &body, const QStringList &actions,
                                const QVariantMap &hints, int expire_timeout)
{
    Q_UNUSED(actions) // no buttons, as in NotifyOSD
    Notification n = Notification::fromDBus(app_name, app_icon, summary, body, hints, expire_timeout);
    qInfo().noquote() << "notify:" << app_name << "|" << n.summary << "|" << n.body.left(60).replace('\n', ' ')
                      << (n.isGauge() ? QStringLiteral("| gauge %1 %2").arg(n.synchronous).arg(n.value) : QString());

    // Gauges: at once, replacing the previous one, whatever its id
    if (!n.synchronous.isEmpty()) {
        if (m_gauge && !m_gauge->isClosing()) {
            n.id = m_gauge->notification().id;
            m_gauge->update(n);
        } else {
            n.id = replaces_id ? replaces_id : m_nextId++;
            m_gauge = createBubble(n);
            m_gauge->showBubble();
        }
        layoutBubbles();
        return n.id;
    }

    // replaces_id: the bubble on screen or the one waiting
    if (replaces_id) {
        n.id = replaces_id;
        if (!n.transient)
            m_history->update(n);
        if (m_current && !m_current->isClosing() && m_current->notification().id == replaces_id) {
            m_current->update(n);
            return n.id;
        }
        for (Notification &q : m_queue)
            if (q.id == replaces_id) {
                q = n;
                return n.id;
            }
        if (suppress(n)) // "Do not disturb": the menu has it already, or gets it now
            return n.id;
    } else {
        n.id = m_nextId++;
    }

    // Appending to the open bubble (instant messengers): same program, same summary
    if (n.append && m_current && !m_current->isClosing() && m_current->notification().appName == n.appName
        && m_current->notification().summary == n.summary) {
        m_current->appendBody(n.body);
        if (!n.transient)
            m_history->update(m_current->notification());
        return m_current->notification().id;
    }

    // Everything else is kept for the messaging menu; with "Do not disturb" that is all
    if (!n.transient)
        m_history->add(n, suppress(n));
    if (suppress(n))
        return n.id;

    // Queue: critical ones go before the others, otherwise in order
    int at = int(m_queue.size());
    if (n.urgency == Notification::Critical)
        for (int i = 0; i < m_queue.size(); ++i)
            if (m_queue.at(i).urgency != Notification::Critical) {
                at = i;
                break;
            }
    m_queue.insert(at, n);
    showNext();
    return n.id;
}

void NotificationServer::CloseNotification(uint id)
{
    for (int i = 0; i < m_queue.size(); ++i)
        if (m_queue.at(i).id == id) {
            m_queue.removeAt(i);
            Q_EMIT NotificationClosed(id, ClosedByCall);
            return;
        }
    for (Bubble *b : {m_current.data(), m_gauge.data()})
        if (b && !b->isClosing() && b->notification().id == id) {
            b->closeBubble();
            Q_EMIT NotificationClosed(id, ClosedByCall);
            return;
        }
}

QutaqNotifyBus::QutaqNotifyBus(NotificationServer *server)
    : m_server(server)
{
    connect(server, &NotificationServer::doNotDisturbChanged, this, &QutaqNotifyBus::DoNotDisturbChanged);
}

bool QutaqNotifyBus::DoNotDisturb()
{
    return m_server->doNotDisturb();
}

void QutaqNotifyBus::SetDoNotDisturb(bool on)
{
    m_server->setDoNotDisturb(on);
}

bool QutaqNotifyBus::ToggleDoNotDisturb()
{
    m_server->setDoNotDisturb(!m_server->doNotDisturb());
    return m_server->doNotDisturb();
}
