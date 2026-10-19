#include "audiovumeterwidget.h"
#include <QPainter>

AudioVuMeterWidget::AudioVuMeterWidget(QWidget* parent)
    : QWidget(parent)
    , m_level(0.0)
    , m_muted(false)
{
    setFixedSize(54, 22);
    setToolTip(QStringLiteral("Уровень звука записи"));
}

void AudioVuMeterWidget::setLevel(double level)
{
    m_level = qBound(0.0, level, 1.0);
    update();
}

void AudioVuMeterWidget::setMuted(bool muted)
{
    m_muted = muted;
    if (m_muted) {
        m_level = 0.0;
    }
    update();
}

void AudioVuMeterWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int numBlocks = 5;
    const int blockW = 7;
    const int blockH = 12;
    const int spacing = 3;
    const int totalW = numBlocks * blockW + (numBlocks - 1) * spacing;
    const int startX = (width() - totalW) / 2;
    const int startY = (height() - blockH) / 2;

    for (int i = 0; i < numBlocks; ++i) {
        int x = startX + i * (blockW + spacing);
        int y = startY;
        QRect rect(x, y, blockW, blockH);

        double threshold = (i + 1) * 0.18;
        bool active = !m_muted && (m_level >= threshold || (i == 0 && m_level > 0.015));

        if (active) {
            QColor fillColor;
            QColor borderColor;
            if (i < 3) {
                fillColor = QColor(QStringLiteral("#22c55e"));
                borderColor = QColor(QStringLiteral("#16a34a"));
            } else if (i == 3) {
                fillColor = QColor(QStringLiteral("#f59e0b"));
                borderColor = QColor(QStringLiteral("#d97706"));
            } else {
                fillColor = QColor(QStringLiteral("#ef4444"));
                borderColor = QColor(QStringLiteral("#dc2626"));
            }
            p.fillRect(rect, fillColor);
            p.setPen(borderColor);
            p.drawRect(rect.adjusted(0, 0, -1, -1));
        } else {
            p.fillRect(rect, QColor(QStringLiteral("#1e293b")));
            p.setPen(QColor(QStringLiteral("#475569")));
            p.drawRect(rect.adjusted(0, 0, -1, -1));
        }
    }
}
