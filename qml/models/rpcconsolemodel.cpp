// Copyright (c) 2022-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/rpcconsolemodel.h>

#include <interfaces/node.h>
#include <qml/models/rpccommandexecutor.h>
#include <univalue.h>

#include <algorithm>
#include <string>
#include <utility>

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
    case TimestampRole:    return r.timestamp;
    case ContentRole:      return r.contentHtml;
    case PlainContentRole: return r.contentPlain;
    case CategoryRole:     return r.category;
    }
    return {};
}

QHash<int, QByteArray> RpcOutputListModel::roleNames() const
{
    return {
        {TimestampRole,    "timestamp"},
        {ContentRole,      "content"},
        {PlainContentRole, "plainContent"},
        {CategoryRole,     "category"},
    };
}

QVariantMap RpcOutputListModel::get(int row) const
{
    if (row < 0 || row >= m_rows.size()) return {};
    const Row& r = m_rows.at(row);
    return QVariantMap{
        {QStringLiteral("timestamp"),    r.timestamp},
        {QStringLiteral("content"),      r.contentHtml},
        {QStringLiteral("plainContent"), r.contentPlain},
        {QStringLiteral("category"),     r.category},
    };
}

void RpcOutputListModel::appendLines(const QString& timestamp,
                                     const QVector<OutputLine>& lines,
                                     int category)
{
    if (lines.isEmpty()) return;

    // A single reply can exceed the cap on its own; keep only its tail.
    const int keep = std::min<int>(lines.size(), kMaxRows);
    const int skipped = lines.size() - keep;

    if (m_rows.size() + keep > kMaxRows) {
        const int to_remove = m_rows.size() + keep - kMaxRows;
        beginRemoveRows({}, 0, to_remove - 1);
        m_rows.remove(0, to_remove);
        endRemoveRows();
    }

    // One insert signal for the whole block, not one per line.
    const int first = m_rows.size();
    beginInsertRows({}, first, first + keep - 1);
    m_rows.reserve(first + keep);
    for (int i = skipped; i < lines.size(); ++i) {
        m_rows.append(Row{i == 0 ? timestamp : QString{},
                          lines.at(i).html, lines.at(i).plain, category});
    }
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

using OutputLine = RpcOutputListModel::OutputLine;

/**
 * Accumulates output a line at a time, in two representations: HTML for the
 * delegate, and the plain text that HTML renders to. Recording the plain text
 * here beats deriving it later by stripping tags, which would have to reproduce
 * Qt's HTML-to-document mapping exactly for search offsets to line up.
 */
class LineSink
{
public:
    /** Tags, which render to nothing. */
    void markup(const QString& html) { m_html += html; }

    /** Characters needing no escaping, e.g. JSON punctuation. */
    void raw(QLatin1String chars) { m_html += chars; m_plain += chars; }
    void raw(QChar c) { m_html += c; m_plain += c; }

    /** Arbitrary text, escaped in the markup. */
    void text(const QString& t) { m_html += t.toHtmlEscaped(); m_plain += t; }

    void indent(int count)
    {
        for (int i = 0; i < count; ++i) {
            m_html += QStringLiteral("&nbsp;");
            m_plain += QChar{0x00A0};
        }
    }

    void newLine()
    {
        m_lines.append(OutputLine{m_html, m_plain});
        m_html.clear();
        m_plain.clear();
    }

    QVector<OutputLine> take()
    {
        newLine();
        return std::move(m_lines);
    }

private:
    QString m_html;
    QString m_plain;
    QVector<OutputLine> m_lines;
};

// Escape `text` and split it into lines. An HTML renderer collapses a run of
// spaces to one, which would misalign the space-padded columns in `help`
// output, so every space in a run but the last is emitted as &nbsp; — the last
// stays an ordinary space so searching for "two words" still matches.
QVector<OutputLine> EscapeToLines(const QString& text)
{
    LineSink out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        bool at_line_start = true;
        int j = 0;
        while (j < line.size()) {
            if (line.at(j) != QLatin1Char(' ')) {
                int k = j;
                while (k < line.size() && line.at(k) != QLatin1Char(' ')) ++k;
                out.text(line.mid(j, k - j));
                at_line_start = false;
                j = k;
                continue;
            }
            int k = j;
            while (k < line.size() && line.at(k) == QLatin1Char(' ')) ++k;
            const int run = k - j;
            if (at_line_start) {
                out.indent(run);
            } else {
                out.indent(run - 1);
                out.raw(QLatin1Char(' '));
            }
            j = k;
        }
        if (i + 1 < lines.size()) out.newLine();
    }
    return out.take();
}

void AppendValue(LineSink& out, const UniValue& v, int depth, const QString& key_color_hex);

void AppendObject(LineSink& out, const UniValue& obj, int depth, const QString& key_color_hex)
{
    if (obj.empty()) { out.raw(QLatin1String("{}")); return; }
    out.raw(QLatin1Char('{'));
    out.newLine();
    const std::vector<std::string>& keys = obj.getKeys();
    const std::vector<UniValue>& vals = obj.getValues();
    for (size_t i = 0; i < keys.size(); ++i) {
        out.indent((depth + 1) * 2);
        out.markup(QStringLiteral("<span style='color:") + key_color_hex + QStringLiteral("'>"));
        out.raw(QLatin1Char('"'));
        out.text(QString::fromStdString(keys[i]));
        out.raw(QLatin1Char('"'));
        out.markup(QStringLiteral("</span>"));
        out.raw(QLatin1String(": "));
        AppendValue(out, vals[i], depth + 1, key_color_hex);
        if (i + 1 < keys.size()) out.raw(QLatin1Char(','));
        out.newLine();
    }
    out.indent(depth * 2);
    out.raw(QLatin1Char('}'));
}

void AppendArray(LineSink& out, const UniValue& arr, int depth, const QString& key_color_hex)
{
    if (arr.empty()) { out.raw(QLatin1String("[]")); return; }
    out.raw(QLatin1Char('['));
    out.newLine();
    for (size_t i = 0; i < arr.size(); ++i) {
        out.indent((depth + 1) * 2);
        AppendValue(out, arr[i], depth + 1, key_color_hex);
        if (i + 1 < arr.size()) out.raw(QLatin1Char(','));
        out.newLine();
    }
    out.indent(depth * 2);
    out.raw(QLatin1Char(']'));
}

void AppendValue(LineSink& out, const UniValue& v, int depth, const QString& key_color_hex)
{
    switch (v.getType()) {
    case UniValue::VOBJ: AppendObject(out, v, depth, key_color_hex); break;
    case UniValue::VARR: AppendArray(out, v, depth, key_color_hex); break;
    case UniValue::VSTR:
        out.raw(QLatin1Char('"'));
        out.text(QString::fromStdString(v.get_str()));
        out.raw(QLatin1Char('"'));
        break;
    case UniValue::VNUM:
        out.text(QString::fromStdString(v.getValStr()));
        break;
    case UniValue::VBOOL:
        out.raw(v.get_bool() ? QLatin1String("true") : QLatin1String("false"));
        break;
    case UniValue::VNULL:
        out.raw(QLatin1String("null"));
        break;
    default: break;
    }
}

// Format an RPC reply body into one OutputLine per line.
// Walks the UniValue tree directly so keys are identified structurally —
// a string value containing `": "` (e.g. `"note": "see section:"`) is never
// mistaken for a key. Non-JSON replies (help text, scalar strings,
// truncation notices) fall through to the plain-escape path.
QVector<OutputLine> FormatJsonReply(const std::string& raw, const QString& key_color_hex)
{
    UniValue v;
    if (!v.read(raw) ||
        (v.getType() != UniValue::VOBJ && v.getType() != UniValue::VARR)) {
        return EscapeToLines(QString::fromStdString(raw));
    }
    LineSink out;
    AppendValue(out, v, 0, key_color_hex);
    return out.take();
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
    void execute(const QString& command, const QString& wallet_name)
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
            if (!RpcCommandExecutor::RPCExecuteCommandLine(m_node, result, executableCommand, nullptr, wallet_name)) {
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
    QVector<RpcOutputListModel::OutputLine> lines;
    switch (category) {
    case CMD_REPLY:
        lines = FormatJsonReply(rawText.toStdString(), m_key_color.name());
        break;
    case CMD_REQUEST:
    case CMD_ERROR:
    default:
        lines = EscapeToLines(rawText);
        break;
    }
    m_output_model.appendLines(time, lines, category);
}

bool RpcConsoleModel::submitCommand(const QString& command, const QString& wallet_name)
{
    const QString trimmed_command = command.trimmed();
    if (trimmed_command.isEmpty()) return false;

    // Compute the filtered version to store in history (passwords redacted).
    // Mirrors Qt5's rpcconsole.cpp:994-1004: the fExecute=false parse call must be
    // wrapped in try/catch because the parser throws std::runtime_error on invalid syntax
    // regardless of fExecute.
    std::string filtered;
    std::string dummy;
    QString time = QDateTime::currentDateTime().toString("hh:mm:ss");
    try {
        if (!RpcCommandExecutor::RPCParseCommandLine(nullptr, dummy, trimmed_command.toStdString() + "\n",
                                                     false, &filtered)) {
            appendFormattedRow(time, CMD_ERROR, tr("Parse error: unbalanced ' or \""));
            return true;
        }
    } catch (const std::runtime_error& e) {
        appendFormattedRow(time, CMD_ERROR,
                           tr("Error: %1").arg(QString::fromStdString(e.what())));
        return true;
    }
    QString filteredCmd = QString::fromStdString(filtered).trimmed();

    // A special case allows requesting shutdown even while a long-running command
    // is executing, mirroring Core's RPCConsole::on_lineEdit_returnPressed().
    // "stop" runs synchronously on the calling thread, so it can abort a command
    // that is blocking the worker, and returns before the request is echoed or
    // added to history: the GUI shuts down immediately, so that output is never
    // seen.
    if (trimmed_command == QLatin1String("stop")) {
        std::string result;
        RpcCommandExecutor::RPCExecuteCommandLine(m_node, result, trimmed_command.toStdString());
        return true;
    }

    // Keyboard Enter reaches submitCommand() directly, so this guard, not just the
    // disabled Run button, is what serialises execution: a new command is refused
    // until the in-flight reply arrives.
    if (m_executing) return false;

    // Add to history (deduplicate consecutive identical entries).
    if (m_history.isEmpty() || m_history.last() != filteredCmd) {
        m_history.append(filteredCmd);
        if (m_history.size() > HISTORY_MAX) {
            m_history.removeFirst();
        }
    }
    m_history_idx = -1;
    m_pending_text.clear();

    // Surface the active wallet context whenever it changes, mirroring Qt Widgets.
    if (m_last_wallet_name != wallet_name) {
        if (!wallet_name.isEmpty()) {
            //: RPC console message shown when a command is run against a specific
            //: wallet. %1 is the wallet name.
            appendFormattedRow(time, CMD_REQUEST,
                               tr("Executing command using \"%1\" wallet").arg(wallet_name));
        } else {
            //: RPC console message shown when a command is run without any wallet
            //: context (the node has no wallet selected).
            appendFormattedRow(time, CMD_REQUEST, tr("Executing command without any wallet"));
        }
        m_last_wallet_name = wallet_name;
    }

    // Append the request line immediately (main thread) so QML sees it before
    // the async reply arrives. Mirrors Qt GUI RPCConsole behaviour.
    appendFormattedRow(time, CMD_REQUEST, filteredCmd);

    setExecuting(true);

    // Dispatch to worker thread.
    QMetaObject::invokeMethod(m_worker, "execute", Qt::QueuedConnection,
                              Q_ARG(QString, trimmed_command),
                              Q_ARG(QString, wallet_name));
    return true;
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
        list.append(QStringLiteral("help ") + QString::fromStdString(c));
    }
    list.append(QStringLiteral("help-console"));
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
