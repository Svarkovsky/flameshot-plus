// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Flameshot + Contributors

#include "nullpointeruploader.h"
#include "src/core/flameshotdaemon.h"
#include "src/utils/filenamehandler.h"
#include "src/utils/globalvalues.h"
#include "src/utils/history.h"
#include "src/utils/systemnotification.h"
#include "src/widgets/loadspinner.h"
#include "src/widgets/notificationwidget.h"
#include <QBuffer>
#include <QDesktopServices>
#include <QHttpMultiPart>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QShortcut>

NullPointerUploader::NullPointerUploader(const QPixmap& capture, QWidget* parent)
  : ImgUploaderBase(capture, parent)
  , m_networkAM(new QNetworkAccessManager(this))
  , m_triedFallback(false)
{
    connect(m_networkAM,
            &QNetworkAccessManager::finished,
            this,
            &NullPointerUploader::handleReply);
}

void NullPointerUploader::upload()
{
    m_triedFallback = false;
    m_imageData.clear();

    QBuffer buffer(&m_imageData);
    pixmap().save(&buffer, "PNG");

    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("image/png")));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"screenshot.png\"")));
    filePart.setBody(m_imageData);
    multiPart->append(filePart);

    QNetworkRequest request(QUrl(QStringLiteral("https://0x0.st")));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Flameshot-Plus/12.1.0"));
    QNetworkReply* reply = m_networkAM->post(request, multiPart);
    multiPart->setParent(reply);
}

void NullPointerUploader::uploadToCatbox()
{
    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart reqTypePart;
    reqTypePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                          QVariant(QStringLiteral("form-data; name=\"reqtype\"")));
    reqTypePart.setBody("fileupload");
    multiPart->append(reqTypePart);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("image/png")));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"fileToUpload\"; filename=\"screenshot.png\"")));
    filePart.setBody(m_imageData);
    multiPart->append(filePart);

    QNetworkRequest request(QUrl(QStringLiteral("https://catbox.moe/user/api.php")));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Flameshot-Plus/12.1.0"));
    QNetworkReply* reply = m_networkAM->post(request, multiPart);
    multiPart->setParent(reply);
}

void NullPointerUploader::handleReply(QNetworkReply* reply)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString response = QString::fromUtf8(reply->readAll()).trimmed();

    if (reply->error() == QNetworkReply::NoError && statusCode == 200 &&
        (response.startsWith(QLatin1String("http://")) || response.startsWith(QLatin1String("https://")))) {

        spinner()->deleteLater();
        setImageURL(QUrl(response));

        m_currentImageName = imageURL().toString();
        int lastSlash = m_currentImageName.lastIndexOf("/");
        if (lastSlash >= 0) {
            m_currentImageName = m_currentImageName.mid(lastSlash + 1);
        }

        History history;
        QString storageType = QStringLiteral("0x0.st");
        if (response.contains(QLatin1String("catbox.moe"))) {
            storageType = QStringLiteral("catbox");
        } else if (response.contains(QLatin1String("litter"))) {
            storageType = QStringLiteral("litterbox");
        }
        m_currentImageName = history.packFileName(storageType, "", m_currentImageName);
        history.save(pixmap(), m_currentImageName);

        FlameshotDaemon::copyToClipboard(response);
        SystemNotification().sendMessage(
            tr("Image uploaded successfully!"),
            response,
            GlobalValues::iconPath(),
            3000);

        showPostUploadDialog();
        emit uploadOk(imageURL());
    } else if (!m_triedFallback) {
        m_triedFallback = true;
        setInfoLabelText(tr("0x0.st is busy, routing to fast fallback..."));
        uploadToCatbox();
    } else {
        spinner()->deleteLater();
        setInfoLabelText(tr("Upload failed: %1\n%2").arg(reply->errorString(), response));
        new QShortcut(Qt::Key_Escape, this, SLOT(close()));
    }
}

void NullPointerUploader::deleteImage(const QString& fileName, const QString& deleteToken)
{
    Q_UNUSED(fileName);
    Q_UNUSED(deleteToken);
    if (notification()) {
        notification()->showMessage(tr("Anonymous uploads expire automatically."));
    }
    emit deleteOk();
}
