#pragma once

#include <QWidget>
#include <QPainter>
#include <QMouseEvent>

class BlockTrimSlider : public QWidget
{
    Q_OBJECT
public:
    explicit BlockTrimSlider(QWidget* parent = nullptr);

    void setDuration(double duration);
    double duration() const { return m_duration; }

    void setRange(double startTime, double endTime);
    double startTime() const { return m_startTime; }
    double endTime() const { return m_endTime; }

signals:
    void rangeChanged(double startTime, double endTime);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int timeToBlockIndex(double t) const;
    double blockIndexToTime(int index) const;

    double m_duration;
    double m_startTime;
    double m_endTime;
    const int m_numBlocks = 28;

    enum DragMode { None, DragStart, DragEnd, DragMove };
    DragMode m_dragMode;
};
