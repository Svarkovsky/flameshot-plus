// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Svarkovsky <https://github.com/Svarkovsky/flameshot-plus>

#pragma once

#include <QWidget>
#include <QRect>
#include <QProcess>
#include <QTimer>
#include <QLabel>
#include <QPushButton>

class RecordCanvasWidget;
class AudioVuMeterWidget;
class RecordPreviewDialog;

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
    static QString getVideoSaveDirectory();
    static QString generateOutputFilePath();

protected:
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void updateTimer();
    void cycleAudioMode();
    void onStartStopClicked();
    void onCancelClicked();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void toggleTools();
    void setToolPencil();
    void setToolLine();
    void setToolArrow();
    void setToolRectOutline();
    void setToolRectFill();
    void setToolCircle();
    void setToolMarker();
    void setToolText();
    void setToolStep();
    void setToolPixelate();
    void setToolInvert();
    void onCanvasColorChanged(const QColor& color);
    void onAudioLevelReady();
    void onAudioLevelDecay();

private:
    void initControlBar();
    void updateAudioButtonLabel();
    void updateWidgetGeometry();
    void highlightActiveTool(QPushButton* activeBtn);
    void startFFmpeg();
    void stopRecording();

    QRect m_area;
    AudioRecordMode m_audioMode;
    bool m_isRecording;
    int m_secondsElapsed;
    QString m_outputFile;

    QProcess* m_ffmpegProcess;
    QTimer* m_timer;

    QLabel* m_statusLabel;
    QLabel* m_timeLabel;
    QPushButton* m_audioButton;
    QPushButton* m_startStopButton;
    QPushButton* m_toolsToggleButton;
    QPushButton* m_cancelButton;
    AudioVuMeterWidget* m_vuMeterWidget;
    QProcess* m_audioLevelProcess;
    QTimer* m_audioLevelDecayTimer;
    double m_currentAudioLevel;
    void startAudioMonitor();
    void stopAudioMonitor();

    // Expandable tools panel
    QWidget* m_toolsContainer;
    QPushButton* m_pencilBtn;
    QPushButton* m_lineBtn;
    QPushButton* m_arrowBtn;
    QPushButton* m_rectOutlineBtn;
    QPushButton* m_rectFillBtn;
    QPushButton* m_circleBtn;
    QPushButton* m_markerBtn;
    QPushButton* m_textBtn;
    QPushButton* m_stepBtn;
    QPushButton* m_pixelateBtn;
    QPushButton* m_invertBtn;
    QPushButton* m_sizeIndicatorBtn;
    QPushButton* m_moveBtn;
    QPushButton* m_undoBtn;
    QPushButton* m_redoBtn;
    QPushButton* m_colorIndicatorBtn;
    QPushButton* m_clearBtn;

    RecordCanvasWidget* m_canvasWidget;
    QPoint m_dragPosition;
};
