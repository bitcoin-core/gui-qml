// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BUILDINFO_H
#define BITCOIN_QML_BUILDINFO_H

#include <clientversion.h>

#include <QObject>
#include <QString>

class BuildInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isDebug READ isDebug CONSTANT)
    Q_PROPERTY(QString fullClientVersion READ fullClientVersion CONSTANT)

public:
    explicit BuildInfo(QObject* parent = nullptr) : QObject(parent) {}

    bool isDebug() const
    {
#ifndef NDEBUG
        return true;
#else
        return false;
#endif
    }

    QString fullClientVersion() const { return QString::fromStdString(FormatFullVersion()); }
};

#endif // BITCOIN_QML_BUILDINFO_H
