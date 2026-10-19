// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Svarkovsky <https://github.com/Svarkovsky/flameshot-plus>

#pragma once

#include <QWidget>
#include <QVector>
#include <QPoint>
#include <QColor>
#include <QRect>
#include <QPixmap>
#include <QLineEdit>

class ColorPicker;

enum class LiveTool {
    Pencil,
    Line,
    Arrow,
    RectangleOutline,
    RectangleFill,
    Circle,
    Marker,
    Text,
    StepCounter,
    Pixelate,
    Invert
};

struct LiveCanvasItem {
    LiveTool tool;
    QColor color;
    int thickness;
    QVector<QPoint> points;
    int stepNumber;
    QString text;
    QPixmap pixmap;
};

class RecordCanvasWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RecordCanvasWidget(const QRect& captureArea, QWidget* parent = nullptr);
    ~RecordCanvasWidget() override;

    void setDrawingEnabled(bool enabled);
    bool isDrawingEnabled() const { return m_drawingEnabled; }
    bool hasItems() const { return !m_items.isEmpty(); }

    void setCurrentTool(LiveTool tool);
    LiveTool currentTool() const { return m_currentTool; }

    void setCurrentColor(const QColor& color);
    QColor currentColor() const { return m_currentColor; }

    void setThickness(int thickness);
    int thickness() const { return m_thickness; }

    void undo();
    void redo();
    void clearCanvas();

signals:
    void requestCollapseTools();
    void colorChanged(const QColor& color);

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateInputShape();
    void drawItem(QPainter& painter, const LiveCanvasItem& item);
    void finishTextEditing();

    QRect m_captureArea;
    bool m_drawingEnabled;
    LiveTool m_currentTool;
    QColor m_currentColor;
    int m_thickness;
    int m_stepCounter;

    bool m_isDrawing;
    QVector<QPoint> m_currentPoints;
    QVector<LiveCanvasItem> m_items;
    QVector<LiveCanvasItem> m_redoStack;

    ColorPicker* m_colorPicker;
    QLineEdit* m_textEditor;
    QPoint m_textPos;
};
