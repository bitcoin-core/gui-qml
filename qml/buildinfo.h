// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BUILDINFO_H
#define BITCOIN_QML_BUILDINFO_H

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <clientversion.h>

#include <QObject>
#include <QString>

class BuildInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isDebug READ isDebug CONSTANT)
    Q_PROPERTY(QString fullClientVersion READ fullClientVersion CONSTANT)
    Q_PROPERTY(bool externalSignerSupported READ externalSignerSupported CONSTANT)

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

    // Without signer support Core cannot run a signer command or load an
    // external signer wallet, so configuring HWI can't make it work.
    bool externalSignerSupported() const
    {
#ifdef ENABLE_EXTERNAL_SIGNER
        return true;
#else
        return false;
#endif
    }
};

#endif // BITCOIN_QML_BUILDINFO_H
