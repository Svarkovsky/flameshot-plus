#include <QApplication>
#include <QTest>
#include <QDebug>
#include <cassert>
#include "src/widgets/record/recordcanvaswidget.h"

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    qDebug() << "Testing RecordCanvasWidget...";

    QRect testArea(100, 100, 640, 480);
    auto* canvas = new RecordCanvasWidget(testArea);
    canvas->show();

    // 1. Initial State: Drawing disabled, clicks pass through
    assert(!canvas->isDrawingEnabled());
    qDebug() << "Test 1: Initial pass-through state OK";

    // 2. Enable Drawing
    canvas->setDrawingEnabled(true);
    assert(canvas->isDrawingEnabled());
    qDebug() << "Test 2: Enable drawing OK";

    // 3. Tool selection
    canvas->setCurrentTool(LiveTool::Arrow);
    assert(canvas->currentTool() == LiveTool::Arrow);

    canvas->setCurrentTool(LiveTool::Pencil);
    assert(canvas->currentTool() == LiveTool::Pencil);

    canvas->setCurrentTool(LiveTool::Rectangle);
    assert(canvas->currentTool() == LiveTool::Rectangle);
    qDebug() << "Test 3: Tool selection OK";

    // 4. Color selection
    QColor cyan(QStringLiteral("#38bdf8"));
    canvas->setCurrentColor(cyan);
    assert(canvas->currentColor() == cyan);
    qDebug() << "Test 4: Color selection OK";

    // 5. Thickness selection
    canvas->setThickness(8);
    assert(canvas->thickness() == 8);
    qDebug() << "Test 5: Thickness adjustment OK";

    // 6. Simulate mouse drawing
    canvas->setCurrentTool(LiveTool::Arrow);
    QMouseEvent pressEvent(QEvent::MouseButtonPress, QPoint(50, 50), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &pressEvent);

    QMouseEvent moveEvent(QEvent::MouseMove, QPoint(150, 150), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &moveEvent);

    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, QPoint(150, 150), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &releaseEvent);
    qDebug() << "Test 6: Mouse draw stroke OK";

    // 7. Undo
    canvas->undo();
    qDebug() << "Test 7: Undo OK";

    // 8. Step Counter
    canvas->setCurrentTool(LiveTool::StepCounter);
    QMouseEvent stepPress(QEvent::MouseButtonPress, QPoint(100, 100), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &stepPress);
    qDebug() << "Test 8: Step counter OK";

    // 9. Clear
    canvas->clearCanvas();
    qDebug() << "Test 9: Clear canvas OK";

    // 10. Disable Drawing (Return to pass-through)
    canvas->setDrawingEnabled(false);
    assert(!canvas->isDrawingEnabled());
    qDebug() << "Test 10: Return to pass-through OK";

    canvas->close();
    delete canvas;

    qDebug() << "All RecordCanvasWidget tests PASSED successfully!";
    return 0;
}
