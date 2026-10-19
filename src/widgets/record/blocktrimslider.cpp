#include "blocktrimslider.h"
#include <QPainter>
#include <QtMath>

BlockTrimSlider::BlockTrimSlider(QWidget* parent)
    : QWidget(parent)
    , m_duration(1.0)
    , m_startTime(0.0)
    , m_endTime(1.0)
    , m_dragMode(None)
{
    setFixedHeight(38);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Кликните или перетащите для быстрой обрезки по блокам"));
}

void BlockTrimSlider::setDuration(double duration)
{
    m_duration = qMax(0.5, duration);
    m_startTime = 0.0;
    m_endTime = m_duration;
    update();
}

void BlockTrimSlider::setRange(double startTime, double endTime)
{
    m_startTime = qBound(0.0, startTime, m_duration);
    m_endTime = qBound(m_startTime, endTime, m_duration);
    update();
}

int BlockTrimSlider::timeToBlockIndex(double t) const
{
    int idx = static_cast<int>((t / m_duration) * m_numBlocks);
    return qBound(0, idx, m_numBlocks - 1);
}

double BlockTrimSlider::blockIndexToTime(int index) const
{
    return (index / static_cast<double>(m_numBlocks)) * m_duration;
}

void BlockTrimSlider::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int spacing = 3;
    const int blockH = 22;
    const int availableW = width() - 8;
    const int blockW = qMax(6, (availableW - (m_numBlocks - 1) * spacing) / m_numBlocks);
    const int totalW = m_numBlocks * blockW + (m_numBlocks - 1) * spacing;
    const int startX = (width() - totalW) / 2;
    const int startY = (height() - blockH) / 2;

    int startBlock = timeToBlockIndex(m_startTime);
    int endBlock = timeToBlockIndex(m_endTime);
    if (m_endTime >= m_duration) {
        endBlock = m_numBlocks - 1;
    }

    for (int i = 0; i < m_numBlocks; ++i) {
        int x = startX + i * (blockW + spacing);
        int y = startY;
        QRect rect(x, y, blockW, blockH);

        bool isKept = (i >= startBlock && i <= endBlock);
        bool isStartHandle = (i == startBlock && startBlock > 0);
        bool isEndHandle = (i == endBlock && endBlock < m_numBlocks - 1);

        if (isKept) {
            if (isStartHandle || isEndHandle) {
                // Edge handles: Highlighted Cyan/Sky Blue
                p.fillRect(rect, QColor(QStringLiteral("#0284c7")));
                p.setPen(QColor(QStringLiteral("#38bdf8")));
                p.drawRect(rect.adjusted(0, 0, -1, -1));
            } else {
                // Kept Video Blocks: Solid Purple with Crisp Borders
                p.fillRect(rect, QColor(QStringLiteral("#a855f7")));
                p.setPen(QColor(QStringLiteral("#c084fc")));
                p.drawRect(rect.adjusted(0, 0, -1, -1));
            }
        } else {
            // Cut / Trimmed Blocks: Muted Slate with crisp dark outline
            p.fillRect(rect, QColor(QStringLiteral("#1e293b")));
            p.setPen(QColor(QStringLiteral("#334155")));
            p.drawRect(rect.adjusted(0, 0, -1, -1));

            // Subtle red indicator dot inside cut blocks
            p.setPen(QColor(QStringLiteral("#ef4444")));
            p.drawPoint(rect.center());
        }
    }
}

void BlockTrimSlider::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;

    const int spacing = 3;
    const int blockH = 22;
    const int availableW = width() - 8;
    const int blockW = qMax(6, (availableW - (m_numBlocks - 1) * spacing) / m_numBlocks);
    const int totalW = m_numBlocks * blockW + (m_numBlocks - 1) * spacing;
    const int startX = (width() - totalW) / 2;

    int clickX = event->x() - startX;
    int clickedBlock = qBound(0, clickX / (blockW + spacing), m_numBlocks - 1);
    double clickedTime = blockIndexToTime(clickedBlock);

    int startBlock = timeToBlockIndex(m_startTime);
    int endBlock = timeToBlockIndex(m_endTime);

    int distToStart = qAbs(clickedBlock - startBlock);
    int distToEnd = qAbs(clickedBlock - endBlock);

    if (distToStart < distToEnd) {
        m_dragMode = DragStart;
        m_startTime = qMin(clickedTime, m_endTime - 0.5);
    } else {
        m_dragMode = DragEnd;
        m_endTime = qMax(clickedTime, m_startTime + 0.5);
    }

    update();
    emit rangeChanged(m_startTime, m_endTime);
}

void BlockTrimSlider::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton) || m_dragMode == None) return;

    const int spacing = 3;
    const int availableW = width() - 8;
    const int blockW = qMax(6, (availableW - (m_numBlocks - 1) * spacing) / m_numBlocks);
    const int totalW = m_numBlocks * blockW + (m_numBlocks - 1) * spacing;
    const int startX = (width() - totalW) / 2;

    int curX = event->x() - startX;
    int curBlock = qBound(0, curX / (blockW + spacing), m_numBlocks - 1);
    double curTime = blockIndexToTime(curBlock);

    if (m_dragMode == DragStart) {
        m_startTime = qBound(0.0, curTime, m_endTime - 0.5);
    } else if (m_dragMode == DragEnd) {
        m_endTime = qBound(m_startTime + 0.5, curTime + (m_duration / m_numBlocks), m_duration);
    }

    update();
    emit rangeChanged(m_startTime, m_endTime);
}

void BlockTrimSlider::mouseReleaseEvent(QMouseEvent* /*event*/)
{
    m_dragMode = None;
}
