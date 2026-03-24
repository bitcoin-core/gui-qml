// Copyright (c) 2022-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/rpcconsolemodel.h>

#include <interfaces/node.h>
#include <qml/models/rpccommandexecutor.h>
#include <univalue.h>

#include <algorithm>
#include <string>

#include <QDateTime>
#include <QString>
#include <QStringList>

static constexpr int HISTORY_MAX = 50;


/**
 * Worker object that lives on a background QThread and executes RPC commands
 * without blocking the UI thread.
 */
class RpcConsoleWorker : public QObject
{
    Q_OBJECT
public:
    explicit RpcConsoleWorker(interfaces::Node& node, QObject* parent = nullptr)
        : QObject(parent), m_node(node) {}

public Q_SLOTS:
    void execute(const QString& command)
    {
        QString time = QDateTime::currentDateTime().toString("hh:mm:ss");
        try {
            // Append "\n" as the parser requires a line terminator to finalize
            // the last token. submitCommand() passes the raw command without it;
            // the worker appends it here.
            std::string executableCommand = command.toStdString() + "\n";

            if (command.trimmed().compare("help-console", Qt::CaseInsensitive) == 0) {
                Q_EMIT resultReady(time, RpcConsoleModel::CMD_REPLY,
                                   tr("\n"
                                      "This console accepts RPC commands using the standard syntax.\n"
                                      "   example:    getblockhash 0\n\n"
                                      "This console can also accept RPC commands using the parenthesized syntax.\n"
                                      "   example:    getblockhash(0)\n\n"
                                      "Commands may be nested when specified with the parenthesized syntax.\n"
                                      "   example:    getblock(getblockhash(0) 1)\n\n"
                                      "A space or a comma can be used to delimit arguments for either syntax.\n"
                                      "   example:    getblockhash 0\n"
                                      "               getblockhash,0\n\n"
                                      "Named results can be queried with a non-quoted key string in brackets.\n"
                                      "   example:    getblock(getblockhash(0) 1)[tx]\n\n"
                                      "Results without keys can be queried with an integer in brackets.\n"
                                      "   example:    getblock(getblockhash(0),1)[tx][0]\n\n").toHtmlEscaped());
                return;
            }

            std::string result;
            if (!RpcCommandExecutor::RPCExecuteCommandLine(m_node, result, executableCommand)) {
                Q_EMIT resultReady(time, RpcConsoleModel::CMD_ERROR,
                                   tr("Parse error: unbalanced ' or \""));
                return;
            }
            Q_EMIT resultReady(time, RpcConsoleModel::CMD_REPLY,
                               QString::fromStdString(result).toHtmlEscaped());
        } catch (UniValue& objError) {
            try {
                int code = objError.find_value("code").getInt<int>();
                std::string message = objError.find_value("message").get_str();
                QString msg = QString::fromStdString(message)
                              + " (code " + QString::number(code) + ")";
                Q_EMIT resultReady(time, RpcConsoleModel::CMD_ERROR, msg.toHtmlEscaped());
            } catch (const std::runtime_error&) {
                Q_EMIT resultReady(time, RpcConsoleModel::CMD_ERROR,
                                   QString::fromStdString(objError.write()).toHtmlEscaped());
            }
        } catch (const std::exception& e) {
            Q_EMIT resultReady(time, RpcConsoleModel::CMD_ERROR,
                               tr("Error: %1").arg(QString::fromStdString(e.what())).toHtmlEscaped());
        }
    }

Q_SIGNALS:
    void resultReady(const QString& time, int category, const QString& escapedHtml);

private:
    interfaces::Node& m_node;
};

#include <qml/models/rpcconsolemodel.moc>

// ---------------------------------------------------------------------------

RpcConsoleModel::RpcConsoleModel(interfaces::Node& node, QObject* parent)
    : QObject(parent), m_node(node)
{
    m_worker = new RpcConsoleWorker(m_node);
    m_worker->moveToThread(&m_worker_thread);

    connect(m_worker, &RpcConsoleWorker::resultReady,
            this,     &RpcConsoleModel::onResultReady,
            Qt::QueuedConnection);

    connect(&m_worker_thread, &QThread::finished,
            m_worker, &RpcConsoleWorker::deleteLater);

    m_worker_thread.start();
}

RpcConsoleModel::~RpcConsoleModel()
{
    m_worker_thread.quit();
    m_worker_thread.wait();
}

void RpcConsoleModel::submitCommand(const QString& command)
{
    if (command.trimmed().isEmpty()) return;

    // Compute the filtered version to store in history (passwords redacted).
    // Mirrors Qt5's rpcconsole.cpp:994-1004: the fExecute=false parse call must be
    // wrapped in try/catch because the parser throws std::runtime_error on invalid syntax
    // regardless of fExecute.
    std::string filtered;
    std::string dummy;
    QString time = QDateTime::currentDateTime().toString("hh:mm:ss");
    try {
        if (!RpcCommandExecutor::RPCParseCommandLine(nullptr, dummy, command.toStdString() + "\n",
                                                     false, &filtered)) {
            Q_EMIT commandResultReceived(time, CMD_ERROR,
                                         tr("Parse error: unbalanced ' or \""));
            return;
        }
    } catch (const std::runtime_error& e) {
        Q_EMIT commandResultReceived(time, CMD_ERROR,
                                     tr("Error: %1").arg(
                                         QString::fromStdString(e.what())).toHtmlEscaped());
        return;
    }
    QString filteredCmd = QString::fromStdString(filtered).trimmed();

    // Add to history (deduplicate consecutive identical entries).
    if (m_history.isEmpty() || m_history.last() != filteredCmd) {
        m_history.append(filteredCmd);
        if (m_history.size() > HISTORY_MAX) {
            m_history.removeFirst();
        }
    }
    m_history_idx = -1;
    m_pending_text.clear();

    // Emit the request line immediately (main thread) so QML sees it before
    // the async reply arrives. Mirrors Qt GUI RPCConsole behaviour.
    Q_EMIT commandResultReceived(time, CMD_REQUEST, filteredCmd.toHtmlEscaped());

    setExecuting(true);

    // Dispatch to worker thread.
    QMetaObject::invokeMethod(m_worker, "execute", Qt::QueuedConnection,
                              Q_ARG(QString, command));
}

QString RpcConsoleModel::browseHistory(int direction, const QString& currentText)
{
    if (m_history.isEmpty()) return currentText;

    // Save the text being composed the first time we enter history browsing.
    if (m_history_idx == -1) {
        m_pending_text = currentText;
    }

    int newIdx = m_history_idx + direction;
    // Clamp: oldest entry is index 0 (size-1 from back), newest is -1.
    if (newIdx < 0) {
        // Past the newest end — restore pending text.
        m_history_idx = -1;
        return m_pending_text;
    }
    if (newIdx >= m_history.size()) {
        newIdx = m_history.size() - 1;
    }
    m_history_idx = newIdx;
    // history is stored oldest-first; m_history_idx 0 = most recent, size-1 = oldest.
    return m_history.at(m_history.size() - 1 - m_history_idx);
}

void RpcConsoleModel::resetHistoryNavigation()
{
    m_history_idx = -1;
    m_pending_text.clear();
}

void RpcConsoleModel::clear()
{
    Q_EMIT clearRequested();
}

void RpcConsoleModel::onNodeInitialized()
{
    std::vector<std::string> cmds = m_node.listRpcCommands();
    QStringList list;
    list.reserve(static_cast<int>(cmds.size()));
    for (const auto& c : cmds) {
        list.append(QString::fromStdString(c));
    }
    list.sort(Qt::CaseInsensitive);
    list.removeDuplicates();
    m_available_commands = list;
    Q_EMIT availableCommandsChanged();
}

void RpcConsoleModel::onResultReady(const QString& time, int category, const QString& escapedHtml)
{
    // Emit the result first so the output line appears before the submit button
    // is re-enabled, avoiding a single-frame window where the user could submit
    // again before seeing the reply.
    Q_EMIT commandResultReceived(time, category, escapedHtml);
    setExecuting(false);
}

void RpcConsoleModel::setExecuting(bool executing)
{
    if (m_executing != executing) {
        m_executing = executing;
        Q_EMIT executingChanged();
    }
}
