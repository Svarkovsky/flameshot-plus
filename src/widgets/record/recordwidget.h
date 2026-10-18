#pragma once

#include <QWidget>
#include <QRect>
#include <QProcess>
#include <QTimer>
#include <QLabel>
#include <QPushButton>

class RecordWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RecordWidget(const QRect& captureArea, bool enableAudio = true, QWidget* parent = nullptr);
    ~RecordWidget() override;

    static void startRecording(const QRect& captureArea, bool enableAudio = true);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void updateTimer();
    void toggleAudio();
    void stopRecording();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void initControlBar();
    void startFFmpeg();

    QRect m_area;
    bool m_enableAudio;
    int m_secondsElapsed;
    QString m_outputFile;

    QProcess* m_ffmpegProcess;
    QTimer* m_timer;

    QWidget* m_controlBar;
    QLabel* m_recDotLabel;
    QLabel* m_timeLabel;
    QPushButton* m_audioButton;
    QPushButton* m_stopButton;

    QPoint m_dragPosition;
};
