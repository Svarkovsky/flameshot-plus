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

RecordWidget::RecordWidget(const QRect& captureArea, bool enableAudio, QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
    , m_area(captureArea)
    , m_enableAudio(enableAudio)
    , m_secondsElapsed(0)
    , m_ffmpegProcess(new QProcess(this))
    , m_timer(new QTimer(this))
    , m_controlBar(nullptr)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);

    // Make area coordinates even numbers for H.264
    int x = m_area.x();
    int y = m_area.y();
    int w = (m_area.width() / 2) * 2;
    int h = (m_area.height() / 2) * 2;
    m_area = QRect(x, y, qMax(16, w), qMax(16, h));

    // Cover full screen so we can show both border and control bar
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        setGeometry(screen->geometry());
    } else {
        setGeometry(0, 0, 1920, 1080);
    }

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
}

void RecordWidget::startRecording(const QRect& captureArea, bool enableAudio)
{
    auto* widget = new RecordWidget(captureArea, enableAudio);
    widget->show();
    widget->raise();
    widget->activateWindow();
}

void RecordWidget::initControlBar()
{
    m_controlBar = new QWidget(this);
    m_controlBar->setStyleSheet(
        "QWidget { "
        "   background-color: rgba(20, 24, 33, 0.94); "
        "   border: 1px solid rgba(255, 255, 255, 0.15); "
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

    auto* layout = new QHBoxLayout(m_controlBar);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(10);

    m_recDotLabel = new QLabel("● REC", m_controlBar);
    m_recDotLabel->setStyleSheet("color: #ef4444; font-weight: bold; font-size: 13px; border: none;");
    layout->addWidget(m_recDotLabel);

    m_timeLabel = new QLabel("00:00", m_controlBar);
    m_timeLabel->setStyleSheet("color: #f1f5f9; font-family: monospace; font-size: 14px; font-weight: bold; border: none;");
    layout->addWidget(m_timeLabel);

    m_audioButton = new QPushButton(m_enableAudio ? "Звук: ВКЛ" : "Звук: ВЫКЛ", m_controlBar);
    m_audioButton->setStyleSheet(
        m_enableAudio 
            ? "background-color: rgba(16, 185, 129, 0.25); color: #34d399; border: 1px solid #10b981;"
            : "background-color: rgba(100, 116, 139, 0.25); color: #94a3b8; border: 1px solid #64748b;"
    );
    connect(m_audioButton, &QPushButton::clicked, this, &RecordWidget::toggleAudio);
    layout->addWidget(m_audioButton);

    m_stopButton = new QPushButton("■ Стоп", m_controlBar);
    m_stopButton->setStyleSheet("background-color: #ef4444; color: #ffffff;");
    connect(m_stopButton, &QPushButton::clicked, this, &RecordWidget::stopRecording);
    layout->addWidget(m_stopButton);

    // Position control bar slightly above or below the recording area
    int barW = 320;
    int barH = 46;
    int barX = m_area.x() + (m_area.width() - barW) / 2;
    int barY = m_area.y() + m_area.height() + 12;

    if (barY + barH > height()) {
        barY = m_area.y() - barH - 12;
    }
    if (barY < 10) {
        barY = 10;
    }
    barX = qBound(10, barX, width() - barW - 10);

    m_controlBar->setGeometry(barX, barY, barW, barH);
}

void RecordWidget::startFFmpeg()
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (baseDir.isEmpty()) {
        baseDir = QDir::homePath();
    }
    QDir().mkpath(baseDir);

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    m_outputFile = QString("%1/screenink_%2.mp4").arg(baseDir, timestamp);

    QStringList args;
    args << "-y";
    args << "-f" << "x11grab";
    args << "-draw_mouse" << "1";
    args << "-video_size" << QString("%1x%2").arg(m_area.width()).arg(m_area.height());
    args << "-framerate" << "30";
    args << "-i" << QString(":0.0+%1,%2").arg(m_area.x()).arg(m_area.y());

    if (m_enableAudio) {
        args << "-f" << "pulse" << "-i" << "default";
    }

    args << "-c:v" << "libx264";
    args << "-preset" << "ultrafast";
    args << "-crf" << "23";
    args << "-pix_fmt" << "yuv420p";

    if (m_enableAudio) {
        args << "-c:a" << "aac" << "-b:a" << "128k";
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
    } else {
        m_recDotLabel->setStyleSheet("color: rgba(239, 68, 68, 0.3); font-weight: bold; font-size: 13px; border: none;");
    }
}

void RecordWidget::toggleAudio()
{
    // Audio toggling mid-stream can be toggled by user for indicator
    m_enableAudio = !m_enableAudio;
    m_audioButton->setText(m_enableAudio ? "Звук: ВКЛ" : "Звук: ВЫКЛ");
    m_audioButton->setStyleSheet(
        m_enableAudio 
            ? "background-color: rgba(16, 185, 129, 0.25); color: #34d399; border: 1px solid #10b981;"
            : "background-color: rgba(100, 116, 139, 0.25); color: #94a3b8; border: 1px solid #64748b;"
    );
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
        "ScreenInk / Flameshot",
        m_outputFile
    );

    close();
}

void RecordWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    // Draw bright red/cyan border around the captured area
    QPen pen(QColor(239, 68, 68, 220), 2, Qt::DashLine);
    painter.setPen(pen);
    painter.drawRect(m_area.adjusted(-1, -1, 1, 1));
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
    if (m_controlBar && m_controlBar->geometry().contains(event->pos())) {
        m_dragPosition = event->globalPos() - m_controlBar->frameGeometry().topLeft();
    }
}

void RecordWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton && m_controlBar && m_controlBar->geometry().contains(event->pos())) {
        m_controlBar->move(event->globalPos() - m_dragPosition);
    }
}
