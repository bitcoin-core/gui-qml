// Copyright (c) 2022-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_RPCCONSOLEMODEL_H
#define BITCOIN_QML_MODELS_RPCCONSOLEMODEL_H

#include <QAbstractListModel>
#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>

namespace interfaces {
class Node;
}

class RpcConsoleWorker;

/**
 * List model exposing the console output rows.
 *
 * Owned by RpcConsoleModel and exposed to QML via the outputModel property;
 * the MonospaceOutputView in CommandConsole.qml binds to it directly. Rows
 * are capped at kMaxRows — the oldest rows are dropped when the cap is hit,
 * protecting against runaway output from chatty RPCs.
 */
class RpcOutputListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        TimestampRole = Qt::UserRole + 1,
        ContentRole,
        CategoryRole,
    };

    static constexpr int kMaxRows = 5000;

    explicit RpcOutputListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void appendRow(const QString& timestamp, const QString& contentHtml, int category);
    void resetAll();

Q_SIGNALS:
    void countChanged();

private:
    struct Row {
        QString timestamp;
        QString contentHtml;
        int category;
    };
    QVector<Row> m_rows;
};

/**
 * Model for the RPC command console page.
 *
 * Exposes:
 *  - availableCommands  – sorted list of all RPC commands for autocomplete
 *  - executing          – true while an RPC call is in flight
 *  - outputModel        – list of output rows (timestamp + pre-formatted HTML)
 *  - requestColor / replyColor / errorColor / keyColor –
 *        palette used to render rows; QML writes these from Theme so the
 *        colours update when the user toggles dark/light mode (already-
 *        emitted rows keep their baked-in colours, which is intentional).
 *
 * Invokable from QML:
 *  - submitCommand(command, walletName) → bool (false if rejected, e.g. empty or
 *        refused while another command is executing)
 *  - browseHistory(direction, currentText) → QString
 *  - resetHistoryNavigation()
 *  - clear()
 */
class RpcConsoleModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool executing READ executing NOTIFY executingChanged)
    Q_PROPERTY(QStringList availableCommands READ availableCommands NOTIFY availableCommandsChanged)
    Q_PROPERTY(QAbstractListModel* outputModel READ outputModel CONSTANT)
    Q_PROPERTY(QColor requestColor MEMBER m_request_color)
    Q_PROPERTY(QColor replyColor   MEMBER m_reply_color)
    Q_PROPERTY(QColor errorColor   MEMBER m_error_color)
    Q_PROPERTY(QColor keyColor     MEMBER m_key_color)

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
    QAbstractListModel* outputModel() { return &m_output_model; }

    Q_INVOKABLE bool submitCommand(const QString& command, const QString& wallet_name = {});

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

private Q_SLOTS:
    void onResultReady(const QString& time, int category, const QString& rawText);

private:
    void setExecuting(bool executing);
    void appendFormattedRow(const QString& time, int category, const QString& rawText);

    interfaces::Node& m_node;
    bool m_executing{false};
    QStringList m_available_commands;
    RpcOutputListModel m_output_model;

    // Palette — populated from QML via the Q_PROPERTY MEMBERs. Sensible
    // fall-back values so the console is legible before QML binds them.
    QColor m_request_color{"#888888"};
    QColor m_reply_color{"#CCCCCC"};
    QColor m_error_color{"#EC6363"};
    QColor m_key_color{"#98C379"};

    // History (stores redacted/filtered versions only)
    QStringList m_history;
    int m_history_idx{-1};
    QString m_pending_text; // text being edited before browsing history
    QString m_last_wallet_name; // last wallet context surfaced in the output

    QThread m_worker_thread;
    RpcConsoleWorker* m_worker{nullptr};
};

#endif // BITCOIN_QML_MODELS_RPCCONSOLEMODEL_H
