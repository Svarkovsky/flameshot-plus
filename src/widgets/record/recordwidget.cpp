// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Svarkovsky <https://github.com/Svarkovsky/flameshot-plus>

#include "recordwidget.h"
#include "recordcanvaswidget.h"
#include "audiovumeterwidget.h"
#include "recordpreviewdialog.h" 
#include "src/core/flameshot.h"
#include "src/core/flameshotdaemon.h"
#include "src/utils/systemnotification.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegExp>
#include <QScreen>
#include <QStandardPaths>

#if defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

static QString getActiveMonitorSource()
{
    // 1. Try finding active sink from playing streams (Corked: no)
    QProcess p;
    p.start(QStringLiteral("pactl"), QStringList() << QStringLiteral("list") << QStringLiteral("sink-inputs"));
    if (p.waitForFinished(1000)) {
        QString out = QString::fromUtf8(p.readAllStandardOutput());
        QString currentSink;
        bool isUncorked = false;
        const QStringList lines = out.split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            QString trimmed = line.trimmed();
            if (trimmed.startsWith(QLatin1String("Sink: "))) {
                currentSink = trimmed.mid(6).trimmed();
            } else if (trimmed == QLatin1String("Corked: no")) {
                isUncorked = true;
                break;
            } else if (trimmed.startsWith(QLatin1String("Sink Input #"))) {
                currentSink.clear();
                isUncorked = false;
            }
        }
        if (isUncorked && !currentSink.isEmpty()) {
            QProcess p2;
            p2.start(QStringLiteral("pactl"), QStringList() << QStringLiteral("list") << QStringLiteral("short") << QStringLiteral("sinks"));
            if (p2.waitForFinished(1000)) {
                QString sinksOut = QString::fromUtf8(p2.readAllStandardOutput());
                for (const QString& sline : sinksOut.split(QLatin1Char('\n'))) {
                    QStringList parts = sline.split(QRegExp(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                    if (parts.size() >= 2 && parts[0] == currentSink) {
                        return parts[1] + QStringLiteral(".monitor");
                    }
                }
            }
        }
    }

    // 2. Check if a bluetooth sink exists (often preferred on Linux desktops)
    QProcess pBt;
    pBt.start(QStringLiteral("pactl"), QStringList() << QStringLiteral("list") << QStringLiteral("short") << QStringLiteral("sinks"));
    if (pBt.waitForFinished(1000)) {
        QString sinksOut = QString::fromUtf8(pBt.readAllStandardOutput());
        for (const QString& sline : sinksOut.split(QLatin1Char('\n'))) {
            QStringList parts = sline.split(QRegExp(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (parts.size() >= 2 && parts[1].startsWith(QLatin1String("bluez_output"))) {
                return parts[1] + QStringLiteral(".monitor");
            }
        }
    }

    // 3. Fallback to default sink monitor
    QProcess pDef;
    pDef.start(QStringLiteral("pactl"), QStringList() << QStringLiteral("get-default-sink"));
    if (pDef.waitForFinished(1000)) {
        QString defSink = QString::fromUtf8(pDef.readAllStandardOutput()).trimmed();
        if (!defSink.isEmpty()) {
            return defSink + QStringLiteral(".monitor");
        }
    }

    return QStringLiteral("@DEFAULT_MONITOR@");
}

RecordWidget::RecordWidget(const QRect& captureArea, AudioRecordMode audioMode, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_area(captureArea)
    , m_audioMode(audioMode)
    , m_isRecording(false)
    , m_secondsElapsed(0)
    , m_ffmpegProcess(new QProcess(this))
    , m_timer(new QTimer(this))
    , m_statusLabel(nullptr)
    , m_timeLabel(nullptr)
    , m_audioButton(nullptr)
    , m_startStopButton(nullptr)
    , m_toolsToggleButton(nullptr)
    , m_cancelButton(nullptr)
    , m_toolsContainer(nullptr)
    , m_pencilBtn(nullptr)
    , m_lineBtn(nullptr)
    , m_arrowBtn(nullptr)
    , m_rectOutlineBtn(nullptr)
    , m_rectFillBtn(nullptr)
    , m_circleBtn(nullptr)
    , m_markerBtn(nullptr)
    , m_textBtn(nullptr)
    , m_stepBtn(nullptr)
    , m_pixelateBtn(nullptr)
    , m_invertBtn(nullptr)
    , m_sizeIndicatorBtn(nullptr)
    , m_moveBtn(nullptr)
    , m_undoBtn(nullptr)
    , m_redoBtn(nullptr)
    , m_colorIndicatorBtn(nullptr)
    , m_clearBtn(nullptr)
    , m_canvasWidget(nullptr)
    , m_vuMeterWidget(nullptr)
    , m_audioLevelProcess(new QProcess(this))
    , m_audioLevelDecayTimer(new QTimer(this))
    , m_currentAudioLevel(0.0)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_QuitOnClose, true);
    setWindowTitle(QStringLiteral("Flameshot + Recording"));

    // Make area coordinates even numbers for H.264
    int x = m_area.x();
    int y = m_area.y();
    int w = (m_area.width() / 2) * 2;
    int h = (m_area.height() / 2) * 2;
    m_area = QRect(x, y, qMax(16, w), qMax(16, h));

    initControlBar();

    connect(m_timer, &QTimer::timeout, this, &RecordWidget::updateTimer);
    connect(m_audioLevelProcess, &QProcess::readyReadStandardOutput, this, &RecordWidget::onAudioLevelReady);
    connect(m_audioLevelDecayTimer, &QTimer::timeout, this, &RecordWidget::onAudioLevelDecay);
    m_audioLevelDecayTimer->start(80);
    startAudioMonitor();
}

RecordWidget::~RecordWidget()
{
    stopAudioMonitor();

    if (m_canvasWidget) {
        m_canvasWidget->close();
        m_canvasWidget = nullptr;
    }

    if (m_ffmpegProcess && m_ffmpegProcess->state() != QProcess::NotRunning) {
        m_ffmpegProcess->write("q\n");
        if (!m_ffmpegProcess->waitForFinished(2000)) {
            m_ffmpegProcess->terminate();
            if (!m_ffmpegProcess->waitForFinished(1500)) {
                m_ffmpegProcess->kill();
            }
        }
    }

    Flameshot::instance()->setExternalWidget(false);
    if (FlameshotDaemon::instance() == nullptr) {
        qApp->exit(0);
    }
}

void RecordWidget::startRecording(const QRect& captureArea, AudioRecordMode audioMode)
{
    qApp->setQuitOnLastWindowClosed(false);
    auto* widget = new RecordWidget(captureArea, audioMode);
    widget->show();
    widget->raise();
    widget->activateWindow();
}

QString RecordWidget::getVideoSaveDirectory()
{
    QString baseDir = QDir::homePath() + QStringLiteral("/Video/flameshot-plus");
    QDir().mkpath(baseDir);
    return baseDir;
}

QString RecordWidget::generateOutputFilePath()
{
    QString baseDir = getVideoSaveDirectory();
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    return QStringLiteral("%1/flameshot_plus_%2.mp4").arg(baseDir, timestamp);
}

void RecordWidget::initControlBar()
{
    setStyleSheet(
        "RecordWidget { "
        "   background-color: #1a1e28; "
        "   border: 2px solid #38bdf8; "
        "   border-radius: 12px; "
        "} "
        "QLabel { "
        "   background: transparent; "
        "} "
        "QPushButton { "
        "   padding: 6px 10px; "
        "   border-radius: 8px; "
        "   font-weight: bold; "
        "   font-size: 13px; "
        "   border: none; "
        "   white-space: nowrap; "
        "} "
        "QPushButton:hover { "
        "   opacity: 0.9; "
        "}"
    );

    // Create drawing canvas covering capture area
    m_canvasWidget = new RecordCanvasWidget(m_area);
    m_canvasWidget->hide();
    connect(m_canvasWidget, &RecordCanvasWidget::requestCollapseTools, this, &RecordWidget::toggleTools);
    connect(m_canvasWidget, &RecordCanvasWidget::colorChanged, this, &RecordWidget::onCanvasColorChanged);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 6, 12, 6);
    mainLayout->setSpacing(8);

    auto* topLayout = new QHBoxLayout();
    topLayout->setSpacing(8);

    // Initial status: Ready (Готов)
    m_statusLabel = new QLabel(QStringLiteral("● ") + tr("READY"), this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #38bdf8; font-weight: bold; font-size: 13px;"));
    m_statusLabel->setFixedWidth(65);
    topLayout->addWidget(m_statusLabel);

    m_timeLabel = new QLabel(QStringLiteral("00:00"), this);
    m_timeLabel->setStyleSheet(QStringLiteral("color: #ffffff; font-family: monospace; font-size: 15px; font-weight: bold;"));
    m_timeLabel->setFixedWidth(55);
    topLayout->addWidget(m_timeLabel);

    m_audioButton = new QPushButton(this);
    m_audioButton->setMinimumWidth(165);
    updateAudioButtonLabel();
    connect(m_audioButton, &QPushButton::clicked, this, &RecordWidget::cycleAudioMode);
    topLayout->addWidget(m_audioButton);

    m_vuMeterWidget = new AudioVuMeterWidget(this);
    m_vuMeterWidget->setMuted(m_audioMode == AudioRecordMode::Muted);
    topLayout->addWidget(m_vuMeterWidget);

    // Start Button (Green initially)
    m_startStopButton = new QPushButton(QStringLiteral("● ") + tr("Record"), this);
    m_startStopButton->setMinimumWidth(95);
    m_startStopButton->setStyleSheet(QStringLiteral("background-color: #10b981; color: #ffffff; font-size: 13px; font-weight: bold;"));
    connect(m_startStopButton, &QPushButton::clicked, this, &RecordWidget::onStartStopClicked);
    topLayout->addWidget(m_startStopButton);

    // Expandable tools chevron button (inverted double chevron)
    m_toolsToggleButton = new QPushButton(this);
    m_toolsToggleButton->setIcon(QIcon(QStringLiteral(":/img/material/white/chevron-double-down.svg")));
    m_toolsToggleButton->setIconSize(QSize(22, 22));
    m_toolsToggleButton->setFixedSize(36, 32);
    m_toolsToggleButton->setStyleSheet(QStringLiteral("background-color: rgba(168, 85, 247, 0.25); border: 1px solid #a855f7; border-radius: 8px;"));
    m_toolsToggleButton->setToolTip(tr("Drawing tools (Esc to fold)"));
    connect(m_toolsToggleButton, &QPushButton::clicked, this, &RecordWidget::toggleTools);
    topLayout->addWidget(m_toolsToggleButton);

    // Cancel Button
    m_cancelButton = new QPushButton(QStringLiteral("✕"), this);
    m_cancelButton->setFixedSize(36, 32);
    m_cancelButton->setStyleSheet(QStringLiteral("background-color: rgba(255, 255, 255, 0.1); color: #94a3b8; font-size: 14px; font-weight: bold;"));
    m_cancelButton->setToolTip(tr("Cancel recording"));
    connect(m_cancelButton, &QPushButton::clicked, this, &RecordWidget::onCancelClicked);
    topLayout->addWidget(m_cancelButton);

    mainLayout->addLayout(topLayout);

    // Collapsible Tools Container (15 tools sequence matching Flameshot)
    m_toolsContainer = new QWidget(this);
    auto* toolsLayout = new QHBoxLayout(m_toolsContainer);
    toolsLayout->setContentsMargins(0, 0, 0, 0);
    toolsLayout->setSpacing(5);

    auto setupToolBtn = [this, toolsLayout](QPushButton*& btn, const QString& iconPath, const QString& tip) {
        btn = new QPushButton(QIcon(iconPath), QString(), m_toolsContainer);
        btn->setFixedSize(34, 30);
        btn->setIconSize(QSize(19, 19));
        btn->setToolTip(tip);
        btn->setStyleSheet(QStringLiteral("background-color: rgba(255, 255, 255, 0.08); border-radius: 6px; border: 1px solid rgba(255, 255, 255, 0.15);"));
        toolsLayout->addWidget(btn);
    };

    // 1. Pencil
    setupToolBtn(m_pencilBtn, QStringLiteral(":/img/material/white/pencil.svg"), tr("Pencil (freehand drawing)"));
    connect(m_pencilBtn, &QPushButton::clicked, this, &RecordWidget::setToolPencil);

    // 2. Line
    setupToolBtn(m_lineBtn, QStringLiteral(":/img/material/white/line.svg"), tr("Straight line"));
    connect(m_lineBtn, &QPushButton::clicked, this, &RecordWidget::setToolLine);

    // 3. Arrow
    setupToolBtn(m_arrowBtn, QStringLiteral(":/img/material/white/arrow-bottom-left.svg"), tr("Arrow"));
    connect(m_arrowBtn, &QPushButton::clicked, this, &RecordWidget::setToolArrow);

    // 4. Rectangle Outline
    setupToolBtn(m_rectOutlineBtn, QStringLiteral(":/img/material/white/square-outline.svg"), tr("Rectangle (outline)"));
    connect(m_rectOutlineBtn, &QPushButton::clicked, this, &RecordWidget::setToolRectOutline);

    // 5. Rectangle Fill
    setupToolBtn(m_rectFillBtn, QStringLiteral(":/img/material/white/square.svg"), tr("Rectangle (fill)"));
    connect(m_rectFillBtn, &QPushButton::clicked, this, &RecordWidget::setToolRectFill);

    // 6. Circle
    setupToolBtn(m_circleBtn, QStringLiteral(":/img/material/white/circle-outline.svg"), tr("Circle"));
    connect(m_circleBtn, &QPushButton::clicked, this, &RecordWidget::setToolCircle);

    // 7. Marker
    setupToolBtn(m_markerBtn, QStringLiteral(":/img/material/white/marker.svg"), tr("Highlighter"));
    connect(m_markerBtn, &QPushButton::clicked, this, &RecordWidget::setToolMarker);

    // 8. Text
    setupToolBtn(m_textBtn, QStringLiteral(":/img/material/white/text.svg"), tr("Text (click canvas to type)"));
    connect(m_textBtn, &QPushButton::clicked, this, &RecordWidget::setToolText);

    // 9. Step Counter
    setupToolBtn(m_stepBtn, QStringLiteral(":/img/material/white/circlecount-outline.svg"), tr("Step counter (1, 2, 3...)"));
    connect(m_stepBtn, &QPushButton::clicked, this, &RecordWidget::setToolStep);

    // 10. Pixelate
    setupToolBtn(m_pixelateBtn, QStringLiteral(":/img/material/white/pixelate.svg"), tr("Pixelate (blur / hide area)"));
    connect(m_pixelateBtn, &QPushButton::clicked, this, &RecordWidget::setToolPixelate);

    // 11. Invert / Blur
    setupToolBtn(m_invertBtn, QStringLiteral(":/img/material/white/invert.svg"), tr("Invert colors"));
    connect(m_invertBtn, &QPushButton::clicked, this, &RecordWidget::setToolInvert);

    // 12. Dimensions Indicator Badge
    m_sizeIndicatorBtn = new QPushButton(QStringLiteral("%1x%2").arg(m_area.width()).arg(m_area.height()), m_toolsContainer);
    m_sizeIndicatorBtn->setFixedHeight(30);
    m_sizeIndicatorBtn->setStyleSheet(QStringLiteral("background-color: rgba(168, 85, 247, 0.2); color: #c084fc; font-size: 11px; font-weight: bold; border: 1px solid rgba(168, 85, 247, 0.5); border-radius: 6px; padding: 0 6px;"));
    m_sizeIndicatorBtn->setToolTip(tr("Recording area resolution"));
    toolsLayout->addWidget(m_sizeIndicatorBtn);

    // 13. Move Button
    setupToolBtn(m_moveBtn, QStringLiteral(":/img/material/white/cursor-move.svg"), tr("Move panel (drag with mouse)"));

    // 14. Undo Button
    setupToolBtn(m_undoBtn, QStringLiteral(":/img/material/white/undo-variant.svg"), tr("Undo (Ctrl+Z)"));
    connect(m_undoBtn, &QPushButton::clicked, this, [this]() {
        if (m_canvasWidget) m_canvasWidget->undo();
    });

    // 15. Redo Button
    setupToolBtn(m_redoBtn, QStringLiteral(":/img/material/white/redo-variant.svg"), tr("Redo (Ctrl+Shift+Z)"));
    connect(m_redoBtn, &QPushButton::clicked, this, [this]() {
        if (m_canvasWidget) m_canvasWidget->redo();
    });

    // 16. Color Indicator Button
    m_colorIndicatorBtn = new QPushButton(m_toolsContainer);
    m_colorIndicatorBtn->setFixedSize(32, 30);
    m_colorIndicatorBtn->setToolTip(tr("Current color (Right click on canvas opens palette)"));
    onCanvasColorChanged(m_canvasWidget->currentColor());
    connect(m_colorIndicatorBtn, &QPushButton::clicked, this, [this]() {
        if (m_canvasWidget) {
            m_canvasWidget->setDrawingEnabled(true);
        }
    });
    toolsLayout->addWidget(m_colorIndicatorBtn);

    // 17. Clear Button
    m_clearBtn = new QPushButton(tr("Clear"), m_toolsContainer);
    m_clearBtn->setFixedHeight(30);
    m_clearBtn->setToolTip(tr("Clear canvas (C)"));
    m_clearBtn->setStyleSheet(QStringLiteral("background-color: rgba(239, 68, 68, 0.2); color: #f87171; border: 1px solid #ef4444; border-radius: 6px; font-weight: bold; font-size: 11px; padding: 0 8px;"));
    connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
        if (m_canvasWidget) m_canvasWidget->clearCanvas();
    });
    toolsLayout->addWidget(m_clearBtn);

    mainLayout->addWidget(m_toolsContainer);
    m_toolsContainer->hide();

    updateWidgetGeometry();
}

void RecordWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
#if defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
    Display* disp = XOpenDisplay(nullptr);
    if (disp) {
        Atom shadowAtom = XInternAtom(disp, "_COMPTON_SHADOW", False);
        if (shadowAtom != None) {
            unsigned long value = 0;
            XChangeProperty(disp, winId(), shadowAtom, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&value, 1);
        }
        XFlush(disp);
        XCloseDisplay(disp);
    }
#endif
}

void RecordWidget::updateWidgetGeometry()
{
    int barW = 760;
    int barH = (m_toolsContainer && m_toolsContainer->isVisible()) ? 94 : 48;
    int barX = m_area.x() + (m_area.width() - barW) / 2;
    int barY = 0;

    QScreen* screen = QGuiApplication::primaryScreen();
    int screenH = screen ? screen->geometry().height() : 1080;
    int screenW = screen ? screen->geometry().width() : 1920;

    const int shadowMargin = 24;

    if (m_area.y() >= barH + shadowMargin + 4) {
        barY = m_area.y() - barH - shadowMargin;
    } else if (m_area.y() + m_area.height() + barH + shadowMargin + 4 <= screenH) {
        barY = m_area.y() + m_area.height() + shadowMargin;
    } else {
        barY = 10;
        barX = screenW - barW - 10;
    }

    barX = qBound(10, barX, screenW - barW - 10);
    setGeometry(barX, barY, barW, barH);
}

void RecordWidget::toggleTools()
{
    if (!m_toolsContainer) return;

    bool willShow = !m_toolsContainer->isVisible();
    if (willShow) {
        m_toolsContainer->show();
        m_toolsToggleButton->setIcon(QIcon(QStringLiteral(":/img/material/white/chevron-double-up.svg")));
        m_toolsToggleButton->setStyleSheet(QStringLiteral("background-color: #a855f7; border: none; border-radius: 8px;"));
        if (m_canvasWidget) {
            m_canvasWidget->show();
            m_canvasWidget->setDrawingEnabled(true);
        }
        highlightActiveTool(m_arrowBtn);
    } else {
        m_toolsContainer->hide();
        m_toolsToggleButton->setIcon(QIcon(QStringLiteral(":/img/material/white/chevron-double-down.svg")));
        m_toolsToggleButton->setStyleSheet(QStringLiteral("background-color: rgba(168, 85, 247, 0.25); border: 1px solid #a855f7; border-radius: 8px;"));
        if (m_canvasWidget) {
            m_canvasWidget->setDrawingEnabled(false);
            if (m_canvasWidget->hasItems()) {
                m_canvasWidget->show();
            } else {
                m_canvasWidget->hide();
            }
        }
    }
    updateWidgetGeometry();
}

void RecordWidget::highlightActiveTool(QPushButton* activeBtn)
{
    QList<QPushButton*> list = {
        m_pencilBtn, m_lineBtn, m_arrowBtn, m_rectOutlineBtn, m_rectFillBtn,
        m_circleBtn, m_markerBtn, m_textBtn, m_stepBtn, m_pixelateBtn, m_invertBtn
    };
    for (auto* btn : list) {
        if (!btn) continue;
        if (btn == activeBtn) {
            btn->setStyleSheet(QStringLiteral("background-color: #8800aa; border-radius: 6px; border: 2px solid #ffffff;"));
        } else {
            btn->setStyleSheet(QStringLiteral("background-color: rgba(255, 255, 255, 0.08); border-radius: 6px; border: 1px solid rgba(255, 255, 255, 0.15);"));
        }
    }
}

void RecordWidget::setToolPencil()
{
    if (m_canvasWidget) m_canvasWidget->setCurrentTool(LiveTool::Pencil);
    highlightActiveTool(m_pencilBtn);
}

void RecordWidget::setToolLine()
{
    if (m_canvasWidget) m_canvasWidget->setCurrentTool(LiveTool::Line);
    highlightActiveTool(m_lineBtn);
}

void RecordWidget::setToolArrow()
{
    if (m_canvasWidget) m_canvasWidget->setCurrentTool(LiveTool::Arrow);
    highlightActiveTool(m_arrowBtn);
}

void RecordWidget::setToolRectOutline()
{
    if (m_canvasWidget) m_canvasWidget->setCurrentTool(LiveTool::RectangleOutline);
    highlightActiveTool(m_rectOutlineBtn);
}

void RecordWidget::setToolRectFill()
{
    if (m_canvasWidget) m_canvasWidget->setCurrentTool(LiveTool::RectangleFill);
    highlightActiveTool(m_rectFillBtn);
}

void RecordWidget::setToolCircle()
{
    if (m_canvasWidget) m_canvasWidget->setCurrentTool(LiveTool::Circle);
    highlightActiveTool(m_circleBtn);
}

void RecordWidget::setToolMarker()
{
    if (m_canvasWidget) m_canvasWidget->setCurrentTool(LiveTool::Marker);
    highlightActiveTool(m_markerBtn);
}

void RecordWidget::setToolText()
{
    if (m_canvasWidget) m_canvasWidget->setCurrentTool(LiveTool::Text);
    highlightActiveTool(m_textBtn);
}

void RecordWidget::setToolStep()
{
    if (m_canvasWidget) m_canvasWidget->setCurrentTool(LiveTool::StepCounter);
    highlightActiveTool(m_stepBtn);
}

void RecordWidget::setToolPixelate()
{
    if (m_canvasWidget) m_canvasWidget->setCurrentTool(LiveTool::Pixelate);
    highlightActiveTool(m_pixelateBtn);
}

void RecordWidget::setToolInvert()
{
    if (m_canvasWidget) m_canvasWidget->setCurrentTool(LiveTool::Invert);
    highlightActiveTool(m_invertBtn);
}

void RecordWidget::onCanvasColorChanged(const QColor& color)
{
    if (m_colorIndicatorBtn) {
        m_colorIndicatorBtn->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 6px; border: 2px solid #ffffff;").arg(color.name()));
    }
}

void RecordWidget::updateAudioButtonLabel()
{
    QString audioText;
    switch (m_audioMode) {
    case AudioRecordMode::SystemSound:
        audioText = QStringLiteral("🔊 ") + tr("Sound: System");
        m_audioButton->setStyleSheet(QStringLiteral("background-color: rgba(16, 185, 129, 0.3); color: #34d399; border: 1px solid #10b981;"));
        break;
    case AudioRecordMode::Microphone:
        audioText = QStringLiteral("🎤 ") + tr("Sound: Mic");
        m_audioButton->setStyleSheet(QStringLiteral("background-color: rgba(59, 130, 246, 0.3); color: #60a5fa; border: 1px solid #3b82f6;"));
        break;
    case AudioRecordMode::BothMixed:
        audioText = QStringLiteral("🔊+🎤 ") + tr("Sound: All");
        m_audioButton->setStyleSheet(QStringLiteral("background-color: rgba(168, 85, 247, 0.3); color: #c084fc; border: 1px solid #a855f7;"));
        break;
    case AudioRecordMode::Muted:
        audioText = QStringLiteral("🔇 ") + tr("Sound: Muted");
        m_audioButton->setStyleSheet(QStringLiteral("background-color: rgba(100, 116, 139, 0.3); color: #94a3b8; border: 1px solid #64748b;"));
        break;
    }
    m_audioButton->setText(audioText);
}

void RecordWidget::cycleAudioMode()
{
    if (m_isRecording) return; // Locked during active recording

    switch (m_audioMode) {
    case AudioRecordMode::SystemSound:
        m_audioMode = AudioRecordMode::Microphone;
        break;
    case AudioRecordMode::Microphone:
        m_audioMode = AudioRecordMode::BothMixed;
        break;
    case AudioRecordMode::BothMixed:
        m_audioMode = AudioRecordMode::Muted;
        break;
    case AudioRecordMode::Muted:
        m_audioMode = AudioRecordMode::SystemSound;
        break;
    }
    updateAudioButtonLabel();
    startAudioMonitor();
}

void RecordWidget::onStartStopClicked()
{
    if (!m_isRecording) {
        // Transition to Recording state
        m_isRecording = true;
        setStyleSheet(
            "RecordWidget { "
            "   background-color: #1a1e28; "
            "   border: 2px solid #ef4444; "
            "   border-radius: 12px; "
            "} "
            "QLabel { background: transparent; } "
            "QPushButton { padding: 6px 10px; border-radius: 8px; font-weight: bold; font-size: 13px; border: none; white-space: nowrap; } "
            "QPushButton:hover { opacity: 0.9; }"
        );

        m_statusLabel->setText(QStringLiteral("● ") + tr("REC"));
        m_statusLabel->setStyleSheet(QStringLiteral("color: #ef4444; font-weight: bold; font-size: 13px;"));

        m_audioButton->setEnabled(false);
        m_cancelButton->hide();

        m_startStopButton->setText(QStringLiteral("■ ") + tr("Stop"));
        m_startStopButton->setMinimumWidth(115);
        m_startStopButton->setStyleSheet(QStringLiteral("background-color: #ef4444; color: #ffffff; font-size: 13px; font-weight: bold;"));

        startFFmpeg();
        m_timer->start(1000);
    } else {
        stopRecording();
    }
}

void RecordWidget::onCancelClicked()
{
    if (!m_isRecording) {
        close();
    }
}

void RecordWidget::startFFmpeg()
{
    m_outputFile = generateOutputFilePath();

    QStringList args;
    args << QStringLiteral("-y");
    args << QStringLiteral("-loglevel") << QStringLiteral("warning");

    // Dynamic X11 display resolution
    QString displayEnv = QString::fromLocal8Bit(qgetenv("DISPLAY")).trimmed();
    if (displayEnv.isEmpty()) {
        displayEnv = QStringLiteral(":0.0");
    } else if (!displayEnv.contains(QLatin1Char('.'))) {
        displayEnv += QStringLiteral(".0");
    }

    // X11 Video grab
    args << QStringLiteral("-f") << QStringLiteral("x11grab");
    args << QStringLiteral("-draw_mouse") << QStringLiteral("1");
    args << QStringLiteral("-framerate") << QStringLiteral("30");
    args << QStringLiteral("-video_size") << QStringLiteral("%1x%2").arg(m_area.width()).arg(m_area.height());
    args << QStringLiteral("-i") << QStringLiteral("%1+%2,%3").arg(displayEnv).arg(m_area.x()).arg(m_area.y());

    // Audio input based on mode
    if (m_audioMode == AudioRecordMode::SystemSound) {
        QString monitor = getActiveMonitorSource();
        args << QStringLiteral("-f") << QStringLiteral("pulse") << QStringLiteral("-i") << monitor;
    } else if (m_audioMode == AudioRecordMode::Microphone) {
        args << QStringLiteral("-f") << QStringLiteral("pulse") << QStringLiteral("-i") << QStringLiteral("default");
    } else if (m_audioMode == AudioRecordMode::BothMixed) {
        QString monitor = getActiveMonitorSource();
        args << QStringLiteral("-f") << QStringLiteral("pulse") << QStringLiteral("-i") << monitor;
        args << QStringLiteral("-f") << QStringLiteral("pulse") << QStringLiteral("-i") << QStringLiteral("default");
        args << QStringLiteral("-filter_complex") << QStringLiteral("amix=inputs=2:duration=first:dropout_transition=2");
    }

    // Video codec H.264
    args << QStringLiteral("-c:v") << QStringLiteral("libx264");
    args << QStringLiteral("-preset") << QStringLiteral("ultrafast");
    args << QStringLiteral("-crf") << QStringLiteral("22");
    args << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    args << QStringLiteral("-g") << QStringLiteral("30");
    args << QStringLiteral("-keyint_min") << QStringLiteral("30");
    args << QStringLiteral("-movflags") << QStringLiteral("+faststart");

    // Audio codec AAC if audio enabled
    if (m_audioMode != AudioRecordMode::Muted) {
        args << QStringLiteral("-c:a") << QStringLiteral("aac");
        args << QStringLiteral("-b:a") << QStringLiteral("192k");
    }

    args << m_outputFile;

    connect(m_ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RecordWidget::onProcessFinished);

    m_ffmpegProcess->start(QStringLiteral("ffmpeg"), args);

    if (!m_ffmpegProcess->waitForStarted(2000)) {
        SystemNotification().sendMessage(
            tr("Recording error"),
            tr("Failed to start ffmpeg process.")
        );
        close();
    }
}

void RecordWidget::stopRecording()
{
    m_timer->stop();

    if (m_canvasWidget) {
        m_canvasWidget->hide();
    }

    if (m_ffmpegProcess && m_ffmpegProcess->state() != QProcess::NotRunning) {
        m_startStopButton->setEnabled(false);
        m_statusLabel->setText(tr("SAVING..."));
        m_statusLabel->setStyleSheet(QStringLiteral("color: #f59e0b; font-weight: bold; font-size: 11px;"));

        m_ffmpegProcess->write("q\n");
        if (!m_ffmpegProcess->waitForFinished(3000)) {
            // Stage 2: Attempt graceful SIGTERM so ffmpeg can finalize the MP4 index
            m_ffmpegProcess->terminate();
            if (!m_ffmpegProcess->waitForFinished(2000)) {
                // Stage 3: Force kill only as absolute last resort
                m_ffmpegProcess->kill();
            }
        }
    } else {
        close();
    }
}

void RecordWidget::startAudioMonitor()
{
    stopAudioMonitor();
    if (!m_vuMeterWidget) return;

    if (m_audioMode == AudioRecordMode::Muted) {
        m_vuMeterWidget->setMuted(true);
        return;
    }

    m_vuMeterWidget->setMuted(false);
    QString source = QStringLiteral("default");
    if (m_audioMode == AudioRecordMode::SystemSound || m_audioMode == AudioRecordMode::BothMixed) {
        QString mon = getActiveMonitorSource();
        if (!mon.isEmpty() && mon != QStringLiteral("default")) {
            source = mon;
        }
    }

    QStringList args;
    args << QStringLiteral("-r")
         << QStringLiteral("-d") << source
         << QStringLiteral("--latency-msec=100")
         << QStringLiteral("--format=s16le")
         << QStringLiteral("--channels=1")
         << QStringLiteral("--rate=8000");

    m_audioLevelProcess->start(QStringLiteral("pacat"), args);
}

void RecordWidget::stopAudioMonitor()
{
    if (m_audioLevelProcess && m_audioLevelProcess->state() != QProcess::NotRunning) {
        m_audioLevelProcess->terminate();
        if (!m_audioLevelProcess->waitForFinished(300)) {
            m_audioLevelProcess->kill();
        }
    }
    m_currentAudioLevel = 0.0;
    if (m_vuMeterWidget) {
        m_vuMeterWidget->setLevel(0.0);
    }
}

void RecordWidget::onAudioLevelReady()
{
    if (!m_audioLevelProcess) return;
    QByteArray data = m_audioLevelProcess->readAllStandardOutput();
    if (data.size() < static_cast<int>(sizeof(qint16))) return;

    const qint16* samples = reinterpret_cast<const qint16*>(data.constData());
    int count = data.size() / sizeof(qint16);
    int peak = 0;
    for (int i = 0; i < count; ++i) {
        int val = qAbs(static_cast<int>(samples[i]));
        if (val > peak) peak = val;
    }

    double normalized = qBound(0.0, peak / 28000.0, 1.0);
    if (normalized > m_currentAudioLevel) {
        m_currentAudioLevel = normalized;
    } else {
        m_currentAudioLevel = m_currentAudioLevel * 0.75 + normalized * 0.25;
    }

    if (m_vuMeterWidget) {
        m_vuMeterWidget->setLevel(m_currentAudioLevel);
    }
}

void RecordWidget::onAudioLevelDecay()
{
    if (m_currentAudioLevel > 0.01) {
        m_currentAudioLevel *= 0.82;
        if (m_vuMeterWidget) {
            m_vuMeterWidget->setLevel(m_currentAudioLevel);
        }
    } else if (m_currentAudioLevel > 0.0) {
        m_currentAudioLevel = 0.0;
        if (m_vuMeterWidget) {
            m_vuMeterWidget->setLevel(0.0);
        }
    }
}

void RecordWidget::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);

    stopAudioMonitor();

    if (m_canvasWidget) {
        m_canvasWidget->hide();
        m_canvasWidget->close();
        m_canvasWidget = nullptr;
    }

    if (QFile::exists(m_outputFile) && QFileInfo(m_outputFile).size() > 1024) {
        hide();
        auto* preview = new RecordPreviewDialog(m_outputFile);
        preview->setAttribute(Qt::WA_DeleteOnClose);
        connect(preview, &QDialog::finished, this, [this](int /*result*/) {
            close();
        });
        preview->show();
        preview->raise();
        preview->activateWindow();
    } else {
        SystemNotification().sendMessage(
            tr("Recording error"),
            tr("Recording file was empty or failed to save.")
        );
        close();
    }
}

void RecordWidget::updateTimer()
{
    ++m_secondsElapsed;
    int mins = m_secondsElapsed / 60;
    int secs = m_secondsElapsed % 60;
    m_timeLabel->setText(QStringLiteral("%1:%2")
        .arg(mins, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0')));
}

void RecordWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_toolsContainer && m_toolsContainer->isVisible()) {
            toggleTools();
            event->accept();
            return;
        }
        if (!m_isRecording) {
            close();
        } else {
            stopRecording();
        }
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return) {
        onStartStopClicked();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void RecordWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
        event->accept();
    }
}

void RecordWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPos() - m_dragPosition);
        event->accept();
    }
}
