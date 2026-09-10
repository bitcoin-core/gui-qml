// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_CHAINSYNCMODEL_H
#define BITCOIN_QML_MODELS_CHAINSYNCMODEL_H
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <QElapsedTimer>
#include <QObject>
#include <QVector>

class ChainSyncModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int blockTipHeight READ blockTipHeight NOTIFY blockTipHeightChanged)
    Q_PROPERTY(int remainingSyncTime READ remainingSyncTime NOTIFY remainingSyncTimeChanged)
    Q_PROPERTY(double verificationProgress READ verificationProgress NOTIFY verificationProgressChanged)
    Q_PROPERTY(bool blockSyncActive READ blockSyncActive NOTIFY blockSyncActiveChanged)
    Q_PROPERTY(bool headerSyncActive READ headerSyncActive NOTIFY headerSyncChanged)
    Q_PROPERTY(bool headerPresync READ headerPresync NOTIFY headerSyncChanged)
    Q_PROPERTY(double headerSyncProgress READ headerSyncProgress NOTIFY headerSyncChanged)
public:
    explicit ChainSyncModel(interfaces::Node& node);
    ~ChainSyncModel() override;
    int blockTipHeight() const { return m_block_tip_height; }
    void setBlockTipHeight(int new_height);
    int remainingSyncTime() const { return m_remaining_sync_time; }
    void setRemainingSyncTime(double new_progress);
    double verificationProgress() const { return m_verification_progress; }
    void setVerificationProgress(double new_progress);
    bool blockSyncActive() const { return m_block_sync_active; }
    bool headerSyncActive() const { return m_header_sync_active; }
    bool headerPresync() const { return m_header_presync; }
    double headerSyncProgress() const { return m_header_sync_progress; }
    int headerTipHeight() const { return m_header_tip_height; }
    int64_t headerTipTime() const { return m_header_tip_time; }
public Q_SLOTS:
    void initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info);
    void stop();
Q_SIGNALS:
    void blockTipHeightChanged();
    void blockTipChanged();
    void remainingSyncTimeChanged();
    void verificationProgressChanged();
    void blockSyncActiveChanged();
    void headerSyncChanged();
    void setTimeRatioList(int new_time);
    void setTimeRatioListInitial();
private:
    interfaces::Node& m_node;
    int m_block_tip_height{0};
    int m_remaining_sync_time{0};
    double m_verification_progress{0.0};
    bool m_block_sync_active{false};
    bool m_header_sync_active{false};
    bool m_header_presync{false};
    double m_header_sync_progress{0.0};
    int m_header_tip_height{0};
    int64_t m_header_tip_time{0};
    bool m_stopped{false};
    QElapsedTimer m_sample_clock;
    QVector<QPair<qint64, double>> m_block_process_time;
    std::unique_ptr<interfaces::Handler> m_handler_notify_block_tip;
    std::unique_ptr<interfaces::Handler> m_handler_notify_header_tip;
    void ConnectToBlockTipSignal();
    void ConnectToHeaderTipSignal();
    void setBlockSyncActive(bool active);
    void setHeaderSyncState(int height, int64_t block_time, bool presync);
};
#endif // BITCOIN_QML_MODELS_CHAINSYNCMODEL_H
