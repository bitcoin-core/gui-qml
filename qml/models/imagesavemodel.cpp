// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/imagesavemodel.h>

#include <QSaveFile>

bool ImageSaveModel::save(const QImage& image, const QUrl& destination)
{
    if (m_pending || image.isNull() || !destination.isLocalFile()) return false;
    m_pending = true;
    Q_EMIT pendingChanged();
    return m_executor.submit(this, [image, path = destination.toLocalFile()] {
        QSaveFile file{path};
        return file.open(QIODevice::WriteOnly) && image.save(&file, "PNG") && file.commit();
    }, [this](bool success) {
        m_pending = false;
        Q_EMIT pendingChanged();
        Q_EMIT finished(success);
    }, [this](std::exception_ptr) {
        m_pending = false;
        Q_EMIT pendingChanged();
        Q_EMIT finished(false);
    });
}
