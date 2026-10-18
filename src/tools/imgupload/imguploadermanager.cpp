// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Yurii Puchkov & Contributors
//

#include "imguploadermanager.h"
#include <QPixmap>
#include <QWidget>

// TODO - remove this hard-code and create plugin manager in the future, you may
// include other storage headers here
#include "storages/nullpointer/nullpointeruploader.h"

ImgUploaderManager::ImgUploaderManager(QObject* parent)
  : QObject(parent)
  , m_imgUploaderBase(nullptr)
{
    m_imgUploaderPlugin = QStringLiteral("0x0.st");
    init();
}

void ImgUploaderManager::init()
{
    m_urlString = QStringLiteral("https://0x0.st/");
    m_imgUploaderPlugin = QStringLiteral("0x0.st");
}

ImgUploaderBase* ImgUploaderManager::uploader(const QPixmap& capture,
                                              QWidget* parent)
{
    m_imgUploaderBase = (ImgUploaderBase*)(new NullPointerUploader(capture, parent));
    if (m_imgUploaderBase && !capture.isNull()) {
        m_imgUploaderBase->upload();
    }
    return m_imgUploaderBase;
}

ImgUploaderBase* ImgUploaderManager::uploader(const QString& imgUploaderPlugin)
{
    m_imgUploaderPlugin = imgUploaderPlugin;
    init();
    return uploader(QPixmap());
}

const QString& ImgUploaderManager::uploaderPlugin()
{
    return m_imgUploaderPlugin;
}

const QString& ImgUploaderManager::url()
{
    return m_urlString;
}
