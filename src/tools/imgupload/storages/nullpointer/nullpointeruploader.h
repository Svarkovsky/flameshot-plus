// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Flameshot + Contributors

#pragma once

#include "src/tools/imgupload/storages/imguploaderbase.h"
#include <QByteArray>

class QNetworkAccessManager;
class QNetworkReply;

class NullPointerUploader : public ImgUploaderBase
{
    Q_OBJECT
public:
    explicit NullPointerUploader(const QPixmap& capture, QWidget* parent = nullptr);

    void upload() override;
    void deleteImage(const QString& fileName, const QString& deleteToken) override;

private slots:
    void handleReply(QNetworkReply* reply);

private:
    void uploadToCatbox();

    QNetworkAccessManager* m_networkAM;
    QByteArray m_imageData;
    bool m_triedFallback;
};
