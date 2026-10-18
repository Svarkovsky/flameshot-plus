#include "recordwidget.h"
#include "src/core/flameshot.h"
#include "src/core/flameshotdaemon.h"
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

RecordWidget::RecordWidget(const QRect& captureArea, AudioRecordMode audioMode, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
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
    setWindowTitle(QStringLiteral("Flameshot + Recording"));

    // Make area coordinates even numbers for H.264
    int x = m_area.x();
    int y = m_area.y();
    int w = (m_area.width() / 2) * 2;
    int h = (m_area.height() / 2) * 2;
    m_area = QRect(x, y, qMax(16, w), qMax(16, h));

    initControlBar();
    startFFmpeg();

    connect(m_timer, &QTimer::timeout, this, &RecordWidget::updateTimer);
    m_timer->start(1000);
}

RecordWidget::~RecordWidget()
{
    if (m_ffmpegProcess && m_ffmpegProcess->state() != QProcess::NotRunning) {
        m_ffmpegProcess->write("q\n");
        if (!m_ffmpegProcess->waitForFinished(1500)) {
            m_ffmpegProcess->kill();
        }
    }

    Flameshot::instance()->setExternalWidget(false);
    if (FlameshotDaemon::instance() == nullptr) {
        qApp->exit(0);
    }
}

void RecordWidget::startRecording(const QRect& captureArea, AudioRecordMode audioMode)
{
    auto* widget = new RecordWidget(captureArea, audioMode);
    widget->show();
    widget->raise();
    widget->activateWindow();
}

void RecordWidget::initControlBar()
{
    setStyleSheet(
        "RecordWidget { "
        "   background-color: #1a1e28; "
        "   border: 2px solid #ef4444; "
        "   border-radius: 10px; "
        "} "
        "QLabel { "
        "   background: transparent; "
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
    m_recDotLabel->setStyleSheet("color: #ef4444; font-weight: bold; font-size: 13px;");
    layout->addWidget(m_recDotLabel);

    m_timeLabel = new QLabel("00:00", this);
    m_timeLabel->setStyleSheet("color: #ffffff; font-family: monospace; font-size: 15px; font-weight: bold;");
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
        m_audioButton->setStyleSheet("background-color: rgba(16, 185, 129, 0.3); color: #34d399; border: 1px solid #10b981;");
        break;
    case AudioRecordMode::Microphone:
        audioText = "🎤 Микрофон";
        m_audioButton->setStyleSheet("background-color: rgba(59, 130, 246, 0.3); color: #60a5fa; border: 1px solid #3b82f6;");
        break;
    case AudioRecordMode::BothMixed:
        audioText = "🔊+🎤 Всё";
        m_audioButton->setStyleSheet("background-color: rgba(168, 85, 247, 0.3); color: #c084fc; border: 1px solid #a855f7;");
        break;
    case AudioRecordMode::Muted:
        audioText = "🔇 Без звука";
        m_audioButton->setStyleSheet("background-color: rgba(100, 116, 139, 0.3); color: #94a3b8; border: 1px solid #64748b;");
        break;
    }
    m_audioButton->setText(audioText);

    // Position control bar strictly OUTSIDE the recorded area
    int barW = 350;
    int barH = 46;
    int barX = m_area.x() + (m_area.width() - barW) / 2;
    int barY = 0;

    QScreen* screen = QGuiApplication::primaryScreen();
    int screenH = screen ? screen->geometry().height() : 1080;
    int screenW = screen ? screen->geometry().width() : 1920;

    if (m_area.y() >= barH + 8) {
        // Place above the recorded area
        barY = m_area.y() - barH - 8;
    } else if (m_area.y() + m_area.height() + barH + 8 <= screenH) {
        // Place below the recorded area
        barY = m_area.y() + m_area.height() + 8;
    } else {
        // Area covers full height: place in top-right corner
        barY = 10;
        barX = screenW - barW - 10;
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
        m_recDotLabel->setStyleSheet("color: #ef4444; font-weight: bold; font-size: 13px;");
    } else {
        m_recDotLabel->setStyleSheet("color: rgba(239, 68, 68, 0.2); font-weight: bold; font-size: 13px;");
    }
}

void RecordWidget::cycleAudioMode()
{
    switch (m_audioMode) {
    case AudioRecordMode::SystemSound:
        m_audioMode = AudioRecordMode::Microphone;
        m_audioButton->setText("🎤 Микрофон");
        m_audioButton->setStyleSheet("background-color: rgba(59, 130, 246, 0.3); color: #60a5fa; border: 1px solid #3b82f6;");
        break;
    case AudioRecordMode::Microphone:
        m_audioMode = AudioRecordMode::BothMixed;
        m_audioButton->setText("🔊+🎤 Всё");
        m_audioButton->setStyleSheet("background-color: rgba(168, 85, 247, 0.3); color: #c084fc; border: 1px solid #a855f7;");
        break;
    case AudioRecordMode::BothMixed:
        m_audioMode = AudioRecordMode::Muted;
        m_audioButton->setText("🔇 Без звука");
        m_audioButton->setStyleSheet("background-color: rgba(100, 116, 139, 0.3); color: #94a3b8; border: 1px solid #64748b;");
        break;
    case AudioRecordMode::Muted:
        m_audioMode = AudioRecordMode::SystemSound;
        m_audioButton->setText("🔊 Система");
        m_audioButton->setStyleSheet("background-color: rgba(16, 185, 129, 0.3); color: #34d399; border: 1px solid #10b981;");
        break;
    }
}

void RecordWidget::stopRecording()
{
    m_timer->stop();
    m_stopButton->setEnabled(false);
    m_stopButton->setText("Сохранение...");

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
