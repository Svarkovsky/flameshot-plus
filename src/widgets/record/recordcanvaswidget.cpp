// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Svarkovsky <https://github.com/Svarkovsky/flameshot-plus>

#include "recordcanvaswidget.h"
#include "src/widgets/capture/colorpicker.h"

#include <QPainter>
#include <QDebug>
#include <QPainterPath>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>
#include <cmath>

#if defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/shapeconst.h>
#include <X11/extensions/Xfixes.h>
#endif

static QPainterPath calculateArrowHead(const QPointF& p1, const QPointF& p2, int thickness)
{
    qreal headLength = qMax(14.0, static_cast<double>(thickness * 3.5));
    qreal headAngle = 0.52; // ~30 degrees
    qreal angle = std::atan2(p2.y() - p1.y(), p2.x() - p1.x());

    QPointF pArrow1 = p2 - QPointF(headLength * std::cos(angle - headAngle), headLength * std::sin(angle - headAngle));
    QPointF pArrow2 = p2 - QPointF(headLength * std::cos(angle + headAngle), headLength * std::sin(angle + headAngle));

    QPainterPath path;
    path.moveTo(p2);
    path.lineTo(pArrow1);
    path.lineTo(pArrow2);
    path.closeSubpath();
    return path;
}

RecordCanvasWidget::RecordCanvasWidget(const QRect& captureArea, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_captureArea(captureArea)
    , m_drawingEnabled(false)
    , m_currentTool(LiveTool::Arrow)
    , m_currentColor(QColor(QStringLiteral("#ef4444"))) // default vibrant red
    , m_thickness(4)
    , m_stepCounter(1)
    , m_isDrawing(false)
    , m_colorPicker(new ColorPicker(this))
    , m_textEditor(nullptr)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setGeometry(m_captureArea);

    m_colorPicker->hide();
    connect(m_colorPicker, &ColorPicker::colorSelected, this, [this](const QColor& c) {
        setCurrentColor(c);
    });

    updateInputShape();
}

RecordCanvasWidget::~RecordCanvasWidget()
{
}

void RecordCanvasWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateInputShape();
}

void RecordCanvasWidget::setDrawingEnabled(bool enabled)
{
    if (!enabled) {
        finishTextEditing();
    }
    m_drawingEnabled = enabled;
    updateInputShape();
    if (m_drawingEnabled) {
        raise();
        activateWindow();
    }
    update();
}

void RecordCanvasWidget::updateInputShape()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
    Display* disp = XOpenDisplay(nullptr);
    if (disp) {
        Window win = static_cast<Window>(winId());
        if (!m_drawingEnabled) {
            // Empty region: all mouse events pass completely through to windows underneath
            XserverRegion emptyRegion = XFixesCreateRegion(disp, nullptr, 0);
            XFixesSetWindowShapeRegion(disp, win, ShapeInput, 0, 0, emptyRegion);
            XFixesDestroyRegion(disp, emptyRegion);
        } else {
            // Full region: capture clicks for drawing tools
            XRectangle rect;
            rect.x = 0;
            rect.y = 0;
            rect.width = static_cast<unsigned short>(qMax(1, width()));
            rect.height = static_cast<unsigned short>(qMax(1, height()));
            XserverRegion fullRegion = XFixesCreateRegion(disp, &rect, 1);
            XFixesSetWindowShapeRegion(disp, win, ShapeInput, 0, 0, fullRegion);
            XFixesDestroyRegion(disp, fullRegion);
        }
        XFlush(disp);
        XCloseDisplay(disp);
    }
#endif
    setAttribute(Qt::WA_TransparentForMouseEvents, !m_drawingEnabled);
    if (m_drawingEnabled) {
        setCursor(Qt::CrossCursor);
    } else {
        unsetCursor();
    }
}

void RecordCanvasWidget::setCurrentTool(LiveTool tool)
{
    finishTextEditing();
    m_currentTool = tool;
}

void RecordCanvasWidget::setCurrentColor(const QColor& color)
{
    m_currentColor = color;
    emit colorChanged(color);
    if (m_textEditor) {
        m_textEditor->setStyleSheet(QStringLiteral(
            "background-color: #1a1e28; color: %1; border: 1.5px solid #a855f7; "
            "border-radius: 4px; font-weight: bold; font-size: %2px; padding: 2px 6px;"
        ).arg(m_currentColor.name()).arg(qMax(14, m_thickness * 3)));
    }
    update();
}

void RecordCanvasWidget::setThickness(int thickness)
{
    m_thickness = qBound(2, thickness, 32);
    update();
}

void RecordCanvasWidget::finishTextEditing()
{
    if (m_textEditor) {
        QString text = m_textEditor->text().trimmed();
        if (!text.isEmpty()) {
            LiveCanvasItem item;
            item.tool = LiveTool::Text;
            item.color = m_currentColor;
            item.thickness = m_thickness;
            item.points = { m_textPos };
            item.stepNumber = 0;
            item.text = text;
            m_items.append(item);
            m_redoStack.clear();
        }
        m_textEditor->deleteLater();
        m_textEditor = nullptr;
        update();
    }
}

void RecordCanvasWidget::undo()
{
    finishTextEditing();
    if (!m_items.isEmpty()) {
        LiveCanvasItem last = m_items.takeLast();
        if (last.tool == LiveTool::StepCounter && m_stepCounter > 1) {
            --m_stepCounter;
        }
        m_redoStack.append(last);
        update();
    }
}

void RecordCanvasWidget::redo()
{
    finishTextEditing();
    if (!m_redoStack.isEmpty()) {
        LiveCanvasItem item = m_redoStack.takeLast();
        if (item.tool == LiveTool::StepCounter) {
            ++m_stepCounter;
        }
        m_items.append(item);
        update();
    }
}

void RecordCanvasWidget::clearCanvas()
{
    finishTextEditing();
    m_items.clear();
    m_redoStack.clear();
    m_stepCounter = 1;
    update();
}

void RecordCanvasWidget::mousePressEvent(QMouseEvent* event)
{
    if (!m_drawingEnabled) {
        event->ignore();
        return;
    }

    if (event->button() == Qt::RightButton) {
        if (m_colorPicker) {
            QPoint pos = event->pos();
            m_colorPicker->move(pos.x() - m_colorPicker->width() / 2,
                                pos.y() - m_colorPicker->height() / 2);
            m_colorPicker->raise();
            m_colorPicker->show();
        }
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (m_colorPicker && m_colorPicker->isVisible()) {
            m_colorPicker->hide();
        }

        if (m_currentTool == LiveTool::Text) {
            finishTextEditing();
            m_textPos = event->pos();
            m_textEditor = new QLineEdit(this);
            int fontSize = qMax(14, m_thickness * 3);
            m_textEditor->setStyleSheet(QStringLiteral(
                "background-color: #1a1e28; color: %1; border: 1.5px solid #a855f7; "
                "border-radius: 4px; font-weight: bold; font-size: %2px; padding: 2px 6px;"
            ).arg(m_currentColor.name()).arg(fontSize));
            m_textEditor->setMinimumWidth(140);
            m_textEditor->setFixedHeight(fontSize + 14);
            m_textEditor->move(m_textPos);
            connect(m_textEditor, &QLineEdit::returnPressed, this, &RecordCanvasWidget::finishTextEditing);
            m_textEditor->show();
            m_textEditor->setFocus();
            return;
        }

        finishTextEditing();

        m_isDrawing = true;
        m_currentPoints.clear();
        m_currentPoints.append(event->pos());

        if (m_currentTool == LiveTool::StepCounter) {
            LiveCanvasItem stepItem;
            stepItem.tool = LiveTool::StepCounter;
            stepItem.color = m_currentColor;
            stepItem.thickness = m_thickness;
            stepItem.points = m_currentPoints;
            stepItem.stepNumber = m_stepCounter++;
            m_items.append(stepItem);
            m_redoStack.clear();
            m_isDrawing = false;
        }

        update();
    }
}

void RecordCanvasWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_drawingEnabled || !m_isDrawing) {
        event->ignore();
        return;
    }

    if (m_currentTool == LiveTool::Pencil || m_currentTool == LiveTool::Marker) {
        m_currentPoints.append(event->pos());
    } else {
        if (m_currentPoints.size() > 1) {
            m_currentPoints.last() = event->pos();
        } else {
            m_currentPoints.append(event->pos());
        }
    }
    update();
}

void RecordCanvasWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_drawingEnabled || !m_isDrawing || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    if (m_currentTool == LiveTool::Pencil || m_currentTool == LiveTool::Marker) {
        m_currentPoints.append(event->pos());
    } else {
        if (m_currentPoints.size() > 1) {
            m_currentPoints.last() = event->pos();
        } else {
            m_currentPoints.append(event->pos());
        }
    }

    if (!m_currentPoints.isEmpty()) {
        if (m_currentTool == LiveTool::Pixelate || m_currentTool == LiveTool::Invert) {
            if (m_currentPoints.size() >= 2) {
                QRect r = QRect(m_currentPoints.first(), m_currentPoints.last()).normalized();
                if (r.width() > 6 && r.height() > 6) {
                    QRect globalRect = QRect(mapToGlobal(r.topLeft()), r.size());
                    QScreen* screen = QGuiApplication::primaryScreen();
                    if (screen) {
                        QPixmap grab = screen->grabWindow(0, globalRect.x(), globalRect.y(), globalRect.width(), globalRect.height());
                        LiveCanvasItem item;
                        item.tool = m_currentTool;
                        item.color = m_currentColor;
                        item.thickness = m_thickness;
                        item.points = { r.topLeft(), r.bottomRight() };
                        item.stepNumber = 0;
                        if (m_currentTool == LiveTool::Pixelate) {
                            int pw = qMax(1, grab.width() / 12);
                            int ph = qMax(1, grab.height() / 12);
                            QImage img = grab.toImage().scaled(pw, ph, Qt::IgnoreAspectRatio, Qt::FastTransformation);
                            item.pixmap = QPixmap::fromImage(img.scaled(grab.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation));
                        } else {
                            QImage img = grab.toImage();
                            img.invertPixels(QImage::InvertRgb);
                            item.pixmap = QPixmap::fromImage(img);
                        }
                        m_items.append(item);
                        m_redoStack.clear();
                    }
                }
            }
        } else {
            LiveCanvasItem item;
            item.tool = m_currentTool;
            item.color = m_currentColor;
            item.thickness = m_thickness;
            item.points = m_currentPoints;
            item.stepNumber = 0;
            m_items.append(item);
            m_redoStack.clear();
        }
    }

    m_isDrawing = false;
    m_currentPoints.clear();
    update();
}

void RecordCanvasWidget::wheelEvent(QWheelEvent* event)
{
    if (!m_drawingEnabled) {
        event->ignore();
        return;
    }

    int delta = event->angleDelta().y();
    if (delta > 0) {
        setThickness(m_thickness + 1);
    } else if (delta < 0) {
        setThickness(m_thickness - 1);
    }
    event->accept();
}

void RecordCanvasWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        finishTextEditing();
        emit requestCollapseTools();
        event->accept();
        return;
    }

    if (event->matches(QKeySequence::Undo)) {
        undo();
        event->accept();
        return;
    }

    if (event->matches(QKeySequence::Redo)) {
        redo();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_C || event->key() == Qt::Key_Delete) {
        clearCanvas();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void RecordCanvasWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw committed items
    for (const auto& item : m_items) {
        drawItem(painter, item);
    }

    // Draw active drawing item
    if (m_isDrawing && m_currentPoints.size() >= 2) {
        LiveCanvasItem activeItem;
        activeItem.tool = m_currentTool;
        activeItem.color = m_currentColor;
        activeItem.thickness = m_thickness;
        activeItem.points = m_currentPoints;
        activeItem.stepNumber = m_stepCounter;
        drawItem(painter, activeItem);
    }
}

void RecordCanvasWidget::drawItem(QPainter& painter, const LiveCanvasItem& item)
{
    if (item.points.isEmpty()) return;

    painter.save();

    switch (item.tool) {
    case LiveTool::Pencil: {
        painter.setPen(QPen(item.color, item.thickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        for (int i = 0; i < item.points.size() - 1; ++i) {
            painter.drawLine(item.points[i], item.points[i + 1]);
        }
        break;
    }
    case LiveTool::Marker: {
        QColor markerCol = item.color;
        markerCol.setAlpha(110);
        painter.setPen(QPen(markerCol, qMax(16, item.thickness * 4), Qt::SolidLine, Qt::SquareCap, Qt::BevelJoin));
        for (int i = 0; i < item.points.size() - 1; ++i) {
            painter.drawLine(item.points[i], item.points[i + 1]);
        }
        break;
    }
    case LiveTool::Line: {
        if (item.points.size() >= 2) {
            painter.setPen(QPen(item.color, item.thickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawLine(item.points.first(), item.points.last());
        }
        break;
    }
    case LiveTool::Arrow: {
        if (item.points.size() >= 2) {
            QPointF p1 = item.points.first();
            QPointF p2 = item.points.last();
            painter.setPen(QPen(item.color, item.thickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawLine(p1, p2);

            QPainterPath arrowHead = calculateArrowHead(p1, p2, item.thickness);
            painter.setBrush(item.color);
            painter.setPen(Qt::NoPen);
            painter.drawPath(arrowHead);
        }
        break;
    }
    case LiveTool::RectangleOutline: {
        if (item.points.size() >= 2) {
            QRect r = QRect(item.points.first(), item.points.last()).normalized();
            painter.setPen(QPen(item.color, item.thickness, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(r, 4, 4);
        }
        break;
    }
    case LiveTool::RectangleFill: {
        if (item.points.size() >= 2) {
            QRect r = QRect(item.points.first(), item.points.last()).normalized();
            painter.setPen(Qt::NoPen);
            painter.setBrush(item.color);
            painter.drawRoundedRect(r, 4, 4);
        }
        break;
    }
    case LiveTool::Circle: {
        if (item.points.size() >= 2) {
            QRect r = QRect(item.points.first(), item.points.last()).normalized();
            painter.setPen(QPen(item.color, item.thickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(r);
        }
        break;
    }
    case LiveTool::Text: {
        if (!item.text.isEmpty() && !item.points.isEmpty()) {
            painter.setPen(item.color);
            QFont f = painter.font();
            f.setBold(true);
            f.setPixelSize(qMax(14, item.thickness * 3));
            painter.setFont(f);
            painter.drawText(item.points.first().x(), item.points.first().y() + f.pixelSize(), item.text);
        }
        break;
    }
    case LiveTool::StepCounter: {
        int r = qMax(14, item.thickness * 3);
        painter.setPen(Qt::NoPen);
        painter.setBrush(item.color);
        painter.drawEllipse(item.points.first(), r, r);

        painter.setPen(Qt::white);
        QFont f = painter.font();
        f.setBold(true);
        f.setPixelSize(r + 2);
        painter.setFont(f);
        QRect textRect(item.points.first().x() - r, item.points.first().y() - r, r * 2, r * 2);
        painter.drawText(textRect, Qt::AlignCenter, QString::number(item.stepNumber));
        break;
    }
    case LiveTool::Pixelate:
    case LiveTool::Invert: {
        if (!item.pixmap.isNull()) {
            painter.drawPixmap(item.points.first(), item.pixmap);
        } else if (m_isDrawing && item.points.size() >= 2) {
            // Preview dashed bounding box while dragging
            QRect r = QRect(item.points.first(), item.points.last()).normalized();
            painter.setPen(QPen(QColor(255, 255, 255, 200), 1.5, Qt::DashLine));
            painter.setBrush(QColor(100, 100, 100, 60));
            painter.drawRect(r);
        }
        break;
    }
    }

    painter.restore();
}
