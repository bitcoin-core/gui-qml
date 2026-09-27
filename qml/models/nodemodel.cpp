// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/nodemodel.h>

#include <common/args.h>
#include <common/system.h>
#include <chainparams.h>
#include <interfaces/node.h>
#include <net.h>
#include <net_processing.h>
#include <netbase.h>
#include <node/interface_ui.h>
#include <util/fs.h>
#include <util/string.h>
#include <util/threadnames.h>
#include <util/time.h>
#include <validation.h>

#include <cassert>
#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

#include <QDateTime>
#include <QEventLoop>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <QTimerEvent>
#include <QVariantMap>

using namespace std::chrono_literals;

static constexpr int MEMPOOL_INFO_POLLING_INTERVAL_MS{3000};
static constexpr int HEADER_HEIGHT_DELTA_SYNC{24};

namespace {
QStringList SplitWarnings(const QString& warnings)
{
    QString normalized{warnings};
    normalized.replace(QStringLiteral("<hr/>"), QStringLiteral("<hr />"), Qt::CaseInsensitive);
    normalized.replace(QStringLiteral("<hr>"), QStringLiteral("<hr />"), Qt::CaseInsensitive);

    QStringList result;
    for (const QString& warning : normalized.split(QStringLiteral("<hr />"), Qt::SkipEmptyParts)) {
        const QString trimmed{warning.trimmed()};
        if (!trimmed.isEmpty()) {
            result.push_back(trimmed);
        }
    }
    return result;
}

QString RuntimeDialogTitle(unsigned int style)
{
    if (style & CClientUIInterface::ICON_ERROR) {
        return QObject::tr("Error");
    }
    if (style & CClientUIInterface::ICON_WARNING) {
        return QObject::tr("Warning");
    }
    return QObject::tr("Information");
}

QString RuntimeDialogIcon(unsigned int style)
{
    if (style & CClientUIInterface::ICON_ERROR) {
        return QStringLiteral("image://images/error");
    }
    if (style & CClientUIInterface::ICON_WARNING) {
        return QStringLiteral("image://images/alert-filled");
    }
    return QStringLiteral("image://images/info-filled");
}

unsigned int RuntimeDialogButtons(unsigned int style)
{
    const unsigned int buttons{style & CClientUIInterface::BTN_MASK};
    return buttons ? buttons : CClientUIInterface::BTN_OK;
}

bool HasMultipleRuntimeDialogButtons(unsigned int buttons)
{
    return (buttons & (buttons - 1)) != 0;
}

QVariant InformationRow(const QString& label, const QString& value)
{
    QVariantMap row;
    row.insert(QStringLiteral("label"), label);
    row.insert(QStringLiteral("value"), value);
    return QVariant::fromValue(row);
}
} // namespace

NodeModel::NodeModel(interfaces::Node& node)
    : m_node{node}
{
    m_mempool_information_available = !gArgs.GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY);
    const auto drained = [this] {
        if (--m_workers_to_drain == 0) Q_EMIT backendDrained();
    };
    connect(&m_backend, &BackendWorker::drained, this, drained);
    connect(&m_shutdown_worker, &BackendWorker::drained, this, drained);
    initializeMempoolInfoPolling();
    refreshPeerCounts();
    ConnectToBlockTipSignal();
    ConnectToHeaderTipSignal();
    ConnectToNumConnectionsChangedSignal();
    ConnectToNetworkActiveChangedSignal();
    ConnectToAlertChangedSignal();
    ConnectToRuntimeDialogSignals();
    ConnectToBannedListChangedSignal();
    refreshMempoolInfo();
}

NodeModel::~NodeModel()
{
    unsubscribeFromCoreSignals();
    setMempoolInfoPollingActive(false);
}

void NodeModel::setBlockTipHeight(int new_height)
{
    RequireModelThread(this);
    if (new_height != m_block_tip_height) {
        m_block_tip_height = new_height;
        Q_EMIT blockTipHeightChanged();
    }
}

void NodeModel::setNumOutboundPeers(int new_num)
{
    RequireModelThread(this);
    if (new_num != m_num_outbound_peers) {
        m_num_outbound_peers = new_num;
        Q_EMIT numOutboundPeersChanged();
    }
}

void NodeModel::setNumPeers(int new_num)
{
    RequireModelThread(this);
    if (new_num != m_num_peers) {
        m_num_peers = new_num;
        Q_EMIT numPeersChanged();
    }
}

void NodeModel::setNumInboundPeers(int new_num)
{
    RequireModelThread(this);
    if (new_num != m_num_inbound_peers) {
        m_num_inbound_peers = new_num;
        Q_EMIT numInboundPeersChanged();
    }
}

void NodeModel::refreshPeerCounts()
{
    refreshSnapshot();
}

void NodeModel::refreshSnapshot()
{
    RequireModelThread(this);
    if (m_draining) return;
    if (m_snapshot_pending) {
        m_snapshot_again = true;
        return;
    }
    m_snapshot_pending = true;
    struct Snapshot {
        int peers, inbound, outbound;
        QString warnings, addresses;
        int64_t received, sent;
    };
    m_backend.submit([node = &m_node, ready = m_node_ready] {
        Snapshot result{};
        result.warnings = QString::fromStdString(node->getWarnings().translated);
        if (ready) {
            result.peers = static_cast<int>(node->getNodeCount(ConnectionDirection::Both));
            result.inbound = static_cast<int>(node->getNodeCount(ConnectionDirection::In));
            result.outbound = static_cast<int>(node->getNodeCount(ConnectionDirection::Out));
            result.received = node->getTotalBytesRecv();
            result.sent = node->getTotalBytesSent();
            for (const auto& [addr, info] : node->getNetLocalAddresses()) {
                result.addresses += QString::fromStdString(addr.ToStringAddr());
                if (!addr.IsI2P()) result.addresses += QStringLiteral(":") + QString::number(info.nPort);
                result.addresses += QStringLiteral(", ");
            }
            if (!result.addresses.isEmpty()) result.addresses.chop(2);
        }
        return result;
    }, [this](Snapshot result) {
        RequireModelThread(this);
        m_snapshot_pending = false;
        setNumPeers(result.peers);
        setNumInboundPeers(result.inbound);
        setNumOutboundPeers(result.outbound);
        setWarnings(result.warnings);
        m_total_bytes_received = result.received;
        m_total_bytes_sent = result.sent;
        m_local_addresses = std::move(result.addresses);
        Q_EMIT nodeInformationChanged();
        if (std::exchange(m_snapshot_again, false)) refreshSnapshot();
    });
}

void NodeModel::refreshMempoolInfo()
{
    requestMempoolInfoRefresh();
}

void NodeModel::setMempoolInfoPollingActive(bool active)
{
    RequireModelThread(this);
    if (m_draining) active = false;
    if (m_mempool_info_polling_active == active) return;
    m_mempool_info_polling_active = active;
    Q_EMIT mempoolInfoPollingActiveChanged(active);
    if (active) {
        m_mempool_info_timer->start();
        refreshMempoolInfo();
    } else {
        m_mempool_info_timer->stop();
    }
}

void NodeModel::initializeMempoolInfoPolling()
{
    m_mempool_info_timer = new QTimer(this);
    m_mempool_info_timer->setInterval(MEMPOOL_INFO_POLLING_INTERVAL_MS);
    connect(m_mempool_info_timer, &QTimer::timeout, this, &NodeModel::requestMempoolInfoRefresh);
}

void NodeModel::requestMempoolInfoRefresh()
{
    RequireModelThread(this);
    if (m_draining || !m_node_ready) return;
    if (m_mempool_pending) {
        m_mempool_again = true;
        return;
    }
    m_mempool_pending = true;
    m_backend.submit([node = &m_node] {
        return MempoolInfo{
            static_cast<int>(node->getMempoolSize()),
            node->getMempoolDynamicUsage() / 1'000'000.0,
            node->getMempoolMaxUsage() / 1'000'000.0,
        };
    }, [this](const MempoolInfo& info) {
        m_mempool_pending = false;
        applyMempoolInfo(info);
        if (std::exchange(m_mempool_again, false)) requestMempoolInfoRefresh();
    });
}

void NodeModel::drainBackend()
{
    RequireModelThread(this);
    if (m_draining) return;
    m_draining = true;
    setMempoolInfoPollingActive(false);
    unsubscribeFromCoreSignals();
    m_backend.drain();
    m_shutdown_worker.drain();
}

void NodeModel::applyMempoolInfo(const MempoolInfo& info)
{
    RequireModelThread(this);
    if (info.transaction_count != m_mempool_transaction_count ||
        info.usage_mb != m_mempool_usage_mb ||
        info.max_usage_mb != m_mempool_max_usage_mb) {
        m_mempool_transaction_count = info.transaction_count;
        m_mempool_usage_mb = info.usage_mb;
        m_mempool_max_usage_mb = info.max_usage_mb;
        Q_EMIT mempoolInfoChanged();
    }
}

void NodeModel::setRemainingSyncTime(double new_progress)
{
    RequireModelThread(this);
    const qint64 currentTime = QDateTime::currentDateTime().toMSecsSinceEpoch();

    // keep a vector of samples of verification progress at height
    m_block_process_time.push_front(qMakePair(currentTime, new_progress));

    // show progress speed if we have more than one sample
    if (m_block_process_time.size() >= 2) {
        double progressDelta = 0;
        qint64 timeDelta = 0;
        int remainingMSecs = 0;
        double remainingProgress = 1.0 - new_progress;
        for (int i = 1; i < m_block_process_time.size(); i++) {
            const auto& sample = m_block_process_time[i];

            // take first sample after 500 seconds or last available one
            if (sample.first < (currentTime - 500 * 1000) || i == m_block_process_time.size() - 1) {
                progressDelta = m_block_process_time[0].second - sample.second;
                timeDelta = m_block_process_time[0].first - sample.first;
                const double estimate = (progressDelta > 0) ? remainingProgress / progressDelta * timeDelta : -1;
                remainingMSecs = static_cast<int>(std::clamp(estimate, -1.0, static_cast<double>(std::numeric_limits<int>::max())));
                break;
            }
        }
        if (remainingMSecs > 0 && m_block_process_time.count() % 1000 == 0) {
            m_remaining_sync_time = remainingMSecs;

            Q_EMIT remainingSyncTimeChanged();
        }
        static const int MAX_SAMPLES = 5000;
        if (m_block_process_time.count() > MAX_SAMPLES) {
            m_block_process_time.remove(1, m_block_process_time.count() - 1);
        }
    }
}
void NodeModel::setVerificationProgress(double new_progress)
{
    RequireModelThread(this);
    if (new_progress != m_verification_progress) {
        setRemainingSyncTime(new_progress);

        m_verification_progress = new_progress;
        Q_EMIT verificationProgressChanged();
    }
}

void NodeModel::setBlockSyncActive(bool active)
{
    RequireModelThread(this);
    if (m_block_sync_active == active) {
        return;
    }

    m_block_sync_active = active;
    Q_EMIT blockSyncActiveChanged();
}

void NodeModel::setHeaderSyncState(int height, int64_t block_time, bool presync)
{
    RequireModelThread(this);
    m_header_tip_height = height;
    m_header_tip_time = block_time;

    const int64_t estimate_headers_left{(GetTime() - block_time) / Params().GetConsensus().nPowTargetSpacing};
    const bool active{height > 0 && estimate_headers_left > HEADER_HEIGHT_DELTA_SYNC};
    const double progress{active ? 1.0 * height / (height + estimate_headers_left) : 0.0};

    if (m_header_sync_active == active &&
        m_header_presync == presync &&
        m_header_sync_progress == progress) {
        return;
    }

    m_header_sync_active = active;
    m_header_presync = presync;
    m_header_sync_progress = progress;
    Q_EMIT headerSyncChanged();
}

void NodeModel::setPause(bool new_pause)
{
    RequireModelThread(this);
    if (m_draining || !m_node_ready) return;
    if(m_pause != new_pause) {
        m_pause = new_pause;
        m_backend.post([node = &m_node, new_pause] { node->setNetworkActive(!new_pause); });
        Q_EMIT pauseChanged(new_pause);
    }
}

void NodeModel::setErrorState(bool faulted)
{
    RequireModelThread(this);
    if (m_faulted != faulted) {
        m_faulted = faulted;
        Q_EMIT errorStateChanged(faulted);
    }
}

void NodeModel::setStartupError(const QString& error)
{
    RequireModelThread(this);
    if (m_startup_error != error) {
        m_startup_error = error;
        Q_EMIT startupErrorChanged();
    }
}

void NodeModel::addStartupWarnings(const QStringList& warnings)
{
    for (const QString& warning : warnings) {
        recordStartupWarningMessage(warning);
    }
}

void NodeModel::setWarnings(const QString& warnings)
{
    RequireModelThread(this);
    const QStringList warning_list{SplitWarnings(warnings)};
    if (m_warnings == warnings && m_warning_list == warning_list) {
        return;
    }
    m_warnings = warnings;
    m_warning_list = warning_list;
    Q_EMIT warningsChanged();
}

void NodeModel::refreshWarnings()
{
    // Keep "current warnings" tied to Core's active warning set.
    refreshSnapshot();
}

void NodeModel::showStartupWarnings()
{
    if (m_startup_warning_messages.isEmpty()) {
        return;
    }

    const QString warnings{m_startup_warning_messages.join(QStringLiteral("\n\n"))};
    m_startup_warning_messages.clear();
    // MSG_WARNING is modal; startup notices should be shown once without blocking initialization.
    showRuntimeDialogOnGuiThread(warnings, CClientUIInterface::ICON_WARNING, /*question=*/false);
}

void NodeModel::recordStartupErrorMessage(const QString& message)
{
    const QString error{message.trimmed()};
    if (error.isEmpty() || m_startup_error_messages.contains(error)) {
        return;
    }
    m_startup_error_messages.push_back(error);
}

void NodeModel::recordStartupWarningMessage(const QString& message)
{
    const QString warning{message.trimmed()};
    if (warning.isEmpty() || m_startup_warning_messages.contains(warning)) {
        return;
    }
    m_startup_warning_messages.push_back(warning);
}

void NodeModel::startNodeInitializionThread()
{
    if (m_initialization_requested) {
        return;
    }
    m_initialization_requested = true;
    Q_EMIT requestedInitialize();
}

void NodeModel::requestShutdown()
{
    RequireModelThread(this);
    if (m_shutdown_requested) {
        return;
    }
    m_shutdown_requested = true;
    stopShutdownPolling();
    // Interrupt must not wait behind a blocked read/RPC, and Interrupt itself
    // can take locks. Give it a separate worker while the shutdown page renders.
    m_shutdown_worker.post([node = &m_node] { node->startShutdown(); });
    Q_EMIT requestedShutdown();
}

void NodeModel::initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info)
{
    RequireModelThread(this);
    if (m_draining) return;
    if (success && (m_shutdown_requested || m_node.shutdownRequested())) {
        requestShutdown();
        return;
    }

    if (!success) {
        if (m_startup_failure_dialog_shown) {
            requestShutdown();
            Q_EMIT nodeInitialized();
            return;
        }
        setErrorState(true);
        refreshWarnings();
        QString startup_error{m_startup_error_messages.isEmpty() ? tr("Node initialization failed.") : m_startup_error_messages.join(QStringLiteral("\n\n"))};
        if (!m_startup_warning_messages.isEmpty()) {
            startup_error = tr("Startup warnings:") + QStringLiteral("\n") + m_startup_warning_messages.join(QStringLiteral("\n\n")) + QStringLiteral("\n\n") + startup_error;
        }
        m_startup_warning_messages.clear();
        setStartupError(startup_error);
        if (m_node.shutdownRequested()) {
            requestShutdown();
        }
    } else {
        m_startup_error_messages.clear();
        m_node_ready = true;
        Q_EMIT nodeReady();
        m_runtime_dialogs_enabled = true;
        refreshWarnings();
        showStartupWarnings();
        setBlockTipHeight(tip_info.block_height);
        setVerificationProgress(tip_info.verification_progress);
        m_last_block_time = tip_info.block_time;
        const auto generation = ++m_sync_generation;
        m_backend.submit([node = &m_node] { return node->isInitialBlockDownload(); },
                         [this, generation, height = tip_info.block_height](bool ibd) {
                             if (generation == m_sync_generation) setBlockSyncActive(height > 0 && ibd);
                         });
        refreshSnapshot();
        setHeaderSyncState(tip_info.header_height, tip_info.header_time, /*presync=*/false);
        refreshMempoolInfo();
        Q_EMIT setTimeRatioListInitial();
    }
    Q_EMIT nodeInitialized();
}

void NodeModel::handleRunawayException(const QString& message)
{
    setErrorState(true);
    setStartupError(message.isEmpty() ? tr("A fatal node error occurred.") : message);
}

void NodeModel::startShutdownPolling()
{
    if (m_shutdown_polling_timer_id != 0) {
        return;
    }
    m_shutdown_polling_timer_id = startTimer(200ms);
}

void NodeModel::stopShutdownPolling()
{
    if (m_shutdown_polling_timer_id == 0) {
        return;
    }
    killTimer(m_shutdown_polling_timer_id);
    m_shutdown_polling_timer_id = 0;
}

void NodeModel::timerEvent(QTimerEvent* event)
{
    if (event->timerId() != m_shutdown_polling_timer_id) {
        return;
    }
    if (m_node.shutdownRequested()) {
        requestShutdown();
    }
}

void NodeModel::ConnectToBlockTipSignal()
{
    assert(!m_handler_notify_block_tip);

    m_handler_notify_block_tip = m_node.handleNotifyBlockTip(
        [this]([[maybe_unused]] SynchronizationState state, interfaces::BlockTip tip, double verification_progress) {
            QMetaObject::invokeMethod(this, [this, state, block_height = tip.block_height, block_time = tip.block_time, verification_progress] {
                ++m_sync_generation;
                m_last_block_time = block_time;
                setBlockTipHeight(block_height);
                refreshSnapshot();
                setVerificationProgress(verification_progress);
                setBlockSyncActive(block_height > 0 && state != SynchronizationState::POST_INIT);

                Q_EMIT setTimeRatioList(block_time);
            }, Qt::QueuedConnection);
        });
}

void NodeModel::ConnectToHeaderTipSignal()
{
    assert(!m_handler_notify_header_tip);

    m_handler_notify_header_tip = m_node.handleNotifyHeaderTip(
        [this]([[maybe_unused]] SynchronizationState state, interfaces::BlockTip tip, bool presync) {
            QMetaObject::invokeMethod(this, [this, block_height = tip.block_height, block_time = tip.block_time, presync] {
                setHeaderSyncState(block_height, block_time, presync);
            }, Qt::QueuedConnection);
        });
}

void NodeModel::ConnectToNumConnectionsChangedSignal()
{
    assert(!m_handler_notify_num_peers_changed);

    m_handler_notify_num_peers_changed = m_node.handleNotifyNumConnectionsChanged(
        [this]([[maybe_unused]] int new_num_connections) {
            QMetaObject::invokeMethod(this, [this] {
                refreshPeerCounts();
            }, Qt::QueuedConnection);
        });
}

void NodeModel::ConnectToNetworkActiveChangedSignal()
{
    assert(!m_handler_notify_network_active_changed);

    m_handler_notify_network_active_changed = m_node.handleNotifyNetworkActiveChanged(
        [this](bool network_active) {
            QMetaObject::invokeMethod(this, [this, network_active] {
                if (m_pause != !network_active) {
                    m_pause = !network_active;
                    Q_EMIT pauseChanged(m_pause);
                }
            }, Qt::QueuedConnection);
        });
}

void NodeModel::ConnectToAlertChangedSignal()
{
    assert(!m_handler_notify_alert_changed);

    m_handler_notify_alert_changed = m_node.handleNotifyAlertChanged([this]() {
        QMetaObject::invokeMethod(this, [this] {
            refreshWarnings();
        }, Qt::QueuedConnection);
    });
}

void NodeModel::ConnectToRuntimeDialogSignals()
{
    assert(!m_handler_message_box);
    assert(!m_handler_question);

    m_handler_message_box = m_node.handleMessageBox(
        [this](const bilingual_str& message, unsigned int style) {
            showRuntimeMessageBox(
                QString::fromStdString(message.translated),
                style);
        });
    m_handler_question = m_node.handleQuestion(
        [this](const bilingual_str& message, [[maybe_unused]] const std::string& non_interactive_message, unsigned int style) {
            return showRuntimeQuestion(
                QString::fromStdString(message.translated),
                style);
        });
}

bool NodeModel::validateProxyAddress(QString address_port)
{
    uint16_t port{0};
    std::string addr_port{address_port.toStdString()};
    std::string hostname;
    // First, attempt to split the input address into hostname and port components.
    // We call SplitHostPort to validate that a port is provided in addr_port.
    // If either splitting fails or port is zero (not specified), return false.
    if (!SplitHostPort(addr_port, port, hostname) || !port) return false;

    // Create a service endpoint (CService) from the address and port.
    // If port is missing in addr_port, DEFAULT_PROXY_PORT is used as the fallback.
    CService serv(LookupNumeric(addr_port, DEFAULT_PROXY_PORT));

    // Construct the Proxy with the service endpoint and return if it's valid
    Proxy addrProxy = Proxy(serv, true);
    return addrProxy.IsValid();
}

QString NodeModel::defaultProxyAddress()
{
    return QString::fromStdString(std::string(DEFAULT_PROXY_HOST) + ":" + util::ToString(DEFAULT_PROXY_PORT));
}

bool NodeModel::disconnectPeer(int nodeId)
{
    RequireModelThread(this);
    if (m_draining || !m_node_ready || m_peer_action_pending) return false;
    m_peer_action_pending = true;
    Q_EMIT peerActionPendingChanged();
    m_backend.submit([node = &m_node, nodeId] { return node->disconnectById(nodeId); },
                     [this](bool success) {
        m_peer_action_pending = false;
        Q_EMIT peerActionPendingChanged();
        Q_EMIT peerActionFinished(success);
    });
    return true;
}

bool NodeModel::banPeer(const QString& rawAddress, int64_t banDuration)
{
    RequireModelThread(this);
    if (m_draining || !m_node_ready || m_peer_action_pending || banDuration <= 0) return false;
    auto addr = LookupHost(rawAddress.toStdString(), /*fAllowLookup=*/false);
    if (!addr) return false;
    m_peer_action_pending = true;
    Q_EMIT peerActionPendingChanged();
    m_backend.submit([node = &m_node, addr = *addr, banDuration] {
        const bool success = node->ban(addr, banDuration);
        if (success) node->disconnectByAddress(addr);
        return success;
    }, [this](bool success) {
        m_peer_action_pending = false;
        Q_EMIT peerActionPendingChanged();
        Q_EMIT peerActionFinished(success);
    });
    return true;
}

QVariantList NodeModel::nodeInformationRows()
{
    RequireModelThread(this);
    const int header_height{m_header_tip_height};
    const int64_t header_time{m_header_tip_time};
    const QString local_addresses{m_local_addresses.isEmpty() ? tr("None") : m_local_addresses};
    const int block_height{m_block_tip_height};
    const int64_t last_block_time{m_last_block_time};
    const QString warning_text{m_warning_list.empty() ? tr("None") : m_warning_list.join(QStringLiteral("\n"))};

    QVariantList rows;
    rows.push_back(InformationRow(tr("Client version"), fullClientVersion()));
    rows.push_back(InformationRow(tr("User agent"), QString::fromStdString(strSubVersion)));
    rows.push_back(InformationRow(tr("Datadir"), QString::fromStdString(fs::PathToString(gArgs.GetDataDirNet()))));
    rows.push_back(InformationRow(tr("Blocks dir"), QString::fromStdString(fs::PathToString(gArgs.GetBlocksDirPath()))));
    rows.push_back(InformationRow(tr("Startup time"), QDateTime::currentDateTime().addSecs(-TicksSeconds(GetUptime())).toString()));
    rows.push_back(InformationRow(tr("Network"), QString::fromStdString(Params().GetChainTypeString())));
    rows.push_back(InformationRow(tr("Block height"), QString::number(block_height)));
    rows.push_back(InformationRow(tr("Header height"), QString::number(header_height)));
    rows.push_back(InformationRow(tr("Header time"), header_time > 0 ? QDateTime::fromSecsSinceEpoch(header_time).toString() : tr("Unknown")));
    rows.push_back(InformationRow(tr("Last block time"), last_block_time > 0 ? QDateTime::fromSecsSinceEpoch(last_block_time).toString() : tr("Unknown")));
    rows.push_back(InformationRow(tr("Verification progress"), QStringLiteral("%1%").arg(QString::number(m_verification_progress * 100.0, 'f', 2))));
    rows.push_back(InformationRow(tr("Peers"), tr("%1 total (%2 inbound, %3 outbound)").arg(m_num_peers).arg(m_num_inbound_peers).arg(m_num_outbound_peers)));
    rows.push_back(InformationRow(tr("Network active"), m_node_ready ? (!m_pause ? tr("Yes") : tr("No")) : tr("Unknown")));
    rows.push_back(InformationRow(tr("Local addresses"), local_addresses));
    rows.push_back(InformationRow(tr("Warnings"), warning_text));
    return rows;
}

void NodeModel::showRuntimeMessageBox(const QString& message, unsigned int style)
{
    if (QThread::currentThread() == thread()) {
        showRuntimeDialogOnGuiThread(message, style, /*question=*/false);
        return;
    }

    if (!(style & CClientUIInterface::MODAL)) {
        QMetaObject::invokeMethod(this, [this, message, style] {
            showRuntimeDialogOnGuiThread(message, style, /*question=*/false);
        }, Qt::QueuedConnection);
        return;
    }

    QMetaObject::invokeMethod(this, [this, message, style] {
        showRuntimeDialogOnGuiThread(message, style, /*question=*/false);
    }, Qt::BlockingQueuedConnection);
}

bool NodeModel::showRuntimeQuestion(const QString& message, unsigned int style)
{
    if (QThread::currentThread() == thread()) {
        return showRuntimeDialogOnGuiThread(message, style, /*question=*/true);
    }

    bool result{false};
    QMetaObject::invokeMethod(this, [this, &result, message, style] {
        result = showRuntimeDialogOnGuiThread(message, style, /*question=*/true);
    }, Qt::BlockingQueuedConnection);
    return result;
}

bool NodeModel::showRuntimeDialogOnGuiThread(const QString& message, unsigned int style, bool question)
{
    if (!m_runtime_dialogs_enabled && !question) {
        if (style & CClientUIInterface::ICON_WARNING) {
            recordStartupWarningMessage(message);
            return false;
        }
        if (!(style & CClientUIInterface::MODAL)) {
            if (style & CClientUIInterface::ICON_ERROR) {
                recordStartupErrorMessage(message);
            }
            return false;
        }
    }

    const bool blocking{(style & CClientUIInterface::MODAL) || question};
    auto request{std::make_shared<RuntimeDialogRequest>()};
    request->message = message;
    request->style = style;
    request->question = question;
    if (!m_runtime_dialogs_enabled && (question || (style & CClientUIInterface::ICON_ERROR))) {
        m_startup_failure_dialog_shown = true;
    }

    QEventLoop loop;
    if (blocking) {
        request->loop = &loop;
    }

    if (m_runtime_dialog_active) {
        m_runtime_dialog_queue.push_back(request);
    } else {
        showRuntimeDialogRequest(request);
    }

    if (!blocking) {
        return false;
    }

    if (!request->answered) {
        loop.exec();
    }
    request->loop = nullptr;

    return request->answer;
}

void NodeModel::showRuntimeDialogRequest(const std::shared_ptr<RuntimeDialogRequest>& request)
{
    RequireModelThread(this);
    m_runtime_dialog_active = request;
    m_runtime_dialog_title = RuntimeDialogTitle(request->style);
    m_runtime_dialog_message = request->message;
    m_runtime_dialog_icon = RuntimeDialogIcon(request->style);
    m_runtime_dialog_buttons = RuntimeDialogButtons(request->style);
    m_runtime_dialog_question = request->question || HasMultipleRuntimeDialogButtons(m_runtime_dialog_buttons);
    m_runtime_dialog_visible = true;
    Q_EMIT runtimeDialogChanged();
}

void NodeModel::answerRuntimeDialog(unsigned int button)
{
    RequireModelThread(this);
    if (!m_runtime_dialog_active) {
        return;
    }

    auto answered_dialog{std::move(m_runtime_dialog_active)};
    answered_dialog->button = button;
    answered_dialog->answer = button == CClientUIInterface::BTN_OK;
    answered_dialog->answered = true;
    if (answered_dialog->loop) {
        answered_dialog->loop->quit();
    }

    if (!m_runtime_dialog_queue.empty()) {
        auto next_dialog{m_runtime_dialog_queue.front()};
        m_runtime_dialog_queue.pop_front();
        showRuntimeDialogRequest(next_dialog);
        return;
    }

    m_runtime_dialog_visible = false;
    Q_EMIT runtimeDialogChanged();
}

#ifdef ENABLE_TEST_AUTOMATION
void NodeModel::showRuntimeDialogForTest(const QString& message, unsigned int style, bool question)
{
    auto request{std::make_shared<RuntimeDialogRequest>()};
    request->message = message;
    request->style = style;
    request->question = question;

    if (m_runtime_dialog_active) {
        m_runtime_dialog_queue.push_back(request);
    } else {
        showRuntimeDialogRequest(request);
    }
}
#endif

void NodeModel::ConnectToBannedListChangedSignal()
{
    assert(!m_handler_notify_banned_list_changed);
    m_handler_notify_banned_list_changed = m_node.handleBannedListChanged([this]() {
        QMetaObject::invokeMethod(this, [this] {
            Q_EMIT bannedListChanged();
        }, Qt::QueuedConnection);
    });
}

void NodeModel::unsubscribeFromCoreSignals()
{
    if (m_handler_notify_block_tip) {
        m_handler_notify_block_tip->disconnect();
    }
    if (m_handler_notify_header_tip) {
        m_handler_notify_header_tip->disconnect();
    }
    if (m_handler_notify_num_peers_changed) {
        m_handler_notify_num_peers_changed->disconnect();
    }
    if (m_handler_notify_network_active_changed) {
        m_handler_notify_network_active_changed->disconnect();
    }
    if (m_handler_notify_alert_changed) {
        m_handler_notify_alert_changed->disconnect();
    }
    if (m_handler_message_box) {
        m_handler_message_box->disconnect();
    }
    if (m_handler_question) {
        m_handler_question->disconnect();
    }
    if (m_handler_notify_banned_list_changed) {
        m_handler_notify_banned_list_changed->disconnect();
    }
}
