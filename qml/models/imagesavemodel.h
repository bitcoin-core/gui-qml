// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_IMAGESAVEMODEL_H
#define BITCOIN_QML_MODELS_IMAGESAVEMODEL_H

#include <qml/backendexecutor.h>

#include <QImage>
#include <QObject>
#include <QUrl>

class ImageSaveModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
public:
    explicit ImageSaveModel(QObject* parent = nullptr) : QObject(parent) {}
    bool pending() const { return m_pending; }
    Q_INVOKABLE bool save(const QImage& image, const QUrl& destination);

Q_SIGNALS:
    void pendingChanged();
    void finished(bool success);

private:
    BackendExecutor m_executor;
    bool m_pending{false};
};

#endif // BITCOIN_QML_MODELS_IMAGESAVEMODEL_H
