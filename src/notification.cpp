#include "notification.h"

#include <QDBusArgument>
#include <QFileInfo>
#include <QIcon>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QUrl>

namespace {
constexpr int kImageSize = 96; // drawn at 48 px; keep enough for HiDPI

// image-data / image_data / icon_data: (iiibiiay) width, height, rowstride, has_alpha,
// bits_per_sample, channels, data
QImage imageFromData(const QVariant &v)
{
    if (!v.canConvert<QDBusArgument>())
        return {};
    const QDBusArgument arg = v.value<QDBusArgument>();
    int width = 0, height = 0, rowstride = 0, bits = 0, channels = 0;
    bool alpha = false;
    QByteArray data;
    arg.beginStructure();
    arg >> width >> height >> rowstride >> alpha >> bits >> channels >> data;
    arg.endStructure();
    if (width <= 0 || height <= 0 || bits != 8 || (channels != 3 && channels != 4)
        || rowstride < width * channels || data.size() < qsizetype(rowstride) * (height - 1) + width * channels)
        return {};
    const QImage::Format format = channels == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    // copy(): the image must not point into `data`
    return QImage(reinterpret_cast<const uchar *>(data.constData()), width, height, rowstride, format).copy();
}
} // namespace

QString plainFromMarkup(const QString &markup)
{
    if (!markup.contains('<') && !markup.contains('&'))
        return markup;
    // Keep line breaks: Qt's HTML parser would fold "\n" into spaces
    QString html = markup;
    html.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    html.replace('\n', QLatin1String("<br>"));
    // No remote content: drop <img> (NotifyOSD didn't show them either)
    html.remove(QRegularExpression(QStringLiteral("<img[^>]*>"), QRegularExpression::CaseInsensitiveOption));
    return QTextDocumentFragment::fromHtml(html).toPlainText().trimmed();
}

QImage imageFromIconString(const QString &icon, int size)
{
    if (icon.isEmpty())
        return {};
    QString path;
    if (icon.startsWith(QLatin1String("file://")))
        path = QUrl(icon).toLocalFile();
    else if (icon.startsWith('/'))
        path = icon;
    if (!path.isEmpty()) {
        QImage img(path);
        if (!img.isNull() && (img.width() > size || img.height() > size))
            img = img.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return img;
    }
    // NotifyOSD's own names ("notification-audio-volume-high") or the plain ones: whichever the
    // theme has in the larger size (Humanity has some notification-* icons only at 22 px)
    QIcon best;
    int bestSize = -1;
    for (const QString &name : {QStringLiteral("notification-") + icon, icon}) {
        const QIcon candidate = QIcon::fromTheme(name);
        if (candidate.isNull())
            continue;
        int largest = 0;
        for (const QSize &s : candidate.availableSizes())
            largest = qMax(largest, s.width());
        if (candidate.availableSizes().isEmpty())
            largest = size; // scalable (SVG)
        if (largest > bestSize) {
            best = candidate;
            bestSize = largest;
        }
    }
    if (best.isNull())
        return {};
    return best.pixmap(size, size).toImage();
}

Notification Notification::fromDBus(const QString &appName, const QString &appIcon, const QString &summary,
                                    const QString &body, const QVariantMap &hints, int expireTimeout)
{
    Notification n;
    n.appName = appName;
    n.summary = plainFromMarkup(summary); // some send markup in the summary too
    n.body = plainFromMarkup(body);
    n.expireTimeout = expireTimeout;

    auto hint = [&hints](std::initializer_list<const char *> names) {
        for (const char *name : names) {
            const auto it = hints.constFind(QLatin1String(name));
            if (it != hints.cend())
                return it.value();
        }
        return QVariant();
    };
    const QVariant urgency = hint({"urgency"});
    if (urgency.isValid())
        n.urgency = Urgency(qBound(0, urgency.toInt(), 2));

    n.synchronous = hint({"x-canonical-private-synchronous", "synchronous", "private-synchronous"}).toString();
    const QVariant value = hint({"value"});
    if (value.isValid())
        n.value = qMax(0, value.toInt());
    const QVariant append = hint({"x-canonical-append", "append"});
    n.append = append.isValid() && (append.toString() == QLatin1String("true") || append.toString() == QLatin1String("allowed")
                                    || append.toBool());
    n.iconOnly = hint({"x-canonical-private-icon-only", "private-icon-only"}).isValid();
    n.transient = hint({"transient"}).toBool();
    n.desktopEntry = hint({"desktop-entry"}).toString();
    n.iconName = appIcon;

    // Image priority as in the specification: image-data, image-path, app_icon, icon_data
    n.image = imageFromData(hint({"image-data", "image_data"}));
    if (n.image.isNull())
        n.image = imageFromIconString(hint({"image-path", "image_path"}).toString(), kImageSize);
    if (n.image.isNull())
        n.image = imageFromIconString(appIcon, kImageSize);
    if (n.image.isNull())
        n.image = imageFromData(hint({"icon_data"}));
    if (n.image.isNull() && !hint({"desktop-entry"}).toString().isEmpty())
        n.image = imageFromIconString(hint({"desktop-entry"}).toString(), kImageSize);
    if (!n.image.isNull() && (n.image.width() > kImageSize || n.image.height() > kImageSize))
        n.image = n.image.scaled(kImageSize, kImageSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return n;
}
