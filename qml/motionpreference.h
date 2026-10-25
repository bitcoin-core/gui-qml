// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MOTIONPREFERENCE_H
#define BITCOIN_QML_MOTIONPREFERENCE_H

#include <QObject>
#include <QString>

//! Whether the platform accessibility settings ask apps to reduce motion.
bool SystemPrefersReducedMotion();

//! The motion mode Theme.motion starts in: "full", "reduced", or "off" (tests only).
class MotionPreference : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode CONSTANT)
public:
    explicit MotionPreference(const QString& mode, QObject* parent = nullptr)
        : QObject(parent), m_mode(mode) {}

    QString mode() const { return m_mode; }

private:
    const QString m_mode;
};

#endif // BITCOIN_QML_MOTIONPREFERENCE_H
