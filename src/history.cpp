#include "history.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
constexpr int kMaxEntries = 50;

QString historyFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/qutaq-notify/history.json");
}

QString imageToBase64(const QImage &img)
{
    if (img.isNull())
        return {};
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&buf, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}
} // namespace

History::History(QObject *parent)
    : QObject(parent)
{
    load();
}

void History::add(const Notification &n, bool suppressed)
{
    HistoryEntry e;
    e.id = n.id;
    e.appName = n.appName;
    e.desktopEntry = n.desktopEntry;
    e.iconName = n.iconName;
    e.summary = n.summary;
    e.body = n.body;
    e.image = n.image.isNull() ? QImage() : n.image.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    e.time = QDateTime::currentDateTime();
    e.suppressed = suppressed;
    remove(n.id); // an id is reused only by replaces_id
    m_entries.prepend(e);
    while (m_entries.size() > kMaxEntries)
        m_entries.removeLast();
    save();
    Q_EMIT changed();
}

void History::update(const Notification &n)
{
    for (HistoryEntry &e : m_entries)
        if (e.id == n.id) {
            e.summary = n.summary;
            e.body = n.body;
            if (!n.image.isNull())
                e.image = n.image.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            e.time = QDateTime::currentDateTime();
            e.unread = true;
            save();
            Q_EMIT changed();
            return;
        }
}

int History::unreadCount() const
{
    int n = 0;
    for (const HistoryEntry &e : m_entries)
        n += e.unread ? 1 : 0;
    return n;
}

void History::markAllRead()
{
    bool any = false;
    for (HistoryEntry &e : m_entries)
        if (e.unread) {
            e.unread = false;
            any = true;
        }
    if (any) {
        save();
        Q_EMIT changed();
    }
}

void History::remove(uint id)
{
    const auto before = m_entries.size();
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [id](const HistoryEntry &e) { return e.id == id; }),
                    m_entries.end());
    if (m_entries.size() != before) {
        save();
        Q_EMIT changed();
    }
}

void History::clear()
{
    if (m_entries.isEmpty())
        return;
    m_entries.clear();
    save();
    Q_EMIT changed();
}

void History::load()
{
    QFile f(historyFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        HistoryEntry e;
        e.id = uint(o.value(QStringLiteral("id")).toDouble());
        e.appName = o.value(QStringLiteral("app")).toString();
        e.desktopEntry = o.value(QStringLiteral("desktopEntry")).toString();
        e.iconName = o.value(QStringLiteral("icon")).toString();
        e.summary = o.value(QStringLiteral("summary")).toString();
        e.body = o.value(QStringLiteral("body")).toString();
        e.time = QDateTime::fromString(o.value(QStringLiteral("time")).toString(), Qt::ISODate);
        e.unread = o.value(QStringLiteral("unread")).toBool();
        e.suppressed = o.value(QStringLiteral("suppressed")).toBool();
        const QByteArray png = QByteArray::fromBase64(o.value(QStringLiteral("image")).toString().toLatin1());
        if (!png.isEmpty())
            e.image.loadFromData(png, "PNG");
        m_entries << e;
    }
}

void History::save() const
{
    QJsonArray arr;
    for (const HistoryEntry &e : m_entries)
        arr << QJsonObject{{QStringLiteral("id"), double(e.id)},
                           {QStringLiteral("app"), e.appName},
                           {QStringLiteral("desktopEntry"), e.desktopEntry},
                           {QStringLiteral("icon"), e.iconName},
                           {QStringLiteral("summary"), e.summary},
                           {QStringLiteral("body"), e.body},
                           {QStringLiteral("time"), e.time.toString(Qt::ISODate)},
                           {QStringLiteral("unread"), e.unread},
                           {QStringLiteral("suppressed"), e.suppressed},
                           {QStringLiteral("image"), e.iconName.isEmpty() ? imageToBase64(e.image) : QString()}};
    QDir().mkpath(QFileInfo(historyFile()).absolutePath());
    QSaveFile f(historyFile());
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    f.commit();
}
