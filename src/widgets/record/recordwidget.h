#pragma once

#include <QWidget>
#include <QRect>
#include <QProcess>
#include <QTimer>
#include <QLabel>
#include <QPushButton>

enum class AudioRecordMode {
    SystemSound, // @DEFAULT_MONITOR@
    Microphone,  // default
    BothMixed,   // system + mic
    Muted        // no sound
};

class RecordWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RecordWidget(const QRect& captureArea, AudioRecordMode audioMode = AudioRecordMode::SystemSound, QWidget* parent = nullptr);
    ~RecordWidget() override;

    static void startRecording(const QRect& captureArea, AudioRecordMode audioMode = AudioRecordMode::SystemSound);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void updateTimer();
    void cycleAudioMode();
    void stopRecording();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void initControlBar();
    void startFFmpeg();

    QRect m_area;
    AudioRecordMode m_audioMode;
    int m_secondsElapsed;
    QString m_outputFile;

    QProcess* m_ffmpegProcess;
    QTimer* m_timer;

    QLabel* m_recDotLabel;
    QLabel* m_timeLabel;
    QPushButton* m_audioButton;
    QPushButton* m_stopButton;

    QPoint m_dragPosition;
};
