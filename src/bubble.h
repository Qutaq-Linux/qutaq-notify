#pragma once

#include "notification.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

class QScreen;

// A NotifyOSD-style bubble: dark, rounded, at the top right below the panel.
//
//   ┌──────────────────────────────┐
//   │ [icon]  Summary              │
//   │         Body text, wrapped,  │
//   │         up to a few lines…   │
//   └──────────────────────────────┘
//
// Gauge bubbles (volume, brightness) show the icon and a bar. Bubbles can't be clicked:
// under the pointer they fade almost away and let clicks through, as in Unity.
class Bubble : public QWidget
{
    Q_OBJECT
public:
    explicit Bubble(const Notification &n, QScreen *screen);

    const Notification &notification() const { return m_n; }
    void update(const Notification &n);  // replaces_id: new content, the timer starts again
    void appendBody(const QString &body); // x-canonical-append
    void setStackOffset(int y);          // stacking: the bubbles nearer to the screen edge
    void showBubble();
    void closeBubble();                  // fade out, then finished()
    bool isClosing() const { return m_closing; }
    int bubbleHeight() const { return height(); }

Q_SIGNALS:
    void expired();   // the duration is over (it then fades out)
    void finished();  // faded out: delete me
    void heightChanged();

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;

private:
    void relayout();
    int durationMs() const;
    void setupLayerShell(QScreen *screen);
    void animateOpacity(qreal to, int ms);
    void restartTimer();
    void applyOffset();
    bool isBottom() const;

    Notification m_n;
    QStringList m_summaryLines, m_bodyLines;
    QImage m_icon;
    qreal m_opacity = 0;
    bool m_closing = false;
    bool m_faded = false;   // under the pointer
    int m_stackOffset = 0;
    QString m_position;  // top-right…, from notify.conf
    int m_width = 300, m_pad = 14, m_iconSize = 48, m_gap = 12, m_radius = 10; // × scale
    QTimer m_expire;
    QElapsedTimer m_shownFor;
    int m_remaining = 0;     // expiry time left while faded
    QVariantAnimation m_fade;
    QTimer m_passThrough;   // while faded: input region off, then check whether the pointer is still there
    bool m_reentered = false;
};
