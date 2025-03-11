// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SNAPSHOTQML_H
#define BITCOIN_QML_MODELS_SNAPSHOTQML_H

#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <kernel/coinstats.h>
#include <logging/timer.h>
#include <node/context.h>
#include <node/utxo_snapshot.h>
#include <clientversion.h>
#include <validation.h>

#include <QObject>


/** Worker class for UTXO snapshot functions. */
class SnapshotQml : public QObject
{
   Q_PROPERTY(bool isPruned READ isPruned WRITE setIsPruned NOTIFY isPrunedChanged)
   Q_PROPERTY(bool isSnapshotFileExists READ isSnapshotFileExists NOTIFY isSnapshotFileExistsChanged)
   Q_PROPERTY(bool isRewinding READ isRewinding WRITE setIsRewinding NOTIFY isRewindingChanged)
   Q_OBJECT
public:
    SnapshotQml(interfaces::Node& node, QString path);

    bool processPath();
    void SnapshotGen();
    bool isPruned() const { return m_is_pruned; }
    void setIsPruned(bool is_pruned);
    void setSnapshotCancel(std::atomic<bool>* cancel) { m_snapshot_cancel = cancel; }
    bool isSnapshotFileExists();
    QString getSnapshotDirectory();
    bool isRewinding() const { return m_is_rewinding; }
    void setIsRewinding(bool is_rewinding);

Q_SIGNALS:
    void isPrunedChanged();
    void isSnapshotFileExistsChanged();
    void isRewindingChanged();
private:
    interfaces::Node& m_node;
    QString m_path;
    bool m_is_pruned;
    std::atomic<bool>* m_snapshot_cancel;
    bool m_is_snapshot_file_exists;
    bool m_is_rewinding;
};

#endif // BITCOIN_QML_MODELS_SNAPSHOTQML_H
