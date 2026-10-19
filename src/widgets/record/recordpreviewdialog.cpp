#include "recordpreviewdialog.h"
#include "blocktrimslider.h"
#include "src/core/flameshotdaemon.h"
#include "src/utils/history.h"
#include "src/utils/systemnotification.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QPixmap>
#include <QPainter>
#include <QEvent>
#include <QProcess>
#include <QApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QFile>

RecordPreviewDialog::RecordPreviewDialog(const QString& videoPath, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::WindowStaysOnTopHint)
    , m_videoPath(videoPath)
    , m_duration(0.0)
    , m_fileSize(0)
    , m_resolution(QStringLiteral("1920x1080"))
    , m_uploadedUrl(QString())
    , m_thumbLabel(nullptr)
    , m_infoLabel(nullptr)
    , m_trimmedDurationLabel(nullptr)
    , m_blockSlider(nullptr)
    , m_startSpin(nullptr)
    , m_endSpin(nullptr)
    , m_trimButton(nullptr)
    , m_saveButton(nullptr)
    , m_playButton(nullptr)
    , m_trashButton(nullptr)
    , m_uploadButton(nullptr)
    , m_nam(new QNetworkAccessManager(this))
{
    setWindowTitle(QStringLiteral("Flameshot + ") + tr("Video Preview"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFixedWidth(510);
    setCursor(Qt::ArrowCursor);

    setStyleSheet(
        "QDialog { background-color: #0f172a; color: #f8fafc; font-family: sans-serif; }"
        "QLabel { color: #f8fafc; }"
        "QDoubleSpinBox { background-color: #1e293b; color: #f8fafc; border: 1px solid #475569; border-radius: 4px; padding: 3px 6px; font-weight: bold; font-family: monospace; }"
        "QDoubleSpinBox:focus { border: 1px solid #a855f7; }"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 14px; background-color: #334155; }"
    );

    extractMetadataAndThumbnail();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 14, 16, 14);
    mainLayout->setSpacing(10);

    // Top title & info row
    auto* topLayout = new QHBoxLayout();
    auto* titleLabel = new QLabel(tr("Video recorded"), this);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold; color: #f8fafc;"));
    topLayout->addWidget(titleLabel);

    topLayout->addStretch();

    QString sizeStr = QString::number(m_fileSize / (1024.0 * 1024.0), 'f', 1) + tr(" MB");
    m_infoLabel = new QLabel(QStringLiteral("%1  •  %2  •  %3")
                                 .arg(formatDuration(m_duration))
                                 .arg(sizeStr)
                                 .arg(m_resolution), this);
    m_infoLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: bold; color: #94a3b8; font-family: monospace;"));
    topLayout->addWidget(m_infoLabel);
    mainLayout->addLayout(topLayout);

    // Thumbnail Preview (clickable)
    m_thumbLabel = new QLabel(this);
    m_thumbLabel->setFixedSize(478, 220);
    m_thumbLabel->setStyleSheet(QStringLiteral("background-color: #020617; border: 1px solid #334155; border-radius: 6px;"));
    m_thumbLabel->setAlignment(Qt::AlignCenter);
    m_thumbLabel->setCursor(Qt::PointingHandCursor);
    m_thumbLabel->setToolTip(tr("Click to play in video player"));
    m_thumbLabel->installEventFilter(this);
    mainLayout->addWidget(m_thumbLabel);

    reloadThumbnail();

    // Trimming Box with Block Slider
    auto* trimBox = new QWidget(this);
    trimBox->setStyleSheet(QStringLiteral("background-color: #1e293b; border-radius: 6px; padding: 6px;"));
    auto* trimLayout = new QVBoxLayout(trimBox);
    trimLayout->setContentsMargins(10, 8, 10, 8);
    trimLayout->setSpacing(6);

    auto* trimHeaderLayout = new QHBoxLayout();
    auto* trimTitle = new QLabel(tr("Trim video edges:"), trimBox);
    trimTitle->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: bold; color: #cbd5e1;"));
    trimHeaderLayout->addWidget(trimTitle);

    trimHeaderLayout->addStretch();

    m_trimmedDurationLabel = new QLabel(tr("Total: %1").arg(formatDuration(m_duration)), trimBox);
    m_trimmedDurationLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: bold; color: #38bdf8; font-family: monospace;"));
    trimHeaderLayout->addWidget(m_trimmedDurationLabel);
    trimLayout->addLayout(trimHeaderLayout);

    // Block Trim Slider
    m_blockSlider = new BlockTrimSlider(trimBox);
    m_blockSlider->setDuration(m_duration);
    trimLayout->addWidget(m_blockSlider);

    auto* spinsLayout = new QHBoxLayout();
    spinsLayout->setSpacing(8);

    auto* startLabel = new QLabel(tr("Start:"), trimBox);
    startLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #94a3b8;"));
    spinsLayout->addWidget(startLabel);

    m_startSpin = new QDoubleSpinBox(trimBox);
    m_startSpin->setRange(0.0, qMax(0.0, m_duration - 0.5));
    m_startSpin->setSingleStep(0.5);
    m_startSpin->setValue(0.0);
    m_startSpin->setSuffix(tr(" s"));
    spinsLayout->addWidget(m_startSpin);

    spinsLayout->addSpacing(10);

    auto* endLabel = new QLabel(tr("End:"), trimBox);
    endLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #94a3b8;"));
    spinsLayout->addWidget(endLabel);

    m_endSpin = new QDoubleSpinBox(trimBox);
    m_endSpin->setRange(0.5, m_duration);
    m_endSpin->setSingleStep(0.5);
    m_endSpin->setValue(m_duration);
    m_endSpin->setSuffix(tr(" s"));
    spinsLayout->addWidget(m_endSpin);

    spinsLayout->addSpacing(12);

    m_trimButton = new QPushButton(tr("✂ Trim"), trimBox);
    m_trimButton->setCursor(Qt::PointingHandCursor);
    m_trimButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #2563eb; color: #ffffff; border: none; border-radius: 5px; padding: 4px 14px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #1d4ed8; }"
        "QPushButton:disabled { background-color: #475569; color: #94a3b8; }"
    ));
    m_trimButton->setToolTip(tr("Trim video to selected boundaries"));
    connect(m_trimButton, &QPushButton::clicked, this, &RecordPreviewDialog::onTrimButtonClicked);
    spinsLayout->addWidget(m_trimButton);

    spinsLayout->addStretch();
    trimLayout->addLayout(spinsLayout);
    mainLayout->addWidget(trimBox);

    connect(m_blockSlider, &BlockTrimSlider::rangeChanged, this, &RecordPreviewDialog::onSliderRangeChanged);
    connect(m_startSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &RecordPreviewDialog::onTrimChanged);
    connect(m_endSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &RecordPreviewDialog::onTrimChanged);

    // Action buttons row
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(8);

    m_trashButton = new QPushButton(this);
    m_trashButton->setCursor(Qt::PointingHandCursor);
    m_trashButton->setFixedSize(50, 32);
    m_trashButton->setIcon(QIcon(QStringLiteral(":/img/material/white/delete.svg")));
    m_trashButton->setIconSize(QSize(20, 20));
    m_trashButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: rgba(239, 68, 68, 0.15); border: 1px solid #ef4444; border-radius: 6px; padding: 0px; }"
        "QPushButton:hover { background-color: rgba(239, 68, 68, 0.35); border-color: #f87171; }"
    ));
    m_trashButton->setToolTip(tr("Delete recording (Del)"));
    connect(m_trashButton, &QPushButton::clicked, this, &RecordPreviewDialog::onTrashClicked);
    btnLayout->addWidget(m_trashButton);

    // Solid margin separating destructive trash button from action buttons
    btnLayout->addSpacing(22);

    m_playButton = new QPushButton(this);
    m_playButton->setCursor(Qt::PointingHandCursor);
    m_playButton->setFixedSize(50, 32);
    m_playButton->setIcon(QIcon(QStringLiteral(":/img/material/white/play.svg")));
    m_playButton->setIconSize(QSize(20, 20));
    m_playButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: rgba(59, 130, 246, 0.15); border: 1px solid #3b82f6; border-radius: 6px; padding: 0px; }"
        "QPushButton:hover { background-color: rgba(59, 130, 246, 0.35); border-color: #60a5fa; }"
    ));
    m_playButton->setToolTip(tr("Play in video player"));
    connect(m_playButton, &QPushButton::clicked, this, &RecordPreviewDialog::onPlayClicked);
    btnLayout->addWidget(m_playButton);

    m_uploadButton = new QPushButton(this);
    m_uploadButton->setCursor(Qt::PointingHandCursor);
    m_uploadButton->setFixedSize(50, 32);
    m_uploadButton->setIcon(QIcon(QStringLiteral(":/img/material/white/cloud-upload.svg")));
    m_uploadButton->setIconSize(QSize(22, 22));
    m_uploadButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: rgba(168, 85, 247, 0.15); border: 1px solid #a855f7; border-radius: 6px; padding: 0px; }"
        "QPushButton:hover { background-color: rgba(168, 85, 247, 0.35); border-color: #c084fc; }"
    ));
    m_uploadButton->setToolTip(tr("Upload video to cloud and copy link"));
    connect(m_uploadButton, &QPushButton::clicked, this, &RecordPreviewDialog::onUploadClicked);
    btnLayout->addWidget(m_uploadButton);

    btnLayout->addStretch();

    m_saveButton = new QPushButton(QStringLiteral("✓ ") + tr("Save"), this);
    m_saveButton->setCursor(Qt::PointingHandCursor);
    m_saveButton->setFixedSize(115, 32);
    m_saveButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #a855f7; color: #ffffff; border: none; border-radius: 6px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background-color: #9333ea; }"
    ));
    m_saveButton->setToolTip(tr("Save, copy path to clipboard and close (Enter)"));
    connect(m_saveButton, &QPushButton::clicked, this, &RecordPreviewDialog::onSaveClicked);
    btnLayout->addWidget(m_saveButton);

    mainLayout->addLayout(btnLayout);
}

RecordPreviewDialog::~RecordPreviewDialog()
{
    QString thumbPath = QDir::tempPath() + QStringLiteral("/flameshot_thumb.jpg");
    QFile::remove(thumbPath);
}

bool RecordPreviewDialog::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_thumbLabel && event->type() == QEvent::MouseButtonRelease) {
        onPlayClicked();
        return true;
    }
    if (obj == m_infoLabel && event->type() == QEvent::MouseButtonRelease && !m_uploadedUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl(m_uploadedUrl));
        return true;
    }
    return QDialog::eventFilter(obj, event);
}

void RecordPreviewDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete) {
        onTrashClicked();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        onSaveClicked();
    } else if (event->key() == Qt::Key_Escape) {
        onSaveClicked();
    } else {
        QDialog::keyPressEvent(event);
    }
}

void RecordPreviewDialog::extractMetadataAndThumbnail()
{
    QFileInfo fi(m_videoPath);
    m_fileSize = fi.size();

    // 1. Duration via ffprobe
    QProcess pDuration;
    pDuration.start(QStringLiteral("ffprobe"), QStringList()
        << QStringLiteral("-v") << QStringLiteral("error")
        << QStringLiteral("-show_entries") << QStringLiteral("format=duration")
        << QStringLiteral("-of") << QStringLiteral("default=noprint_wrappers=1:nokey=1")
        << m_videoPath);
    if (pDuration.waitForFinished(1500)) {
        QString out = QString::fromUtf8(pDuration.readAllStandardOutput()).trimmed();
        m_duration = out.toDouble();
    }
    if (m_duration <= 0.0) {
        m_duration = 1.0;
    }

    // 2. Resolution via ffprobe
    QProcess pRes;
    pRes.start(QStringLiteral("ffprobe"), QStringList()
        << QStringLiteral("-v") << QStringLiteral("error")
        << QStringLiteral("-select_streams") << QStringLiteral("v:0")
        << QStringLiteral("-show_entries") << QStringLiteral("stream=width,height")
        << QStringLiteral("-of") << QStringLiteral("csv=s=x:p=0")
        << m_videoPath);
    if (pRes.waitForFinished(1500)) {
        QString out = QString::fromUtf8(pRes.readAllStandardOutput()).trimmed();
        if (!out.isEmpty()) {
            m_resolution = out;
        }
    }

    // 3. Extract Thumbnail frame via ffmpeg
    QString thumbPath = QDir::tempPath() + QStringLiteral("/flameshot_thumb.jpg");
    QFile::remove(thumbPath);
    double thumbTime = qMin(0.5, m_duration / 2.0);
    QProcess pThumb;
    pThumb.start(QStringLiteral("ffmpeg"), QStringList()
        << QStringLiteral("-y")
        << QStringLiteral("-ss") << QString::number(thumbTime, 'f', 2)
        << QStringLiteral("-i") << m_videoPath
        << QStringLiteral("-vframes") << QStringLiteral("1")
        << QStringLiteral("-q:v") << QStringLiteral("2")
        << thumbPath);
    pThumb.waitForFinished(2000);
}

void RecordPreviewDialog::reloadThumbnail()
{
    if (!m_thumbLabel) return;
    QString thumbPath = QDir::tempPath() + QStringLiteral("/flameshot_thumb.jpg");
    QPixmap pix(thumbPath);
    if (!pix.isNull()) {
        QPixmap scaled = pix.scaled(m_thumbLabel->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        QPixmap finalPix = scaled.copy((scaled.width() - m_thumbLabel->width()) / 2,
                                       (scaled.height() - m_thumbLabel->height()) / 2,
                                       m_thumbLabel->width(), m_thumbLabel->height());
        QPainter p(&finalPix);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(QColor(15, 23, 42, 190));
        p.setPen(QColor(168, 85, 247, 220));
        QRect badgeRect(finalPix.width() / 2 - 24, finalPix.height() / 2 - 24, 48, 48);
        p.drawEllipse(badgeRect);
        p.setBrush(QColor(255, 255, 255, 230));
        p.setPen(Qt::NoPen);
        QPolygon poly;
        poly << QPoint(badgeRect.x() + 19, badgeRect.y() + 14)
             << QPoint(badgeRect.x() + 34, badgeRect.y() + 24)
             << QPoint(badgeRect.x() + 19, badgeRect.y() + 34);
        p.drawPolygon(poly);
        p.end();

        m_thumbLabel->setPixmap(finalPix);
    } else {
        m_thumbLabel->setText(tr("🎬 Click to play in video player"));
    }
}

QString RecordPreviewDialog::formatDuration(double seconds) const
{
    int totalSec = static_cast<int>(seconds);
    int m = totalSec / 60;
    int s = totalSec % 60;
    return QStringLiteral("%1:%2")
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

void RecordPreviewDialog::onSliderRangeChanged(double start, double end)
{
    if (m_startSpin && m_endSpin) {
        m_startSpin->blockSignals(true);
        m_endSpin->blockSignals(true);
        m_startSpin->setValue(start);
        m_endSpin->setValue(end);
        m_startSpin->blockSignals(false);
        m_endSpin->blockSignals(false);

        double effective = qMax(0.1, end - start);
        if (m_trimmedDurationLabel) {
            m_trimmedDurationLabel->setText(tr("Total: %1").arg(formatDuration(effective)));
        }
    }
}

void RecordPreviewDialog::onTrimChanged()
{
    if (m_startSpin && m_endSpin && m_trimmedDurationLabel) {
        if (m_startSpin->value() >= m_endSpin->value()) {
            m_startSpin->setValue(qMax(0.0, m_endSpin->value() - 0.5));
        }
        double effective = qMax(0.1, m_endSpin->value() - m_startSpin->value());
        m_trimmedDurationLabel->setText(tr("Total: %1").arg(formatDuration(effective)));
        if (m_blockSlider) {
            m_blockSlider->blockSignals(true);
            m_blockSlider->setRange(m_startSpin->value(), m_endSpin->value());
            m_blockSlider->blockSignals(false);
        }
    }
}

void RecordPreviewDialog::applyTrimIfNeeded()
{
    double start = m_startSpin ? m_startSpin->value() : 0.0;
    double end = m_endSpin ? m_endSpin->value() : m_duration;

    bool needsTrim = (start > 0.05) || (end < m_duration - 0.05);
    if (!needsTrim || end <= start) {
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (m_trimButton) {
        m_trimButton->setEnabled(false);
        m_trimButton->setText(tr("Trimming..."));
    }
    if (m_saveButton) {
        m_saveButton->setEnabled(false);
    }
    if (m_infoLabel) {
        m_infoLabel->setText(tr("Trimming video..."));
    }
    qApp->processEvents();

    double duration = end - start;
    QString trimmedPath = m_videoPath + QStringLiteral(".trim.mp4");
    QProcess p;
    // Fast stream copy (instant, 0.1-0.2s)
    p.start(QStringLiteral("ffmpeg"), QStringList()
        << QStringLiteral("-y")
        << QStringLiteral("-ss") << QString::number(start, 'f', 2)
        << QStringLiteral("-i") << m_videoPath
        << QStringLiteral("-t") << QString::number(duration, 'f', 2)
        << QStringLiteral("-c") << QStringLiteral("copy")
        << QStringLiteral("-avoid_negative_ts") << QStringLiteral("make_zero")
        << QStringLiteral("-movflags") << QStringLiteral("+faststart")
        << trimmedPath);

    bool ok = p.waitForFinished(10000) && QFile::exists(trimmedPath) && QFileInfo(trimmedPath).size() > 1024;
    if (!ok) {
        // Fallback to ultrafast re-encode if stream copy fails
        QFile::remove(trimmedPath);
        p.start(QStringLiteral("ffmpeg"), QStringList()
            << QStringLiteral("-y")
            << QStringLiteral("-ss") << QString::number(start, 'f', 2)
            << QStringLiteral("-i") << m_videoPath
            << QStringLiteral("-t") << QString::number(duration, 'f', 2)
            << QStringLiteral("-c:v") << QStringLiteral("libx264")
            << QStringLiteral("-preset") << QStringLiteral("ultrafast")
            << QStringLiteral("-crf") << QStringLiteral("22")
            << QStringLiteral("-c:a") << QStringLiteral("aac")
            << QStringLiteral("-b:a") << QStringLiteral("192k")
            << QStringLiteral("-movflags") << QStringLiteral("+faststart")
            << trimmedPath);
        ok = p.waitForFinished(30000) && QFile::exists(trimmedPath) && QFileInfo(trimmedPath).size() > 1024;
    }

    if (ok) {
        QFile::remove(m_videoPath);
        QFile::rename(trimmedPath, m_videoPath);

        m_duration = duration;
        QFileInfo fi(m_videoPath);
        m_fileSize = fi.size();

        if (m_startSpin) {
            m_startSpin->blockSignals(true);
            m_startSpin->setRange(0.0, qMax(0.0, m_duration - 0.5));
            m_startSpin->setValue(0.0);
            m_startSpin->blockSignals(false);
        }
        if (m_endSpin) {
            m_endSpin->blockSignals(true);
            m_endSpin->setRange(0.5, m_duration);
            m_endSpin->setValue(m_duration);
            m_endSpin->blockSignals(false);
        }
        if (m_blockSlider) {
            m_blockSlider->setDuration(m_duration);
        }
        if (m_trimmedDurationLabel) {
            m_trimmedDurationLabel->setText(tr("Total: %1").arg(formatDuration(m_duration)));
        }

        extractMetadataAndThumbnail();
        reloadThumbnail();

        QString sizeStr = QString::number(m_fileSize / (1024.0 * 1024.0), 'f', 1) + tr(" MB");
        if (m_infoLabel) {
            m_infoLabel->setText(QStringLiteral("%1  •  %2  •  %3")
                                     .arg(formatDuration(m_duration))
                                     .arg(sizeStr)
                                     .arg(m_resolution));
            m_infoLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: bold; color: #94a3b8; font-family: monospace;"));
        }

        if (m_trimButton) {
            m_trimButton->setText(tr("✓ Done"));
            m_trimButton->setEnabled(true);
        }

        SystemNotification().sendMessage(
            tr("Video trimmed"),
            tr("Duration: %1").arg(formatDuration(m_duration))
        );
    } else {
        QFile::remove(trimmedPath);
        if (m_trimButton) {
            m_trimButton->setText(tr("✂ Trim"));
            m_trimButton->setEnabled(true);
        }
    }

    if (m_saveButton) {
        m_saveButton->setEnabled(true);
        m_saveButton->setText(QStringLiteral("✓ ") + tr("Save"));
    }
    QApplication::restoreOverrideCursor();
}

void RecordPreviewDialog::onTrimButtonClicked()
{
    applyTrimIfNeeded();
}

void RecordPreviewDialog::onPlayClicked()
{
    applyTrimIfNeeded();
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_videoPath));
}

void RecordPreviewDialog::onTrashClicked()
{
    QFile::remove(m_videoPath);
    SystemNotification().sendMessage(
        tr("Recording deleted"),
        tr("File moved to trash.")
    );
    reject();
}

void RecordPreviewDialog::onSaveClicked()
{
    applyTrimIfNeeded();

    FlameshotDaemon::copyToClipboard(m_videoPath);
    SystemNotification().sendMessage(
        tr("Video saved"),
        tr("Path copied to clipboard: %1").arg(m_videoPath)
    );
    accept();
}

void RecordPreviewDialog::onUploadClicked()
{
    double start = m_startSpin ? m_startSpin->value() : 0.0;
    double end = m_endSpin ? m_endSpin->value() : m_duration;
    bool needsTrim = (start > 0.05) || (end < m_duration - 0.05);

    if (needsTrim && end > start) {
        if (m_infoLabel) {
            m_infoLabel->setText(tr("Preparing video (trimming)..."));
            m_infoLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: bold; color: #38bdf8; font-family: monospace;"));
        }
        applyTrimIfNeeded();
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_uploadButton->setEnabled(false);
    m_uploadButton->setIcon(QIcon());
    m_uploadButton->setText(QStringLiteral("..."));
    m_infoLabel->setText(tr("Connecting to cloud server..."));
    m_infoLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: bold; color: #38bdf8; font-family: monospace;"));

    QFileInfo fi(m_videoPath);
    bool isLarge = (fi.size() > 200 * 1024 * 1024);

    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart reqTypePart;
    reqTypePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                          QVariant(QStringLiteral("form-data; name=\"reqtype\"")));
    reqTypePart.setBody("fileupload");
    multiPart->append(reqTypePart);

    if (isLarge) {
        // Litterbox supports large files up to 1 GB (1024 MB)
        QHttpPart timePart;
        timePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                          QVariant(QStringLiteral("form-data; name=\"time\"")));
        timePart.setBody("72h");
        multiPart->append(timePart);
    }

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("video/mp4")));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"fileToUpload\"; filename=\"video.mp4\"")));

    auto* file = new QFile(m_videoPath, multiPart);
    if (!file->open(QIODevice::ReadOnly)) {
        QApplication::restoreOverrideCursor();
        m_uploadButton->setEnabled(true);
        m_uploadButton->setText(tr("File error"));
        m_infoLabel->setText(tr("Failed to open video file."));
        delete multiPart;
        return;
    }
    filePart.setBodyDevice(file);
    multiPart->append(filePart);

    QUrl uploadUrl = isLarge
        ? QUrl(QStringLiteral("https://litterbox.catbox.moe/resources/internals/api.php"))
        : QUrl(QStringLiteral("https://catbox.moe/user/api.php"));

    QNetworkRequest request(uploadUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Flameshot-Plus/12.1.0"));

    QNetworkReply* reply = m_nam->post(request, multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::uploadProgress, this, &RecordPreviewDialog::onUploadProgress);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onUploadFinished(reply);
    });
}

void RecordPreviewDialog::onUploadProgress(qint64 bytesSent, qint64 bytesTotal)
{
    if (bytesTotal > 0 && m_uploadButton && m_infoLabel) {
        double mbSent = bytesSent / (1024.0 * 1024.0);
        double mbTotal = bytesTotal / (1024.0 * 1024.0);
        int pct = static_cast<int>((bytesSent * 100) / bytesTotal);

        if (pct < 100) {
            m_uploadButton->setText(QStringLiteral("%1%").arg(pct));
            m_infoLabel->setText(tr("Uploading: %1 / %2 MB (%3%)")
                                     .arg(QString::number(mbSent, 'f', 1))
                                     .arg(QString::number(mbTotal, 'f', 1))
                                     .arg(pct));
            m_infoLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: bold; color: #38bdf8; font-family: monospace;"));
        } else {
            m_uploadButton->setText(tr("Saving..."));
            m_infoLabel->setText(tr("File uploaded. Server generating link..."));
            m_infoLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: bold; color: #f59e0b; font-family: monospace;"));
        }
    }
}

void RecordPreviewDialog::onUploadFinished(QNetworkReply* reply)
{
    QApplication::restoreOverrideCursor();
    reply->deleteLater();
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString response = QString::fromUtf8(reply->readAll()).trimmed();

    if (reply->error() == QNetworkReply::NoError && statusCode == 200 &&
        (response.startsWith(QLatin1String("http://")) || response.startsWith(QLatin1String("https://")))) {

        m_uploadedUrl = response;
        FlameshotDaemon::copyToClipboard(response);

        // Save to Latest Uploads history
        History history;
        QString fileName = response;
        int lastSlash = fileName.lastIndexOf("/");
        if (lastSlash >= 0) {
            fileName = fileName.mid(lastSlash + 1);
        }
        QString storageType = QStringLiteral("catbox");
        if (response.contains(QLatin1String("litter"))) {
            storageType = QStringLiteral("litterbox");
        } else if (response.contains(QLatin1String("0x0.st"))) {
            storageType = QStringLiteral("0x0.st");
        }
        QString packedName = history.packFileName(storageType, "", fileName);
        QPixmap thumb = m_thumbLabel ? m_thumbLabel->pixmap(Qt::ReturnByValue) : QPixmap();
        if (thumb.isNull()) {
            QString thumbPath = QDir::tempPath() + QStringLiteral("/flameshot_thumb.jpg");
            thumb.load(thumbPath);
        }
        if (!thumb.isNull()) {
            history.save(thumb, packedName);
        }

        SystemNotification().sendMessage(
            tr("Video uploaded to cloud!"),
            response
        );

        m_uploadButton->setEnabled(true);
        m_uploadButton->setText(QString());
        m_uploadButton->setIcon(QIcon(QStringLiteral(":/img/material/white/accept.svg")));
        m_uploadButton->setIconSize(QSize(20, 20));
        m_uploadButton->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: rgba(34, 197, 94, 0.2); border: 1px solid #22c55e; border-radius: 6px; padding: 0px; }"
            "QPushButton:hover { background-color: rgba(34, 197, 94, 0.35); border-color: #4ade80; }"
        ));

        m_infoLabel->setText(response);
        m_infoLabel->setCursor(Qt::PointingHandCursor);
        m_infoLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: bold; color: #22c55e; font-family: monospace; text-decoration: underline;"));
        m_infoLabel->setToolTip(tr("Click to open video in browser"));
        m_infoLabel->installEventFilter(this);
    } else {
        m_uploadButton->setEnabled(true);
        m_uploadButton->setText(QString());
        m_uploadButton->setIcon(QIcon(QStringLiteral(":/img/material/white/cloud-upload.svg")));
        m_uploadButton->setIconSize(QSize(22, 22));
        m_infoLabel->setText(tr("Cloud upload error"));
        m_infoLabel->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: bold; color: #ef4444; font-family: monospace;"));
    }
}
