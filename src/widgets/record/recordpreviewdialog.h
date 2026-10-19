#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QKeyEvent>

class QNetworkAccessManager;
class QNetworkReply;
class BlockTrimSlider;

class RecordPreviewDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RecordPreviewDialog(const QString& videoPath, QWidget* parent = nullptr);
    ~RecordPreviewDialog() override;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onPlayClicked();
    void onTrashClicked();
    void onSaveClicked();
    void onUploadClicked();
    void onUploadProgress(qint64 bytesSent, qint64 bytesTotal);
    void onUploadFinished(QNetworkReply* reply);
    void onTrimChanged();
    void onSliderRangeChanged(double start, double end);
    void onTrimButtonClicked();

private:
    void extractMetadataAndThumbnail();
    void reloadThumbnail();
    void applyTrimIfNeeded();
    QString formatDuration(double seconds) const;

    QString m_videoPath;
    double m_duration;
    qint64 m_fileSize;
    QString m_resolution;
    QString m_uploadedUrl;

    QLabel* m_thumbLabel;
    QLabel* m_infoLabel;
    QLabel* m_trimmedDurationLabel;
    BlockTrimSlider* m_blockSlider;
    QDoubleSpinBox* m_startSpin;
    QDoubleSpinBox* m_endSpin;
    QPushButton* m_trimButton;
    QPushButton* m_saveButton;
    QPushButton* m_playButton;
    QPushButton* m_trashButton;
    QPushButton* m_uploadButton;
    QNetworkAccessManager* m_nam;
};
