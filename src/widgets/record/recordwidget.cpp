#include "recordwidget.h"
#include "src/utils/systemnotification.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QStandardPaths>

RecordFrame::RecordFrame(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint)
    , m_color(QColor(239, 68, 68, 230))
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_DeleteOnClose);
}

void RecordFrame::setBorderColor(const QColor& color)
{
    m_color = color;
    update();
}

void RecordFrame::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.fillRect(rect(), m_color);
}

RecordWidget::RecordWidget(const QRect& captureArea, AudioRecordMode audioMode, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint)
    , m_area(captureArea)
    , m_audioMode(audioMode)
    , m_secondsElapsed(0)
    , m_ffmpegProcess(new QProcess(this))
    , m_timer(new QTimer(this))
    , m_recDotLabel(nullptr)
    , m_timeLabel(nullptr)
    , m_audioButton(nullptr)
    , m_stopButton(nullptr)
{
    setAttribute(Qt::WA_DeleteOnClose);

    // Make area coordinates even numbers for H.264
    int x = m_area.x();
    int y = m_area.y();
    int w = (m_area.width() / 2) * 2;
    int h = (m_area.height() / 2) * 2;
    m_area = QRect(x, y, qMax(16, w), qMax(16, h));

    createExternalBorders();
    initControlBar();
    startFFmpeg();

    connect(m_timer, &QTimer::timeout, this, &RecordWidget::updateTimer);
    m_timer->start(1000);
}

RecordWidget::~RecordWidget()
{
    for (auto* frame : m_borderFrames) {
        if (frame) {
            frame->close();
        }
    }
    m_borderFrames.clear();

    if (m_ffmpegProcess && m_ffmpegProcess->state() != QProcess::NotRunning) {
        m_ffmpegProcess->write("q\n");
        if (!m_ffmpegProcess->waitForFinished(1500)) {
            m_ffmpegProcess->kill();
        }
    }
}

void RecordWidget::startRecording(const QRect& captureArea, AudioRecordMode audioMode)
{
    auto* widget = new RecordWidget(captureArea, audioMode);
    widget->show();
    widget->raise();
    widget->activateWindow();
}

void RecordWidget::createExternalBorders()
{
    // 4 separate thin widgets strictly OUTSIDE m_area so FFmpeg never captures them
    int x = m_area.x();
    int y = m_area.y();
    int w = m_area.width();
    int h = m_area.height();

    // Top border (2px thick, placed at y - 2)
    auto* top = new RecordFrame();
    top->setGeometry(x - 2, y - 2, w + 4, 2);
    top->show();
    m_borderFrames.append(top);

    // Bottom border (2px thick, placed at y + h)
    auto* bottom = new RecordFrame();
    bottom->setGeometry(x - 2, y + h, w + 4, 2);
    bottom->show();
    m_borderFrames.append(bottom);

    // Left border (2px thick, placed at x - 2)
    auto* left = new RecordFrame();
    left->setGeometry(x - 2, y - 2, 2, h + 4);
    left->show();
    m_borderFrames.append(left);

    // Right border (2px thick, placed at x + w)
    auto* right = new RecordFrame();
    right->setGeometry(x + w, y - 2, 2, h + 4);
    right->show();
    m_borderFrames.append(right);
}

void RecordWidget::updateBorderVisibility(bool visible)
{
    for (auto* frame : m_borderFrames) {
        if (frame) {
            frame->setVisible(visible);
        }
    }
}

void RecordWidget::initControlBar()
{
    setStyleSheet(
        "QWidget { "
        "   background-color: rgba(18, 22, 30, 0.95); "
        "   border: 1px solid rgba(255, 255, 255, 0.18); "
        "   border-radius: 12px; "
        "   color: #ffffff; "
        "} "
        "QPushButton { "
        "   padding: 6px 12px; "
        "   border-radius: 6px; "
        "   font-weight: bold; "
        "   font-size: 13px; "
        "   border: none; "
        "   cursor: pointer; "
        "} "
        "QPushButton:hover { "
        "   opacity: 0.9; "
        "}"
    );

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(10);

    m_recDotLabel = new QLabel("● REC", this);
    m_recDotLabel->setStyleSheet("color: #ef4444; font-weight: bold; font-size: 13px; border: none;");
    layout->addWidget(m_recDotLabel);

    m_timeLabel = new QLabel("00:00", this);
    m_timeLabel->setStyleSheet("color: #f1f5f9; font-family: monospace; font-size: 14px; font-weight: bold; border: none;");
    layout->addWidget(m_timeLabel);

    m_audioButton = new QPushButton(this);
    connect(m_audioButton, &QPushButton::clicked, this, &RecordWidget::cycleAudioMode);
    layout->addWidget(m_audioButton);

    m_stopButton = new QPushButton("■ Стоп", this);
    m_stopButton->setStyleSheet("background-color: #ef4444; color: #ffffff;");
    connect(m_stopButton, &QPushButton::clicked, this, &RecordWidget::stopRecording);
    layout->addWidget(m_stopButton);

    // Update audio button label and style based on initial mode
    QString audioText;
    switch (m_audioMode) {
    case AudioRecordMode::SystemSound:
        audioText = "🔊 Система";
        m_audioButton->setStyleSheet("background-color: rgba(16, 185, 129, 0.25); color: #34d399; border: 1px solid #10b981;");
        break;
    case AudioRecordMode::Microphone:
        audioText = "🎤 Микрофон";
        m_audioButton->setStyleSheet("background-color: rgba(59, 130, 246, 0.25); color: #60a5fa; border: 1px solid #3b82f6;");
        break;
    case AudioRecordMode::BothMixed:
        audioText = "🔊+🎤 Всё";
        m_audioButton->setStyleSheet("background-color: rgba(168, 85, 247, 0.25); color: #c084fc; border: 1px solid #a855f7;");
        break;
    case AudioRecordMode::Muted:
        audioText = "🔇 Без звука";
        m_audioButton->setStyleSheet("background-color: rgba(100, 116, 139, 0.25); color: #94a3b8; border: 1px solid #64748b;");
        break;
    }
    m_audioButton->setText(audioText);

    // Position control bar strictly OUTSIDE the recorded area
    int barW = 360;
    int barH = 46;
    int barX = m_area.x() + (m_area.width() - barW) / 2;
    int barY = 0;

    QScreen* screen = QGuiApplication::primaryScreen();
    int screenH = screen ? screen->geometry().height() : 1080;
    int screenW = screen ? screen->geometry().width() : 1920;

    if (m_area.y() >= barH + 8) {
        // Place above the recorded area
        barY = m_area.y() - barH - 6;
    } else if (m_area.y() + m_area.height() + barH + 8 <= screenH) {
        // Place below the recorded area
        barY = m_area.y() + m_area.height() + 6;
    } else {
        // If recording area spans full height, place in top corner
        barY = 10;
        barX = 10;
    }

    barX = qBound(10, barX, screenW - barW - 10);
    setGeometry(barX, barY, barW, barH);
}

void RecordWidget::startFFmpeg()
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (baseDir.isEmpty()) {
        baseDir = QDir::homePath();
    }
    QDir().mkpath(baseDir);

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    m_outputFile = QString("%1/flameshot_plus_%2.mp4").arg(baseDir, timestamp);

    QStringList args;
    args << "-y";
    args << "-f" << "x11grab";
    args << "-draw_mouse" << "1";
    args << "-video_size" << QString("%1x%2").arg(m_area.width()).arg(m_area.height());
    args << "-framerate" << "30";
    args << "-i" << QString(":0.0+%1,%2").arg(m_area.x()).arg(m_area.y());

    // Audio input setup
    switch (m_audioMode) {
    case AudioRecordMode::SystemSound:
        args << "-f" << "pulse" << "-i" << "@DEFAULT_MONITOR@";
        break;
    case AudioRecordMode::Microphone:
        args << "-f" << "pulse" << "-i" << "default";
        break;
    case AudioRecordMode::BothMixed:
        args << "-f" << "pulse" << "-i" << "@DEFAULT_MONITOR@";
        args << "-f" << "pulse" << "-i" << "default";
        args << "-filter_complex" << "[0:a][1:a]amix=inputs=2:duration=longest[aout]";
        args << "-map" << "0:v" << "-map" << "[aout]";
        break;
    case AudioRecordMode::Muted:
        break;
    }

    args << "-c:v" << "libx264";
    args << "-preset" << "ultrafast";
    args << "-crf" << "23";
    args << "-pix_fmt" << "yuv420p";

    if (m_audioMode != AudioRecordMode::Muted) {
        args << "-c:a" << "aac" << "-b:a" << "192k";
    }

    args << m_outputFile;

    connect(m_ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RecordWidget::onProcessFinished);

    m_ffmpegProcess->start("ffmpeg", args);
}

void RecordWidget::updateTimer()
{
    m_secondsElapsed++;
    int mins = m_secondsElapsed / 60;
    int secs = m_secondsElapsed % 60;
    m_timeLabel->setText(QString("%1:%2")
        .arg(mins, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0')));

    // Blink red REC indicator
    if (m_secondsElapsed % 2 == 0) {
        m_recDotLabel->setStyleSheet("color: #ef4444; font-weight: bold; font-size: 13px; border: none;");
        for (auto* frame : m_borderFrames) {
            if (frame) frame->setBorderColor(QColor(239, 68, 68, 230));
        }
    } else {
        m_recDotLabel->setStyleSheet("color: rgba(239, 68, 68, 0.3); font-weight: bold; font-size: 13px; border: none;");
        for (auto* frame : m_borderFrames) {
            if (frame) frame->setBorderColor(QColor(239, 68, 68, 120));
        }
    }
}

void RecordWidget::cycleAudioMode()
{
    // Mode indicator for the user
    switch (m_audioMode) {
    case AudioRecordMode::SystemSound:
        m_audioMode = AudioRecordMode::Microphone;
        m_audioButton->setText("🎤 Микрофон");
        m_audioButton->setStyleSheet("background-color: rgba(59, 130, 246, 0.25); color: #60a5fa; border: 1px solid #3b82f6;");
        break;
    case AudioRecordMode::Microphone:
        m_audioMode = AudioRecordMode::BothMixed;
        m_audioButton->setText("🔊+🎤 Всё");
        m_audioButton->setStyleSheet("background-color: rgba(168, 85, 247, 0.25); color: #c084fc; border: 1px solid #a855f7;");
        break;
    case AudioRecordMode::BothMixed:
        m_audioMode = AudioRecordMode::Muted;
        m_audioButton->setText("🔇 Без звука");
        m_audioButton->setStyleSheet("background-color: rgba(100, 116, 139, 0.25); color: #94a3b8; border: 1px solid #64748b;");
        break;
    case AudioRecordMode::Muted:
        m_audioMode = AudioRecordMode::SystemSound;
        m_audioButton->setText("🔊 Система");
        m_audioButton->setStyleSheet("background-color: rgba(16, 185, 129, 0.25); color: #34d399; border: 1px solid #10b981;");
        break;
    }
}

void RecordWidget::stopRecording()
{
    m_timer->stop();
    m_stopButton->setEnabled(false);
    m_stopButton->setText("Сохранение...");

    for (auto* frame : m_borderFrames) {
        if (frame) frame->hide();
    }

    if (m_ffmpegProcess && m_ffmpegProcess->state() != QProcess::NotRunning) {
        m_ffmpegProcess->write("q\n");
    } else {
        close();
    }
}

void RecordWidget::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);

    SystemNotification notification;
    notification.sendMessage(
        QString("Видеозапись сохранена в:\n%1").arg(m_outputFile),
        "Flameshot +",
        m_outputFile
    );

    close();
}

void RecordWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Q) {
        stopRecording();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void RecordWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
    }
}

void RecordWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPos() - m_dragPosition);
    }
}
