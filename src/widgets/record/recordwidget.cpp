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
#include <QRegExp>
#include <QScreen>
#include <QStandardPaths>

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
        "   border-radius: 12px; "
        "} "
        "QLabel { "
        "   background: transparent; "
        "} "
        "QPushButton { "
        "   padding: 7px 14px; "
        "   border-radius: 8px; "
        "   font-weight: bold; "
        "   font-size: 13px; "
        "   border: none; "
        "   cursor: pointer; "
        "   white-space: nowrap; "
        "} "
        "QPushButton:hover { "
        "   opacity: 0.9; "
        "}"
    );

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 6, 14, 6);
    layout->setSpacing(12);

    m_recDotLabel = new QLabel(QStringLiteral("● REC"), this);
    m_recDotLabel->setStyleSheet(QStringLiteral("color: #ef4444; font-weight: bold; font-size: 13px;"));
    m_recDotLabel->setFixedWidth(55);
    layout->addWidget(m_recDotLabel);

    m_timeLabel = new QLabel(QStringLiteral("00:00"), this);
    m_timeLabel->setStyleSheet(QStringLiteral("color: #ffffff; font-family: monospace; font-size: 15px; font-weight: bold;"));
    m_timeLabel->setFixedWidth(60);
    layout->addWidget(m_timeLabel);

    m_audioButton = new QPushButton(this);
    m_audioButton->setMinimumWidth(180);
    connect(m_audioButton, &QPushButton::clicked, this, &RecordWidget::cycleAudioMode);
    layout->addWidget(m_audioButton);

    m_stopButton = new QPushButton(QStringLiteral("■ Стоп"), this);
    m_stopButton->setMinimumWidth(95);
    m_stopButton->setStyleSheet(QStringLiteral("background-color: #ef4444; color: #ffffff; font-size: 13px; font-weight: bold;"));
    connect(m_stopButton, &QPushButton::clicked, this, &RecordWidget::stopRecording);
    layout->addWidget(m_stopButton);

    // Update audio button label and style based on initial mode
    QString audioText;
    switch (m_audioMode) {
    case AudioRecordMode::SystemSound:
        audioText = QStringLiteral("🔊 Звук: Система");
        m_audioButton->setStyleSheet(QStringLiteral("background-color: rgba(16, 185, 129, 0.3); color: #34d399; border: 1px solid #10b981;"));
        break;
    case AudioRecordMode::Microphone:
        audioText = QStringLiteral("🎤 Звук: Микрофон");
        m_audioButton->setStyleSheet(QStringLiteral("background-color: rgba(59, 130, 246, 0.3); color: #60a5fa; border: 1px solid #3b82f6;"));
        break;
    case AudioRecordMode::BothMixed:
        audioText = QStringLiteral("🔊+🎤 Звук: Всё");
        m_audioButton->setStyleSheet(QStringLiteral("background-color: rgba(168, 85, 247, 0.3); color: #c084fc; border: 1px solid #a855f7;"));
        break;
    case AudioRecordMode::Muted:
        audioText = QStringLiteral("🔇 Звук: Выкл");
        m_audioButton->setStyleSheet(QStringLiteral("background-color: rgba(100, 116, 139, 0.3); color: #94a3b8; border: 1px solid #64748b;"));
        break;
    }
    m_audioButton->setText(audioText);

    // Width increased to 490px so all labels fit comfortably with generous padding
    int barW = 490;
    int barH = 48;
    int barX = m_area.x() + (m_area.width() - barW) / 2;
    int barY = 0;

    QScreen* screen = QGuiApplication::primaryScreen();
    int screenH = screen ? screen->geometry().height() : 1080;
    int screenW = screen ? screen->geometry().width() : 1920;

    if (m_area.y() >= barH + 10) {
        barY = m_area.y() - barH - 8;
    } else if (m_area.y() + m_area.height() + barH + 10 <= screenH) {
        barY = m_area.y() + m_area.height() + 8;
    } else {
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

    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    m_outputFile = QStringLiteral("%1/flameshot_plus_%2.mp4").arg(baseDir, timestamp);

    QString activeMonitor = getActiveMonitorSource();

    QStringList args;
    args << QStringLiteral("-y");
    args << QStringLiteral("-thread_queue_size") << QStringLiteral("512");
    args << QStringLiteral("-f") << QStringLiteral("x11grab");
    args << QStringLiteral("-draw_mouse") << QStringLiteral("1");
    args << QStringLiteral("-video_size") << QStringLiteral("%1x%2").arg(m_area.width()).arg(m_area.height());
    args << QStringLiteral("-framerate") << QStringLiteral("30");
    args << QStringLiteral("-i") << QStringLiteral(":0.0+%1,%2").arg(m_area.x()).arg(m_area.y());

    // Audio input setup using detected active monitor
    switch (m_audioMode) {
    case AudioRecordMode::SystemSound:
        args << QStringLiteral("-thread_queue_size") << QStringLiteral("512");
        args << QStringLiteral("-f") << QStringLiteral("pulse") << QStringLiteral("-i") << activeMonitor;
        break;
    case AudioRecordMode::Microphone:
        args << QStringLiteral("-thread_queue_size") << QStringLiteral("512");
        args << QStringLiteral("-f") << QStringLiteral("pulse") << QStringLiteral("-i") << QStringLiteral("default");
        break;
    case AudioRecordMode::BothMixed:
        args << QStringLiteral("-thread_queue_size") << QStringLiteral("512");
        args << QStringLiteral("-f") << QStringLiteral("pulse") << QStringLiteral("-i") << activeMonitor;
        args << QStringLiteral("-thread_queue_size") << QStringLiteral("512");
        args << QStringLiteral("-f") << QStringLiteral("pulse") << QStringLiteral("-i") << QStringLiteral("default");
        args << QStringLiteral("-filter_complex") << QStringLiteral("[0:a][1:a]amix=inputs=2:duration=longest[aout]");
        args << QStringLiteral("-map") << QStringLiteral("0:v") << QStringLiteral("-map") << QStringLiteral("[aout]");
        break;
    case AudioRecordMode::Muted:
        break;
    }

    args << QStringLiteral("-c:v") << QStringLiteral("libx264");
    args << QStringLiteral("-preset") << QStringLiteral("ultrafast");
    args << QStringLiteral("-crf") << QStringLiteral("23");
    args << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");

    if (m_audioMode != AudioRecordMode::Muted) {
        args << QStringLiteral("-c:a") << QStringLiteral("aac") << QStringLiteral("-b:a") << QStringLiteral("192k");
    }

    args << m_outputFile;

    connect(m_ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RecordWidget::onProcessFinished);

    m_ffmpegProcess->setStandardErrorFile(QStringLiteral("/tmp/flameshot_ffmpeg.log"));
    m_ffmpegProcess->start(QStringLiteral("ffmpeg"), args);
}

void RecordWidget::updateTimer()
{
    m_secondsElapsed++;
    int mins = m_secondsElapsed / 60;
    int secs = m_secondsElapsed % 60;
    m_timeLabel->setText(QStringLiteral("%1:%2")
        .arg(mins, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0')));

    // Blink red REC indicator
    if (m_secondsElapsed % 2 == 0) {
        m_recDotLabel->setStyleSheet(QStringLiteral("color: #ef4444; font-weight: bold; font-size: 13px;"));
    } else {
        m_recDotLabel->setStyleSheet(QStringLiteral("color: rgba(239, 68, 68, 0.2); font-weight: bold; font-size: 13px;"));
    }
}

void RecordWidget::cycleAudioMode()
{
    switch (m_audioMode) {
    case AudioRecordMode::SystemSound:
        m_audioMode = AudioRecordMode::Microphone;
        m_audioButton->setText(QStringLiteral("🎤 Звук: Микрофон"));
        m_audioButton->setStyleSheet(QStringLiteral("background-color: rgba(59, 130, 246, 0.3); color: #60a5fa; border: 1px solid #3b82f6;"));
        break;
    case AudioRecordMode::Microphone:
        m_audioMode = AudioRecordMode::BothMixed;
        m_audioButton->setText(QStringLiteral("🔊+🎤 Звук: Всё"));
        m_audioButton->setStyleSheet(QStringLiteral("background-color: rgba(168, 85, 247, 0.3); color: #c084fc; border: 1px solid #a855f7;"));
        break;
    case AudioRecordMode::BothMixed:
        m_audioMode = AudioRecordMode::Muted;
        m_audioButton->setText(QStringLiteral("🔇 Звук: Выкл"));
        m_audioButton->setStyleSheet(QStringLiteral("background-color: rgba(100, 116, 139, 0.3); color: #94a3b8; border: 1px solid #64748b;"));
        break;
    case AudioRecordMode::Muted:
        m_audioMode = AudioRecordMode::SystemSound;
        m_audioButton->setText(QStringLiteral("🔊 Звук: Система"));
        m_audioButton->setStyleSheet(QStringLiteral("background-color: rgba(16, 185, 129, 0.3); color: #34d399; border: 1px solid #10b981;"));
        break;
    }
}

void RecordWidget::stopRecording()
{
    m_timer->stop();
    m_stopButton->setEnabled(false);
    m_stopButton->setText(QStringLiteral("Сохранение..."));

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
        QStringLiteral("Видеозапись сохранена в:\n%1").arg(m_outputFile),
        QStringLiteral("Flameshot +"),
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
