#include "bubble.h"

#include "settings.h"

#include <LayerShellQt/Window>

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTextLayout>
#include <QWindow>

namespace {
constexpr int kMargin = 8;       // from the screen edge / the panel
constexpr int kSummaryLines = 2;
constexpr int kBodyLines = 10;   // longer bodies end with "…" (x-canonical-truncation)
constexpr qreal kFadedOpacity = 0.12;
const QColor kBackground(24, 24, 26, 235);
const QColor kSummaryColor(0xff, 0xff, 0xff);
const QColor kBodyColor(0xea, 0xea, 0xec);

// Word-wrap `text` into at most `maxLines` lines of `width` px; the last one ends with "…"
// if something was cut. Explicit line breaks are kept.
QStringList wrap(const QString &text, const QFont &font, int width, int maxLines)
{
    QStringList out;
    const QFontMetrics fm(font);
    const QStringList paragraphs = text.split('\n');
    for (int p = 0; p < paragraphs.size() && out.size() < maxLines; ++p) {
        const QString para = paragraphs.at(p);
        if (para.isEmpty()) {
            out << QString();
            continue;
        }
        QTextLayout layout(para, font);
        QTextOption opt;
        opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(opt);
        layout.beginLayout();
        for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
            line.setLineWidth(width);
            if (out.size() == maxLines - 1) {
                // The last allowed line: the rest of the text, elided
                const QString rest = para.mid(line.textStart());
                const bool more = p + 1 < paragraphs.size() && !paragraphs.mid(p + 1).join(QString()).trimmed().isEmpty();
                QString last = fm.elidedText(rest, Qt::ElideRight, width);
                if (more && !last.endsWith(QChar(0x2026)))
                    last = fm.elidedText(last + QStringLiteral(" …"), Qt::ElideRight, width);
                out << last;
                break;
            }
            out << para.mid(line.textStart(), line.textLength()).trimmed();
        }
        layout.endLayout();
    }
    while (!out.isEmpty() && out.last().isEmpty())
        out.removeLast();
    return out;
}

QFont summaryFont(const QFont &base)
{
    QFont f = base;
    f.setBold(true);
    f.setPointSizeF(base.pointSizeF() * 1.08);
    return f;
}
} // namespace

Bubble::Bubble(const Notification &n, QScreen *screen)
    : m_n(n)
{
    // Size and place from notify.conf (read when the bubble is made)
    const qreal scale = qBound(0.5, notifySetting(QStringLiteral("scale"), 1.0).toDouble(), 3.0);
    m_width = qBound(160, int(notifySetting(QStringLiteral("width"), 300).toInt() * scale), 1200);
    m_pad = qRound(14 * scale);
    m_iconSize = qRound(48 * scale);
    m_gap = qRound(12 * scale);
    m_radius = qRound(10 * scale);
    if (!qFuzzyCompare(scale, 1.0)) {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * scale);
        setFont(f);
    }
    m_position = notifySetting(QStringLiteral("position"), QStringLiteral("top-right")).toString();

    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowFlag(Qt::FramelessWindowHint);
    setWindowFlag(Qt::WindowDoesNotAcceptFocus);

    m_expire.setSingleShot(true);
    connect(&m_expire, &QTimer::timeout, this, [this] {
        Q_EMIT expired();
        closeBubble();
    });

    m_fade.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_opacity = v.toReal();
        QWidget::update();
    });
    connect(&m_fade, &QVariantAnimation::finished, this, [this] {
        if (m_closing)
            Q_EMIT finished();
    });

    // While faded under the pointer, clicks go through to the window below. Then the bubble
    // takes the pointer back for a moment to see whether it is still there.
    m_passThrough.setSingleShot(true);
    connect(&m_passThrough, &QTimer::timeout, this, [this] {
        if (!m_faded || m_closing)
            return;
        m_reentered = false;
        if (windowHandle())
            windowHandle()->setMask(QRegion());
        QTimer::singleShot(300, this, [this] {
            if (!m_faded || m_closing)
                return;
            if (m_reentered) {
                m_passThrough.start(1200);
                return;
            }
            m_faded = false;
            animateOpacity(1.0, 200);
            m_expire.start(qMax(1500, m_remaining));
        });
    });

    relayout();
    setupLayerShell(screen);
}

void Bubble::setupLayerShell(QScreen *screen)
{
    if (QGuiApplication::platformName() != QLatin1String("wayland")) {
        setWindowFlag(Qt::ToolTip); // X11 or others: a frameless always-on-top window
        setScreen(screen);
        applyOffset();
        return;
    }
    setScreen(screen);
    winId();
    LayerShellQt::Window *ls = LayerShellQt::Window::get(windowHandle());
    if (!ls)
        return;
    ls->setScope(QStringLiteral("qutaq-notify"));
    // Overlay: also above full-screen video, like Unity's volume bubble
    ls->setLayer(LayerShellQt::Window::LayerOverlay);
    LayerShellQt::Window::Anchors anchors;
    anchors |= isBottom() ? LayerShellQt::Window::AnchorBottom : LayerShellQt::Window::AnchorTop;
    if (m_position.endsWith(QLatin1String("left")))
        anchors |= LayerShellQt::Window::AnchorLeft;
    else if (m_position.endsWith(QLatin1String("right")) || !m_position.endsWith(QLatin1String("center")))
        anchors |= LayerShellQt::Window::AnchorRight;
    // neither left nor right: centred
    ls->setAnchors(anchors);
    ls->setExclusiveZone(0); // next to the panel and the launcher, not over them
    ls->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    applyOffset();
#if LSQ_VERSION_MAJOR > 6 || (LSQ_VERSION_MAJOR == 6 && LSQ_VERSION_MINOR >= 6)
    ls->setWantsToBeOnActiveScreen(false);
    ls->setScreen(screen);
#endif
}

bool Bubble::isBottom() const
{
    return m_position.startsWith(QLatin1String("bottom"));
}

void Bubble::setStackOffset(int y)
{
    if (y == m_stackOffset)
        return;
    m_stackOffset = y;
    applyOffset();
}

// The distance from the screen edge: the margin plus the bubbles nearer to that edge
void Bubble::applyOffset()
{
    const int edge = kMargin + m_stackOffset;
    if (QGuiApplication::platformName() != QLatin1String("wayland")) {
        if (!screen())
            return;
        const QRect a = screen()->availableGeometry();
        const int x = m_position.endsWith(QLatin1String("left"))     ? a.left() + kMargin
                      : m_position.endsWith(QLatin1String("center")) ? a.center().x() - width() / 2
                                                                     : a.right() - width() - kMargin;
        move(x, isBottom() ? a.bottom() - height() - edge : a.top() + edge);
        return;
    }
    if (windowHandle())
        if (LayerShellQt::Window *ls = LayerShellQt::Window::get(windowHandle()))
            ls->setMargins(QMargins(kMargin, isBottom() ? 0 : edge, kMargin, isBottom() ? edge : 0));
    QWidget::update(); // the new margin takes effect with the next commit
}

void Bubble::relayout()
{
    m_icon = m_n.image;
    const bool hasIcon = !m_icon.isNull();
    const QFont sf = summaryFont(font());
    int h;
    int w = m_width;
    if (m_n.iconOnly && hasIcon) {
        w = m_iconSize + 2 * m_pad + 16;
        h = w;
        m_summaryLines.clear();
        m_bodyLines.clear();
    } else if (m_n.isGauge()) {
        h = m_iconSize + 2 * m_pad;
        m_summaryLines.clear();
        m_bodyLines.clear();
    } else {
        const int textW = m_width - 2 * m_pad - (hasIcon ? m_iconSize + m_gap : 0);
        m_summaryLines = wrap(m_n.summary, sf, textW, kSummaryLines);
        m_bodyLines = wrap(m_n.body, font(), textW, kBodyLines);
        const int textH = int(m_summaryLines.size()) * QFontMetrics(sf).lineSpacing()
                          + (m_summaryLines.isEmpty() || m_bodyLines.isEmpty() ? 0 : 3)
                          + int(m_bodyLines.size()) * QFontMetrics(font()).lineSpacing();
        h = 2 * m_pad + qMax(hasIcon ? m_iconSize : 0, textH);
    }
    const bool changed = size() != QSize(w, h);
    setFixedSize(w, h);
    QWidget::update();
    if (changed)
        Q_EMIT heightChanged();
}

int Bubble::durationMs() const
{
    if (m_n.isGauge() || !m_n.synchronous.isEmpty())
        return 2000;
    // Long enough to read: about 4 s, plus a little for every word beyond a few, at most 12 s
    const int chars = int(m_n.summary.size() + m_n.body.size());
    int ms = 4000 + qMax(0, chars - 30) * 40;
    if (m_n.urgency == Notification::Critical)
        ms += 3000;
    const qreal factor = qBound(0.25, notifySetting(QStringLiteral("duration"), 1.0).toDouble(), 10.0);
    return int(qMin(ms, 12000) * factor);
}

void Bubble::restartTimer()
{
    m_remaining = durationMs();
    if (!m_faded)
        m_expire.start(m_remaining);
}

void Bubble::update(const Notification &n)
{
    m_n = n;
    relayout();
    if (!m_closing)
        restartTimer();
}

void Bubble::appendBody(const QString &body)
{
    m_n.body = m_n.body.isEmpty() ? body : m_n.body + QLatin1Char('\n') + body;
    relayout();
    restartTimer();
}

void Bubble::showBubble()
{
    show();
    animateOpacity(1.0, 180);
    restartTimer();
}

void Bubble::closeBubble()
{
    if (m_closing)
        return;
    m_closing = true;
    m_expire.stop();
    m_passThrough.stop();
    animateOpacity(0.0, 300);
}

void Bubble::animateOpacity(qreal to, int ms)
{
    m_fade.stop();
    m_fade.setStartValue(m_opacity);
    m_fade.setEndValue(to);
    m_fade.setDuration(ms);
    m_fade.start();
}

void Bubble::enterEvent(QEnterEvent *)
{
    if (m_closing)
        return;
    if (m_faded) {
        m_reentered = true;
        return;
    }
    // Under the pointer: almost invisible and click-through, like NotifyOSD. The timer waits.
    m_faded = true;
    m_remaining = m_expire.isActive() ? m_expire.remainingTime() : m_remaining;
    m_expire.stop();
    animateOpacity(kFadedOpacity, 150);
    if (windowHandle())
        windowHandle()->setMask(QRegion(0, 0, 1, 1)); // the transparent corner: input goes below
    m_passThrough.start(1500);
}

void Bubble::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setOpacity(m_opacity);

    QPainterPath bg;
    bg.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), m_radius, m_radius);
    p.fillPath(bg, kBackground);
    p.setPen(QPen(QColor(255, 255, 255, 22), 1));
    p.drawPath(bg);

    const bool hasIcon = !m_icon.isNull();
    QRect iconRect;
    if (hasIcon) {
        const QSize s = m_icon.size().scaled(m_iconSize, m_iconSize, Qt::KeepAspectRatio);
        const int top = m_n.iconOnly || m_n.isGauge() ? (height() - s.height()) / 2 : m_pad + (m_iconSize - s.height()) / 2;
        const int left = m_n.iconOnly ? (width() - s.width()) / 2 : m_pad + (m_iconSize - s.width()) / 2;
        iconRect = QRect(QPoint(left, top), s);
        p.drawImage(iconRect, m_icon);
    }
    if (m_n.iconOnly && hasIcon)
        return;

    const int x = m_pad + (hasIcon ? m_iconSize + m_gap : 0);
    const int textW = width() - x - m_pad;

    if (m_n.isGauge()) {
        // Volume / brightness: a bar, filled to the value
        const int barH = 10;
        const QRectF bar(x, (height() - barH) / 2.0, textW, barH);
        QPainterPath track;
        track.addRoundedRect(bar, barH / 2.0, barH / 2.0);
        p.fillPath(track, QColor(255, 255, 255, 45));
        const qreal frac = qBound(0.0, m_n.value / 100.0, 1.0);
        if (frac > 0) {
            QPainterPath fill;
            fill.addRoundedRect(QRectF(bar.left(), bar.top(), qMax(qreal(barH), bar.width() * frac), barH), barH / 2.0, barH / 2.0);
            p.fillPath(fill, QColor(0xf4, 0xf4, 0xf4));
        }
        return;
    }

    const QFont sf = summaryFont(font());
    int y = m_pad;
    const int textH = int(m_summaryLines.size()) * QFontMetrics(sf).lineSpacing()
                      + (m_summaryLines.isEmpty() || m_bodyLines.isEmpty() ? 0 : 3)
                      + int(m_bodyLines.size()) * QFontMetrics(font()).lineSpacing();
    if (hasIcon && textH < m_iconSize)
        y += (m_iconSize - textH) / 2; // a short text sits in the middle of the icon
    p.setFont(sf);
    p.setPen(kSummaryColor);
    for (const QString &line : std::as_const(m_summaryLines)) {
        p.drawText(QRect(x, y, textW, QFontMetrics(sf).lineSpacing()), Qt::AlignLeft | Qt::AlignVCenter, line);
        y += QFontMetrics(sf).lineSpacing();
    }
    if (!m_summaryLines.isEmpty())
        y += 3;
    p.setFont(font());
    p.setPen(kBodyColor);
    for (const QString &line : std::as_const(m_bodyLines)) {
        p.drawText(QRect(x, y, textW, QFontMetrics(font()).lineSpacing()), Qt::AlignLeft | Qt::AlignVCenter, line);
        y += QFontMetrics(font()).lineSpacing();
    }
}
