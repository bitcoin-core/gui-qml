// Copyright (c) 2022-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/rpcconsolemodel.h>
#include <qml/asyncjoin.h>
#include <qml/retainedvalue.h>

#include <interfaces/node.h>
#include <qml/models/rpccommandexecutor.h>
#include <univalue.h>

#include <algorithm>
#include <string>
#include <stdexcept>
#include <vector>
#include <atomic>
#include <mutex>
#include <limits>

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
    case CategoryRole:  return r.category;
    case ContinuationRole: return r.continuation;
    case CommandIdRole: return QVariant::fromValue(r.command_id);
    case GenerationRole: return QVariant::fromValue(r.generation);
    case WalletContextRole: return r.context.target == RpcCommandContext::Target::Wallet;
    case WalletNameRole: return r.context.wallet_name;
    }
    return {};
}

QHash<int, QByteArray> RpcOutputListModel::roleNames() const
{
    return {
        {TimestampRole, "timestamp"},
        {ContentRole,   "content"},
        {CategoryRole,  "category"},
        {ContinuationRole, "continuation"},
        {CommandIdRole, "commandId"},
        {GenerationRole, "generation"},
        {WalletContextRole, "walletContext"},
        {WalletNameRole, "walletName"},
    };
}

void RpcOutputListModel::appendRows(const Batch& rows, int offset, int count)
{
    if (offset < 0 || offset >= rows.size()) return;
    count = std::min(count, int(rows.size()) - offset);
    if (count <= 0) return;
    // Public callers cannot bypass the chunk/aggregate limits. Capture only a
    // bounded newest suffix; an oversized rich-text row gets a plain notice.
    Batch bounded;
    qsizetype incoming{0};
    for (int i = count - 1; i >= std::max(0, count - kMaxRows); --i) {
        auto row = rows[offset + i];
        if (row.contentHtml.size() > 8192) row.contentHtml = tr("[Output row omitted by the viewer limit.]");
        row.timestamp = row.timestamp.left(32);
        row.context.wallet_name = row.context.wallet_name.left(1024);
        if (incoming + row.contentHtml.size() > kMaxCharacters) break;
        incoming += row.contentHtml.size(); bounded.append(std::move(row));
    }
    std::reverse(bounded.begin(), bounded.end());
    count = bounded.size();
    if (!count) return;
    int remove{0};
    while (remove < m_rows.size() && (m_rows.size() - remove + count > kMaxRows || m_characters + incoming > kMaxCharacters)) {
        m_characters -= m_rows[remove++].contentHtml.size();
    }
    if (remove) {
        beginRemoveRows({}, 0, remove - 1);
        m_rows.remove(0, remove);
        endRemoveRows();
    }
    const int first = m_rows.size();
    beginInsertRows({}, first, first + count - 1);
    for (const auto& row : bounded) m_rows.append(row);
    m_characters += incoming;
    endInsertRows();
    Q_EMIT countChanged();
}

std::shared_ptr<const RpcOutputListModel::Batch> RpcOutputListModel::releaseRows(bool notify)
{
    if (m_rows.isEmpty()) return {};
    if (notify) beginResetModel();
    auto rows = std::make_shared<const Batch>(std::exchange(m_rows, {}));
    m_characters = 0;
    if (notify) { endResetModel(); Q_EMIT countChanged(); }
    return rows;
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

void AppendValue(QString& out, const UniValue& value, const QString& key_color_hex)
{
    struct Frame {
        const UniValue* value;
        size_t next{0};
    };
    std::vector<Frame> stack{{&value}};
    while (!stack.empty()) {
        // Deeply nested JSON can expand quadratically through indentation.
        // Bound the rendered form as well as the original UTF-8 response.
        if (out.size() > 2 * 1024 * 1024) {
            out = QObject::tr("[Output truncated while formatting. Use bitcoin-cli for the full result.]");
            return;
        }
        auto& frame{stack.back()};
        const auto& current{*frame.value};
        const bool object{current.isObject()};
        if (object || current.isArray()) {
            if (frame.next == 0) {
                out.append(object ? QLatin1Char('{') : QLatin1Char('['));
                if (current.empty()) {
                    out.append(object ? QLatin1Char('}') : QLatin1Char(']'));
                    stack.pop_back();
                    continue;
                }
                out.append(QStringLiteral("<br>"));
            } else {
                if (frame.next < current.size()) out.append(QLatin1Char(','));
                out.append(QStringLiteral("<br>"));
            }
            if (frame.next == current.size()) {
                AppendIndent(out, static_cast<int>(stack.size() - 1));
                out.append(object ? QLatin1Char('}') : QLatin1Char(']'));
                stack.pop_back();
                continue;
            }
            AppendIndent(out, static_cast<int>(stack.size()));
            if (object) {
                out.append(QStringLiteral("<span style='color:"));
                out.append(key_color_hex);
                out.append(QStringLiteral("'>\""));
                out.append(QString::fromStdString(current.getKeys()[frame.next]).toHtmlEscaped());
                out.append(QStringLiteral("\"</span>: "));
            }
            const auto* child{&current.getValues()[frame.next++]};
            stack.push_back({child});
            continue;
        }
        switch (current.getType()) {
        case UniValue::VSTR:
            out.append(QLatin1Char('"'));
            out.append(QString::fromStdString(current.get_str()).toHtmlEscaped());
            out.append(QLatin1Char('"'));
            break;
        case UniValue::VNUM:
            out.append(QString::fromStdString(current.getValStr()));
            break;
        case UniValue::VBOOL:
            out.append(current.get_bool() ? QStringLiteral("true") : QStringLiteral("false"));
            break;
        case UniValue::VNULL:
            out.append(QStringLiteral("null"));
            break;
        case UniValue::VOBJ:
        case UniValue::VARR:
            break; // Containers are handled above.
        }
        stack.pop_back();
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
    AppendValue(out, v, key_color_hex);
    return out;
}

} // anonymous namespace

// All queued GUI notices are weak mailbox notifications. A reply is split on
// the worker, before any Qt model insertion or rich text layout on the GUI.
namespace {
using Rows = RpcOutputListModel::Batch;
std::shared_ptr<const Rows> SplitRows(const QString& time, int category, const QString& html, quint64 command_id = 0, quint64 generation = 0, const RpcCommandContext& context = {})
{
    auto rows = std::make_shared<Rows>();
    QString piece, span;
    int visible{0};
    bool continuation{false};
    const auto flush = [&](bool continued) {
        rows->append({rows->empty() ? time : QString{}, piece + (span.isEmpty() ? QString{} : QStringLiteral("</span>")), category, continuation, command_id, generation, context});
        piece = span; visible = 0; continuation = continued;
    };
    for (qsizetype i = 0; i < html.size();) {
        if (html.mid(i, 4) == QLatin1String("<br>")) { flush(false); i += 4; continue; }
        const bool surrogate = html[i].isHighSurrogate() && i + 1 < html.size() && html[i + 1].isLowSurrogate();
        if (visible + (surrogate ? 2 : 1) > RpcOutputListModel::kRowCharacters || piece.size() >= 6000) flush(true);
        if (html[i] == QLatin1Char('<')) {
            const auto end = html.indexOf(QLatin1Char('>'), i);
            if (end >= 0) {
                const auto tag = html.mid(i, end - i + 1);
                if (tag.startsWith(QLatin1String("<span"))) span = tag;
                else if (tag == QLatin1String("</span>")) span.clear();
                piece += tag; i = end + 1; continue;
            }
        }
        if (html[i] == QLatin1Char('&')) {
            const auto end = html.indexOf(QLatin1Char(';'), i);
            if (end >= i && end - i < 16) { piece += html.mid(i, end - i + 1); i = end + 1; ++visible; continue; }
        }
        piece += html[i++]; ++visible;
        if (i < html.size() && html[i - 1].isHighSurrogate() && html[i].isLowSurrogate()) { piece += html[i++]; ++visible; }
    }
    if (!piece.isEmpty() || rows->isEmpty()) flush(false);
    return rows;
}
QString BoundedText(const std::string& text)
{
    constexpr size_t limit{50'000};
    size_t count = std::min(text.size(), limit);
    if (count < text.size()) while (count && (static_cast<unsigned char>(text[count]) & 0xc0) == 0x80) --count;
    auto result = QString::fromUtf8(text.data(), int(count));
    if (count < text.size()) result += QObject::tr("\n[Output truncated. Use bitcoin-cli for the full result.]");
    return result;
}
}

struct RpcConsoleDrainState {
    std::atomic_bool retired{false};
    std::function<void()> notify, release_join;

    void finish()
    {
        retired = true;
        try { notify(); } catch (...) {}
        if (release_join) std::exchange(release_join, {})();
    }
};
struct RpcConsoleMailbox {
    struct Queues {
        explicit Queues(execution::Executor& executor)
            : rpc(executor.makeQueue(execution::Lane::Rpc)),
              priority(executor.makeQueue(execution::Lane::Priority)),
              outcomes(executor.makeQueue(execution::Lane::Utility)) {}
        execution::SerialQueue rpc, priority, outcomes;
    };

    explicit RpcConsoleMailbox(std::shared_ptr<RpcConsoleDrainState> value, std::shared_ptr<Queues> worker_queues,
                               std::shared_ptr<execution::ValueCleanupQueue> delivery)
        : drain(std::move(value)), queues(std::move(worker_queues)), outcome_delivery(std::move(delivery)) {}
    struct Event {
        std::shared_ptr<const Rows> rows;
        QString history;
        std::shared_ptr<const QStringList> commands;
        std::shared_ptr<const QStringList> suggestions;
        quint64 completion_generation{0};
        quint64 command_id{0}, generation{0};
        RpcCommandContext context{};
    };
    std::mutex mutex;
    std::deque<Event> events;
    std::vector<std::shared_ptr<const void>> retired;
    std::shared_ptr<RpcConsoleDrainState> drain;
    const std::shared_ptr<Queues> queues;
    const std::shared_ptr<execution::ValueCleanupQueue> outcome_delivery;
    ~RpcConsoleMailbox() {
        // Destroy retained payloads before acknowledging feature retirement.
        releaseRetired(); events.clear(); commands.reset();
        const auto workers = {&queues->rpc, &queues->priority, &queues->outcomes};
        const auto remaining = std::make_shared<std::atomic_size_t>(workers.size());
        for (auto* worker : workers) {
            worker->close();
            worker->whenDrained([remaining, drain = drain] {
                if (remaining->fetch_sub(1) == 1) drain->finish();
            });
        }
    }
    void releaseRetired() {
        std::vector<std::shared_ptr<const void>> values;
        { std::lock_guard lock{mutex}; values.swap(retired); }
    }
    std::atomic_size_t pending{0};
    std::shared_ptr<const QStringList> commands;
    bool closed{false};
    std::function<void()> notify;
    struct Pending {
        std::shared_ptr<RpcConsoleMailbox> owner;
        explicit Pending(std::shared_ptr<RpcConsoleMailbox> value) : owner(std::move(value)) { ++owner->pending; }
        ~Pending() { --owner->pending; try { owner->notify(); } catch (...) {} }
    };
    void publish(Event event) {
        {
            std::lock_guard lock{mutex};
            if (!closed) {
                if (event.completion_generation) {
                    const auto old = std::find_if(events.begin(), events.end(), [](const auto& value) { return value.completion_generation != 0; });
                    if (old == events.end()) events.push_back(std::move(event));
                    else if (old->completion_generation <= event.completion_generation) *old = std::move(event);
                } else events.push_back(std::move(event));
            }
        }
        notify();
    }
};

namespace {
template <typename Work, typename Done>
execution::SubmitResult SubmitConsole(const std::shared_ptr<RpcConsoleMailbox>& mailbox,
                                      Work work, Done done, std::function<void(std::exception_ptr)> failed,
                                      execution::Lane lane = execution::Lane::Rpc)
{
    using execution::Lane;
    const auto queues = mailbox->queues;

    auto packet = execution::MakeTask(std::move(work), std::move(done), std::move(failed));
    bool accepted;
    if (lane == Lane::Utility) {
        accepted = queues->outcomes.post([packet = std::move(packet)] {
            packet->execute();
            packet->deliver();
        });
    } else {
        auto& worker = lane == Lane::Priority ? queues->priority : queues->rpc;
        return mailbox->outcome_delivery->postWithOutcome(worker, std::move(packet));
    }
    return {{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
}
} // namespace

RpcConsoleModel::RpcConsoleModel(interfaces::Node& node, QObject* parent)
    : RpcConsoleModel(node, nullptr, {}, parent) {}

RpcConsoleModel::RpcConsoleModel(interfaces::Node& node, execution::Executor& executor, presentation::Post post, QObject* parent)
    : RpcConsoleModel(node, &executor, std::move(post), parent) {}

RpcConsoleModel::RpcConsoleModel(interfaces::Node& node, execution::Executor* executor, presentation::Post post, QObject* parent)
    : QObject(parent), m_node(node), m_drain_state(std::make_shared<RpcConsoleDrainState>())
{
    if (!executor) {
        m_owned_executor = std::make_shared<execution::Executor>(execution::Executor::Config{});
        executor = m_owned_executor.get();
    }
    const auto queues = std::make_shared<RpcConsoleMailbox::Queues>(*executor);
    auto cleanup = std::make_shared<execution::ValueCleanupQueue>(
        std::shared_ptr<execution::SerialQueue>{queues, &queues->outcomes});
    m_mailbox = execution::RetainValues<RpcConsoleMailbox>(
        [cleanup](auto retire) { return cleanup->hold(std::move(retire)); },
        [&] { return std::make_unique<RpcConsoleMailbox>(m_drain_state, queues, cleanup); });
    const QPointer<RpcConsoleModel> observer{this};
    m_drain_state->notify = [observer, post = presentation::ReliablePost(std::move(post))] {
        post([observer] { if (observer) observer->onResultReady(); });
    };
    if (m_mailbox) m_mailbox->notify = m_drain_state->notify;
    else {
        m_stopping = true; m_drain_state->retired = true;
        m_completion_error = tr("Console cleanup is unavailable.");
    }
    m_append_timer.setSingleShot(true);
    connect(&m_append_timer, &QTimer::timeout, this, &RpcConsoleModel::appendPendingRows);
}

RpcConsoleModel::~RpcConsoleModel()
{
    // Executor packets and their cleanup anchor outlive this GUI observer.
    // The Node owner retains its public interface through producer drain.
    beginShutdown();
}

void RpcConsoleModel::retireOutput(std::shared_ptr<const void> value)
{
    if (!value) return;
    const auto anchor = m_mailbox;
    { std::lock_guard lock{anchor->mutex}; anchor->retired.push_back(std::move(value)); }
    // Final cleanup owns the anchor if this optional eager release cannot
    // be admitted. Never put large captures in a rejected task.
    if (!m_stopping) anchor->queues->outcomes.post([anchor] { anchor->releaseRetired(); });
}

bool RpcConsoleModel::submitNodeCommand(const QString& command)
{
    return submitCommandForContext(command, RpcCommandContext::ForNode());
}

bool RpcConsoleModel::submitWalletCommand(const QString& command, const QString& wallet_name)
{
    return submitCommandForContext(command, RpcCommandContext::ForWallet(wallet_name));
}

bool RpcConsoleModel::submitInput(const QString& command, bool wallet_context,
                                  const QString& wallet_name, bool target_available)
{
    if (m_stopping) return false;
    // Widgets handles this independent lifecycle intent before wallet routing.
    if (command.size() <= 64 && command.trimmed() == QLatin1String("stop")) return submitNodeCommand(command);
    if (!target_available) {
        m_completion_error = tr("The selected wallet is not ready for RPC commands.");
        Q_EMIT completionChanged(); return false;
    }
    // #1131: a recalled wallet command that last ran in another wallet is
    // refused once, naming both, so it never silently moves to a new target.
    const auto target = wallet_context ? RpcCommandContext::ForWallet(wallet_name) : RpcCommandContext::ForNode();
    if (const auto recalled = std::exchange(m_recalled, std::nullopt);
        recalled && recalled->text == command && recalled->context.target == RpcCommandContext::Target::Wallet && recalled->context != target) {
        const auto name = [](const RpcCommandContext& context) {
            if (context.target == RpcCommandContext::Target::Node) return tr("the node, with no wallet");
            return context.wallet_name.isEmpty() ? tr("the default wallet") : tr("wallet \"%1\"").arg(context.wallet_name.left(64));
        };
        m_completion_error = tr("This command last ran in %1; the console now targets %2. Press Enter again to run it there.").arg(name(recalled->context), name(target));
        Q_EMIT completionChanged(); return false;
    }
    return wallet_context ? submitWalletCommand(command, wallet_name) : submitNodeCommand(command);
}

bool RpcConsoleModel::submitCommandForContext(const QString& command, const RpcCommandContext& context)
{
    if (m_stopping || command.isEmpty() || (command.size() <= 64 && command.trimmed().isEmpty())) return false;
    if (command.size() > kMaxInputCharacters || context.wallet_name.size() > 1024 ||
        m_next_command_id == std::numeric_limits<quint64>::max()) {
        m_completion_error = tr("The command exceeds the console input limit.");
        Q_EMIT completionChanged(); return false;
    }
    // Only bounded recognition of the independent stop intent occurs on GUI.
    const bool stop = command.size() <= 64 && command.trimmed() == QLatin1String("stop");
    bool backpressure{false};
    { std::lock_guard lock{m_mailbox->mutex}; backpressure = !stop && m_mailbox->retired.size() >= 8; }
    if (backpressure) { m_completion_error = tr("The console is still clearing. Try again."); Q_EMIT completionChanged(); return false; }
    if ((stop && m_stop_pending) || (!stop && m_executing)) return false;
    const auto mailbox = m_mailbox;
    const auto key_color = m_key_color.name();
    const auto node = &m_node;
    const bool context_changed = !m_last_command_context || *m_last_command_context != context;
    const auto command_id = ++m_next_command_id;
    const auto generation = m_output_generation;
    const auto receipt = std::make_shared<RpcCommandReceipt>([weak = std::weak_ptr<RpcConsoleMailbox>{mailbox}] { if (const auto owner = weak.lock()) owner->notify(); });
    const QPointer<RpcConsoleModel> observer{this};
    const auto pending = std::make_shared<RpcConsoleMailbox::Pending>(mailbox);
    const auto previous_command = m_command;
    const auto previous_id = m_command_id, previous_generation = m_command_generation;
    const auto previous_context = m_command_context;
    const auto previous_finished = m_command_finished;
    // Busy ingress is set before an injected admission can reenter the model.
    // The effect and its exact-once outcome remain in the accepted packet.
    if (stop) { m_stop_pending = true; m_stop_command = receipt; }
    else { m_command = receipt; m_command_id = command_id; m_command_generation = generation; m_command_context = context; m_executing = true; m_command_finished = false; }
    const auto submitted = receipt->startWith([&](auto done, auto failed) {
        return SubmitConsole(mailbox,
        [mailbox, pending, node, command, context, key_color, stop, context_changed, command_id, generation] {
            RpcCommandResult result_fact{command_id, generation, context};
            const auto time = QDateTime::currentDateTime().toString("hh:mm:ss");
            const auto publish = [&](int category, const QString& text, QString history = {}) {
                const auto html = category == CMD_REPLY ? FormatJsonReply(text.toStdString(), key_color) : EscapeAndConvertWhitespace(text);
                mailbox->publish({SplitRows(time, category, html, command_id, generation, context), std::move(history), {}, {}, 0, command_id, generation, context});
            };
            const auto trimmed = command.trimmed();
            const auto executable = trimmed.toStdString() + "\n";
            try {
                const auto prepared = RpcCommandExecutor::Prepare(executable);
                if (!prepared) {
                    publish(CMD_ERROR, tr("Parse error: unbalanced ' or \""));
                    return result_fact;
                }
                // Redaction and all request formatting happen before the effect,
                // on the worker. History retains a bounded, explicitly clipped
                // representation while the original RPC request stays intact.
                auto history = QString::fromStdString(prepared->filtered()).trimmed();
                if (history.size() > 4096) history = history.left(4096) + QStringLiteral(" ") + tr("[history truncated]");
                if (!stop && context_changed) {
                    QString description;
                    if (context.target == RpcCommandContext::Target::Node) {
                        description = tr("Executing command without any wallet");
                    } else if (context.wallet_name.isEmpty()) {
                        description = tr("Executing command using the default wallet");
                    } else {
                        description = tr("Executing command using \"%1\" wallet").arg(context.wallet_name.left(1024));
                    }
                    publish(CMD_REQUEST, description);
                }
                publish(CMD_REQUEST, history, stop ? QString{} : history);
                if (trimmed.compare(QLatin1String("help-console"), Qt::CaseInsensitive) == 0) {
                    publish(CMD_REPLY, tr("\n"
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
                    result_fact.outcome = RpcCommandResult::Outcome::Succeeded;
                    return result_fact;
                }
                const auto result = RpcCommandExecutor::Execute(*node, *prepared, context, result_fact.attempted);
                result_fact.outcome = RpcCommandResult::Outcome::Succeeded;
                publish(CMD_REPLY, BoundedText(result));
            } catch (UniValue& error) {
                result_fact.outcome = RpcCommandResult::Outcome::KnownError;
                try { publish(CMD_ERROR, BoundedText(error.find_value("message").get_str()) + QStringLiteral(" ") + tr("(code %1)").arg(error.find_value("code").getInt<int>())); }
                catch (const std::exception&) { publish(CMD_ERROR, BoundedText(error.write())); }
            } catch (const std::exception& error) {
                result_fact.outcome = result_fact.attempted ? RpcCommandResult::Outcome::Uncertain : RpcCommandResult::Outcome::NotStarted;
                publish(CMD_ERROR, tr("Error: %1").arg(BoundedText(error.what())));
                if (result_fact.attempted) publish(CMD_ERROR, tr("The command may not have finished. Check its effects before running it again."));
            }
            return result_fact;
        }, [pending, done = std::move(done)](RpcCommandResult result) mutable { done(std::move(result)); },
        [pending, failed = std::move(failed)](std::exception_ptr error) { failed(error); },
        stop ? execution::Lane::Priority : execution::Lane::Rpc);
    });
    if (!observer || m_stopping) return bool(submitted);
    if (!submitted) {
        if (stop) { m_stop_pending = false; m_stop_command.reset(); }
        else {
            m_executing = false; m_command = previous_command;
            m_command_id = previous_id; m_command_generation = previous_generation; m_command_finished = previous_finished;
            m_command_context = previous_context;
        }
        m_completion_error = tr("The command could not start. Try again.");
        Q_EMIT completionChanged(); return false;
    }
    if (!stop) {
        if (generation == m_output_generation) m_last_command_context = context;
        Q_EMIT executingChanged();
        if (!observer || m_stopping) return true;
    }
    if (!m_completion_error.isEmpty()) { m_completion_error.clear(); Q_EMIT completionChanged(); }
    return true;
}

QString RpcConsoleModel::browseHistory(int direction, const QString& currentText)
{
    if (m_stopping || m_history.isEmpty() || (direction != 1 && direction != -1)) return currentText.left(kMaxInputCharacters);

    // Save the text being composed the first time we enter history browsing.
    if (m_history_idx == -1) {
        m_pending_text = currentText.left(kMaxInputCharacters);
    }

    int newIdx = m_history_idx + direction;
    // Clamp: oldest entry is index 0 (size-1 from back), newest is -1.
    if (newIdx < 0) {
        // Past the newest end — restore pending text.
        m_history_idx = -1;
        m_recalled.reset();
        return m_pending_text;
    }
    if (newIdx >= m_history.size()) {
        newIdx = m_history.size() - 1;
    }
    m_history_idx = newIdx;
    // history is stored oldest-first; m_history_idx 0 = most recent, size-1 = oldest.
    const auto index = m_history.size() - 1 - m_history_idx;
    m_recalled = Recalled{m_history.at(index), m_history_contexts.at(index)};
    return m_history.at(index);
}

void RpcConsoleModel::resetHistoryNavigation()
{
    m_history_idx = -1;
    m_pending_text.clear();
    m_recalled.reset();
}

void RpcConsoleModel::clear()
{
    if (m_stopping) return;
    if (m_output_generation == std::numeric_limits<quint64>::max()) { close(); return; }
    ++m_output_generation;
    m_append_timer.stop();
    for (auto& pending : m_pending_rows) retireOutput(std::move(pending.rows));
    m_pending_rows.clear();
    if (m_last_command_context && m_last_command_context->target == RpcCommandContext::Target::Wallet) m_last_command_context.reset();
    resetHistoryNavigation(); // Clear retains only the redacted recall inventory.
    const QPointer<RpcConsoleModel> observer{this};
    const auto mailbox = m_mailbox;
    auto rows = m_output_model.releaseRows();
    if (!observer || m_stopping) {
        if (rows) { std::lock_guard lock{mailbox->mutex}; mailbox->retired.push_back(std::move(rows)); }
        return;
    }
    retireOutput(std::move(rows));
    Q_EMIT outputCleared();
    if (observer && !m_stopping && m_command_finished) setExecuting(false);
}

void RpcConsoleModel::setCompletionPrefix(const QString& prefix)
{
    const auto bounded = prefix.left(257);
    if (m_stopping || m_completion_prefix == bounded) return;
    m_completion_prefix = bounded;
    const auto generation = ++m_completion_generation;
    m_completion_suggestions.clear(); m_completion_error.clear();
    // RPC command identifiers are bounded metadata. A longer draft is an
    // argument expression and cannot match a command-name completion.
    if (prefix.isEmpty() || prefix.size() > 256) { m_completion_busy = false; Q_EMIT completionChanged(); return; }
    const auto mailbox = m_mailbox;
    const auto pending = std::make_shared<RpcConsoleMailbox::Pending>(mailbox);
    m_completion_busy = true;
    const QPointer<RpcConsoleModel> observer{this};
    const auto submitted = SubmitConsole(mailbox, [mailbox, pending, prefix = bounded] {
        std::shared_ptr<const QStringList> commands;
        { std::lock_guard lock{mailbox->mutex}; commands = mailbox->commands; }
        auto suggestions = std::make_shared<QStringList>();
        if (commands) for (const auto& command : *commands) {
            if (command.startsWith(prefix, Qt::CaseInsensitive)) suggestions->append(command);
            if (suggestions->size() == 10) break;
        }
        return std::shared_ptr<const QStringList>{std::move(suggestions)};
    }, [mailbox, pending, generation](std::shared_ptr<const QStringList> suggestions) {
        RpcConsoleMailbox::Event event; event.suggestions = std::move(suggestions); event.completion_generation = generation;
        mailbox->publish(std::move(event));
    }, [mailbox, pending, generation](std::exception_ptr) {
        RpcConsoleMailbox::Event event; event.completion_generation = generation;
        mailbox->publish(std::move(event));
    }, execution::Lane::Utility);
    if (!observer || m_stopping) return;
    if (!submitted) { m_completion_busy = false; m_completion_error = tr("Command suggestions are temporarily unavailable."); }
    Q_EMIT completionChanged();
}

void RpcConsoleModel::onNodeInitialized()
{
    if (m_stopping || m_metadata_pending) return;
    { std::lock_guard lock{m_mailbox->mutex}; if (m_mailbox->retired.size() >= 8) return; }
    m_metadata_pending = true;
    const QPointer<RpcConsoleModel> observer{this};
    const auto mailbox = m_mailbox;
    const auto pending = std::make_shared<RpcConsoleMailbox::Pending>(mailbox);
    const auto submitted = SubmitConsole(mailbox, [node = &m_node, pending] {
        auto list = std::make_shared<QStringList>();
        for (const auto& command : node->listRpcCommands()) {
            if (list->size() >= 10000 || command.size() > 256) throw std::runtime_error("RPC command metadata exceeds the console limit");
            const auto name = QString::fromStdString(command);
            list->append(name); list->append(QStringLiteral("help ") + name);
        }
        list->append(QStringLiteral("help-console"));
        list->sort(Qt::CaseInsensitive); list->removeDuplicates();
        return std::shared_ptr<const QStringList>{std::move(list)};
    }, [mailbox, pending](std::shared_ptr<const QStringList> commands) {
        { std::lock_guard lock{mailbox->mutex}; if (!mailbox->closed) mailbox->commands = commands; }
        mailbox->publish({{}, {}, std::move(commands), {}, 0});
    }, [mailbox, pending](std::exception_ptr error) {
        auto message = QObject::tr("RPC command discovery failed.");
        try { std::rethrow_exception(error); } catch (const std::exception& cause) { message += QLatin1Char(' ') + BoundedText(cause.what()); } catch (...) {}
        mailbox->publish({SplitRows({}, CMD_ERROR, EscapeAndConvertWhitespace(message)), {}, std::make_shared<const QStringList>(), {}, 0});
    });
    if (!observer || m_stopping) return;
    if (!submitted) { m_metadata_pending = false; m_completion_error = tr("The list of console commands could not be loaded."); Q_EMIT completionChanged(); }
}

void RpcConsoleModel::onResultReady()
{
    if (!m_mailbox) { finishDrain(); return; }
    std::deque<RpcConsoleMailbox::Event> events;
    { std::lock_guard lock{m_mailbox->mutex}; events.swap(m_mailbox->events); }
    bool commands_changed{false}, completion_changed{false};
    for (auto& event : events) {
        if (m_stopping) { retireOutput(std::move(event.rows)); retireOutput(std::move(event.commands)); continue; }
        if (!event.history.isEmpty()) {
            if (m_history.isEmpty() || m_history.last() != event.history || m_history_contexts.last() != event.context) {
                if (event.generation != m_output_generation && m_history_idx >= 0) ++m_history_idx;
                m_history.append(std::move(event.history));
                m_history_contexts.append(event.context);
            }
            if (m_history.size() > HISTORY_MAX) { m_history.removeFirst(); m_history_contexts.removeFirst(); }
            m_history_idx = std::min(m_history_idx, int(m_history.size()) - 1);
            if (event.generation == m_output_generation) { m_history_idx = -1; m_pending_text.clear(); }
        }
        if (event.commands) {
            m_metadata_pending = false;
            if (!event.commands->isEmpty()) {
                retireOutput(std::make_shared<const QStringList>(std::exchange(m_available_commands, *event.commands)));
                commands_changed = true;
            }
            retireOutput(std::move(event.commands));
        }
        if (event.completion_generation == m_completion_generation && event.completion_generation) {
            m_completion_busy = false;
            m_completion_error = event.suggestions ? QString{} : tr("Command suggestions are temporarily unavailable.");
            m_completion_suggestions = event.suggestions ? *event.suggestions : QStringList{};
            completion_changed = true;
        }
        if (event.rows) {
            if (!event.command_id || event.generation == m_output_generation) m_pending_rows.push_back({std::move(event.rows), 0});
            else retireOutput(std::move(event.rows));
        }
    }
    if (m_stop_command && m_stop_command->result()) { m_stop_pending = false; m_stop_command.reset(); }
    if (m_command && !m_command_finished) {
        if (const auto result = m_command->result()) {
            m_command_finished = true;
            if (result->error) {
                RpcCommandResult::Outcome outcome{RpcCommandResult::Outcome::Uncertain};
                try { std::rethrow_exception(result->error); }
                catch (const execution::NotStarted&) { outcome = RpcCommandResult::Outcome::NotStarted; }
                catch (...) {}
                const auto message = outcome == RpcCommandResult::Outcome::NotStarted
                    ? tr("The command did not start.") : tr("The command may not have finished. Check its effects before running it again.");
                if (m_output_generation == m_command_generation) m_pending_rows.push_back({SplitRows({}, CMD_ERROR, message, m_command_id, m_output_generation, m_command_context), 0});
            }
        }
    }
    // Transfer every large event to a retirement anchor/member before emitting
    // a Qt signal which can close or destroy the observing view.
    const QPointer<RpcConsoleModel> observer{this};
    if (commands_changed) {
        Q_EMIT availableCommandsChanged(); if (!observer || m_stopping) return;
        const auto prefix = m_completion_prefix; m_completion_prefix.clear(); setCompletionPrefix(prefix);
        if (!observer || m_stopping) return;
    }
    if (completion_changed) { Q_EMIT completionChanged(); if (!observer || m_stopping) return; }
    if (!m_stopping) appendPendingRows();
    if (observer) finishDrain();
}

void RpcConsoleModel::appendPendingRows()
{
    if (m_stopping) return;
    int budget{16};
    Rows staged;
    while (budget && !m_pending_rows.empty()) {
        auto& next = m_pending_rows.front();
        const int count = std::min(budget, int(next.rows->size()) - next.offset);
        for (int row = 0; row < count; ++row) staged.append(next.rows->at(next.offset + row));
        next.offset += count; budget -= count;
        if (next.offset == next.rows->size()) { retireOutput(std::move(next.rows)); m_pending_rows.pop_front(); }
    }
    const QPointer<RpcConsoleModel> observer{this};
    if (!staged.isEmpty()) m_output_model.appendRows(staged, 0, staged.size());
    if (!observer || m_stopping) return;
    if (!m_pending_rows.empty()) m_append_timer.start(0);
    else if (m_command_finished) setExecuting(false);
}

void RpcConsoleModel::setExecuting(bool executing)
{
    if (m_executing != executing) { m_executing = executing; Q_EMIT executingChanged(); }
}

void RpcConsoleModel::close()
{
    if (std::exchange(m_stopping, true)) return;
    m_append_timer.stop();
    { std::lock_guard lock{m_mailbox->mutex}; m_mailbox->closed = true;
      if (m_mailbox->commands) m_mailbox->retired.push_back(std::exchange(m_mailbox->commands, {})); }
    for (auto& pending : m_pending_rows) retireOutput(std::move(pending.rows));
    m_pending_rows.clear();
    retireOutput(m_output_model.releaseRows(false));
    retireOutput(std::make_shared<const QStringList>(std::exchange(m_available_commands, {})));
    retireOutput(std::make_shared<const QStringList>(std::exchange(m_history, {})));
    retireOutput(std::make_shared<const QList<RpcCommandContext>>(std::exchange(m_history_contexts, {})));
    m_recalled.reset();
    retireOutput(std::make_shared<const QString>(std::exchange(m_pending_text, {})));
    retireOutput(std::make_shared<const QString>(std::exchange(m_completion_prefix, {})));
    retireOutput(std::make_shared<const std::optional<RpcCommandContext>>(std::exchange(m_last_command_context, {})));
    // No large payload enters a queued GUI closure after this point.
    std::deque<RpcConsoleMailbox::Event> events;
    { std::lock_guard lock{m_mailbox->mutex}; events.swap(m_mailbox->events); }
    for (auto& event : events) { retireOutput(std::move(event.rows)); retireOutput(std::move(event.commands)); }
}

void RpcConsoleModel::beginShutdown()
{
    if (std::exchange(m_shutdown_started, true)) return;
    close();
    if (m_owned_executor) {
        m_join_pending = true;
        m_drain_state->release_join = JoinThreadAfterDrain(
            [executor = std::move(m_owned_executor)] { executor->stopAndJoin(); }, this, [this] {
                m_join_pending = false;
                finishDrain();
            });
    }
    m_mailbox.reset();
    finishDrain();
}

void RpcConsoleModel::finishDrain()
{
    if (!m_shutdown_started || m_drained || !m_drain_state->retired || m_join_pending) return;
    m_drained = true;
    Q_EMIT drained();
}
