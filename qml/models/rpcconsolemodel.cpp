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

// ---------------------------------------------------------------------------
// RpcOutputListModel
// ---------------------------------------------------------------------------

RpcOutputListModel::RpcOutputListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int RpcOutputListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant RpcOutputListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const Row& r = m_rows.at(index.row());
    switch (role) {
    case TimestampRole: return r.timestamp;
    case ContentRole:   return r.contentHtml;
    }
    return {};
}

QHash<int, QByteArray> RpcOutputListModel::roleNames() const
{
    return {
        {TimestampRole, "timestamp"},
        {ContentRole,   "content"},
    };
}

void RpcOutputListModel::appendRow(const QString& timestamp, const QString& contentHtml)
{
    // Cap the row buffer. Drop oldest rows until we have room for the new one.
    if (m_rows.size() >= kMaxRows) {
        const int to_remove = m_rows.size() - kMaxRows + 1;
        beginRemoveRows({}, 0, to_remove - 1);
        m_rows.remove(0, to_remove);
        endRemoveRows();
    }
    beginInsertRows({}, m_rows.size(), m_rows.size());
    m_rows.append(Row{timestamp, contentHtml});
    endInsertRows();
    Q_EMIT countChanged();
}

void RpcOutputListModel::resetAll()
{
    if (m_rows.isEmpty()) return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    Q_EMIT countChanged();
}

// ---------------------------------------------------------------------------
// Reply formatting helpers
// ---------------------------------------------------------------------------

namespace {

// HTML-escape `text` and convert whitespace for monospace RichText rendering:
// newlines become <br>, leading-indent runs of spaces become &nbsp; runs so
// indentation is preserved.
QString EscapeAndConvertWhitespace(const QString& text)
{
    const QString escaped = text.toHtmlEscaped();
    QString out;
    out.reserve(escaped.size());
    bool at_line_start = true;
    for (const QChar c : escaped) {
        if (c == QLatin1Char('\n')) {
            out.append(QStringLiteral("<br>"));
            at_line_start = true;
        } else if (c == QLatin1Char(' ') && at_line_start) {
            out.append(QStringLiteral("&nbsp;"));
        } else {
            out.append(c);
            if (c != QLatin1Char(' ')) at_line_start = false;
        }
    }
    return out;
}

void AppendIndent(QString& out, int depth)
{
    for (int i = 0; i < depth * 2; ++i) out.append(QStringLiteral("&nbsp;"));
}

void AppendValue(QString& out, const UniValue& v, int depth, const QString& key_color_hex);

void AppendObject(QString& out, const UniValue& obj, int depth, const QString& key_color_hex)
{
    if (obj.empty()) { out.append(QStringLiteral("{}")); return; }
    out.append(QStringLiteral("{<br>"));
    const std::vector<std::string>& keys = obj.getKeys();
    const std::vector<UniValue>& vals = obj.getValues();
    for (size_t i = 0; i < keys.size(); ++i) {
        AppendIndent(out, depth + 1);
        out.append(QStringLiteral("<span style='color:"));
        out.append(key_color_hex);
        out.append(QStringLiteral("'>\""));
        out.append(QString::fromStdString(keys[i]).toHtmlEscaped());
        out.append(QStringLiteral("\"</span>: "));
        AppendValue(out, vals[i], depth + 1, key_color_hex);
        if (i + 1 < keys.size()) out.append(QLatin1Char(','));
        out.append(QStringLiteral("<br>"));
    }
    AppendIndent(out, depth);
    out.append(QLatin1Char('}'));
}

void AppendArray(QString& out, const UniValue& arr, int depth, const QString& key_color_hex)
{
    if (arr.empty()) { out.append(QStringLiteral("[]")); return; }
    out.append(QStringLiteral("[<br>"));
    for (size_t i = 0; i < arr.size(); ++i) {
        AppendIndent(out, depth + 1);
        AppendValue(out, arr[i], depth + 1, key_color_hex);
        if (i + 1 < arr.size()) out.append(QLatin1Char(','));
        out.append(QStringLiteral("<br>"));
    }
    AppendIndent(out, depth);
    out.append(QLatin1Char(']'));
}

void AppendValue(QString& out, const UniValue& v, int depth, const QString& key_color_hex)
{
    switch (v.getType()) {
    case UniValue::VOBJ: AppendObject(out, v, depth, key_color_hex); break;
    case UniValue::VARR: AppendArray(out, v, depth, key_color_hex); break;
    case UniValue::VSTR:
        out.append(QLatin1Char('"'));
        out.append(QString::fromStdString(v.get_str()).toHtmlEscaped());
        out.append(QLatin1Char('"'));
        break;
    case UniValue::VNUM:
        out.append(QString::fromStdString(v.getValStr()));
        break;
    case UniValue::VBOOL:
        out.append(v.get_bool() ? QStringLiteral("true") : QStringLiteral("false"));
        break;
    case UniValue::VNULL:
        out.append(QStringLiteral("null"));
        break;
    default: break;
    }
}

// Format an RPC reply body into HTML suitable for RichText rendering.
// Walks the UniValue tree directly so keys are identified structurally —
// a string value containing `": "` (e.g. `"note": "see section:"`) is never
// mistaken for a key. Non-JSON replies (help text, scalar strings,
// truncation notices) fall through to the plain-escape path.
QString FormatJsonReply(const std::string& raw, const QString& key_color_hex)
{
    UniValue v;
    if (!v.read(raw) ||
        (v.getType() != UniValue::VOBJ && v.getType() != UniValue::VARR)) {
        return EscapeAndConvertWhitespace(QString::fromStdString(raw));
    }
    QString out;
    AppendValue(out, v, 0, key_color_hex);
    return out;
}

QString WrapColor(const QString& inner, const QColor& color)
{
    return QStringLiteral("<span style='color:%1'>%2</span>")
        .arg(color.name(), inner);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// RpcConsoleWorker — executes RPC commands on a background thread
// ---------------------------------------------------------------------------

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
                                      "   example:    getblock(getblockhash(0),1)[tx][0]\n\n"));
                return;
            }

            std::string result;
            if (!RpcCommandExecutor::RPCExecuteCommandLine(m_node, result, executableCommand)) {
                Q_EMIT resultReady(time, RpcConsoleModel::CMD_ERROR,
                                   tr("Parse error: unbalanced ' or \""));
                return;
            }
            static constexpr int MAX_RESULT_CHARS = 50'000;
            QString resultStr = QString::fromStdString(result);
            const bool truncated = resultStr.size() > MAX_RESULT_CHARS;
            if (truncated) {
                resultStr = resultStr.left(MAX_RESULT_CHARS);
                resultStr += "\n" + tr("[Output truncated at %1 characters. "
                                       "Use bitcoin-cli for the full result.]")
                                       .arg(MAX_RESULT_CHARS);
            }
            Q_EMIT resultReady(time, RpcConsoleModel::CMD_REPLY, resultStr);
        } catch (UniValue& objError) {
            try {
                int code = objError.find_value("code").getInt<int>();
                std::string message = objError.find_value("message").get_str();
                QString msg = QString::fromStdString(message)
                              + " (code " + QString::number(code) + ")";
                Q_EMIT resultReady(time, RpcConsoleModel::CMD_ERROR, msg);
            } catch (const std::runtime_error&) {
                Q_EMIT resultReady(time, RpcConsoleModel::CMD_ERROR,
                                   QString::fromStdString(objError.write()));
            }
        } catch (const std::exception& e) {
            Q_EMIT resultReady(time, RpcConsoleModel::CMD_ERROR,
                               tr("Error: %1").arg(QString::fromStdString(e.what())));
        }
    }

Q_SIGNALS:
    void resultReady(const QString& time, int category, const QString& rawText);

private:
    interfaces::Node& m_node;
};

#include <qml/models/rpcconsolemodel.moc>

// ---------------------------------------------------------------------------
// RpcConsoleModel
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

void RpcConsoleModel::appendFormattedRow(const QString& time, int category, const QString& rawText)
{
    QString body;
    QColor color;
    QString prefix;
    switch (category) {
    case CMD_REQUEST:
        body = EscapeAndConvertWhitespace(rawText);
        color = m_request_color;
        prefix = QStringLiteral("&gt;&gt; ");
        break;
    case CMD_REPLY:
        body = FormatJsonReply(rawText.toStdString(), m_key_color.name());
        color = m_reply_color;
        break;
    case CMD_ERROR:
    default:
        body = EscapeAndConvertWhitespace(rawText);
        color = m_error_color;
        prefix = QStringLiteral("!! ");
        break;
    }
    m_output_model.appendRow(QStringLiteral("[%1]").arg(time),
                             WrapColor(prefix + body, color));
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
            appendFormattedRow(time, CMD_ERROR, tr("Parse error: unbalanced ' or \""));
            return;
        }
    } catch (const std::runtime_error& e) {
        appendFormattedRow(time, CMD_ERROR,
                           tr("Error: %1").arg(QString::fromStdString(e.what())));
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

    // Append the request line immediately (main thread) so QML sees it before
    // the async reply arrives. Mirrors Qt GUI RPCConsole behaviour.
    appendFormattedRow(time, CMD_REQUEST, filteredCmd);

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
    m_output_model.resetAll();
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

void RpcConsoleModel::onResultReady(const QString& time, int category, const QString& rawText)
{
    // Append the row first so the output line appears before the submit button
    // is re-enabled, avoiding a single-frame window where the user could submit
    // again before seeing the reply.
    appendFormattedRow(time, category, rawText);
    setExecuting(false);
}

void RpcConsoleModel::setExecuting(bool executing)
{
    if (m_executing != executing) {
        m_executing = executing;
        Q_EMIT executingChanged();
    }
}
