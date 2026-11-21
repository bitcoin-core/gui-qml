// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BLOCKCLOCKPROJECTION_H
#define BITCOIN_QML_BLOCKCLOCKPROJECTION_H

#include <qml/snapshotquery.h>
#include <memory>
#include <QList>
#include <QMetaType>

namespace blockclock {
struct Projection {
    qint64 period{0};
    quint64 revision{0}, interest{0}, serial{0}, previous_serial{0}, history_generation{0};
    Projection() = default;
    Projection(const Projection&) = delete;
    Projection& operator=(const Projection&) = delete;
    QList<qreal> fractions;
    bool changed{false}, failed{false};
};
using Snapshot = std::shared_ptr<const Projection>;
using WeakSnapshot = std::weak_ptr<const Projection>;
} // namespace blockclock
Q_DECLARE_METATYPE(blockclock::WeakSnapshot)
#endif // BITCOIN_QML_BLOCKCLOCKPROJECTION_H
