// Copyright (c) 2022-2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_RPCCONSOLEMODEL_H
#define BITCOIN_QML_MODELS_RPCCONSOLEMODEL_H

#include <QObject>
#include <QStringList>
#include <QThread>

namespace interfaces {
class Node;
}

class RpcConsoleWorker;

/**
 * Model for the RPC command console page.
 *
 * Exposes:
 *  - availableCommands  – sorted list of all RPC commands for autocomplete
 *  - executing          – true while an RPC call is in flight
 *
 * Signals to QML:
 *  - commandResultReceived(time, category, escapedHtml) – new output line ready
 *  - clearRequested()                                   – QML should empty the list
 *
 * Invokable from QML:
 *  - submitCommand(command)
 *  - browseHistory(direction, currentText) → QString
 *  - resetHistoryNavigation()
 *  - clear()
 */
class RpcConsoleModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool executing READ executing NOTIFY executingChanged)
    Q_PROPERTY(QStringList availableCommands READ availableCommands NOTIFY availableCommandsChanged)

public:
    enum MessageCategory {
        CMD_REQUEST = 0,
        CMD_REPLY   = 1,
        CMD_ERROR   = 2
    };
    Q_ENUM(MessageCategory)

    explicit RpcConsoleModel(interfaces::Node& node, QObject* parent = nullptr);
    ~RpcConsoleModel();

    bool executing() const { return m_executing; }
    QStringList availableCommands() const { return m_available_commands; }

    Q_INVOKABLE void submitCommand(const QString& command);

    /**
     * Navigate command history.
     * @param direction  +1 = older entry, -1 = newer entry
     * @param currentText  current input text (saved as pending on first call)
     * @return  the history entry to display, or empty string if at newest end
     */
    Q_INVOKABLE QString browseHistory(int direction, const QString& currentText);

    /** Reset history navigation pointer (call when user starts typing a new command). */
    Q_INVOKABLE void resetHistoryNavigation();

    /** Clear the console output. */
    Q_INVOKABLE void clear();

public Q_SLOTS:
    void onNodeInitialized();

Q_SIGNALS:
    void executingChanged();
    void availableCommandsChanged();

    /** Emitted (on the main thread) when a new console line is ready to display. */
    void commandResultReceived(const QString& time, int category, const QString& escapedHtml);

    /** Emitted when the console should be cleared. */
    void clearRequested();

private Q_SLOTS:
    void onResultReady(const QString& time, int category, const QString& escapedHtml);

private:
    void setExecuting(bool executing);

    interfaces::Node& m_node;
    bool m_executing{false};
    QStringList m_available_commands;

    // History (stores redacted/filtered versions only)
    QStringList m_history;
    int m_history_idx{-1};
    QString m_pending_text; // text being edited before browsing history

    QThread m_worker_thread;
    RpcConsoleWorker* m_worker{nullptr};
};

#endif // BITCOIN_QML_MODELS_RPCCONSOLEMODEL_H
