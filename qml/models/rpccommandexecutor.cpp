// Copyright (c) 2011-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Portions of this file are ported from src/qt/rpcconsole.cpp.

#include <qml/models/rpccommandexecutor.h>

#include <interfaces/node.h>
#include <rpc/client.h>
#include <univalue.h>
#include <util/strencodings.h>

#include <cassert>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <optional>

#include <QStringList>
#include <QUrl>
#include <QCoreApplication>

namespace {

// Redact arguments before storing either request output or recall history.
// Keep the pinned Qt Widgets historyFilter policy, plus addhdkey's private key.
// The parity regression requires an explicit review when Core's policy changes.
const QStringList HISTORY_FILTER = QStringList()
    << "addhdkey"
    << "createwallet"
    << "createwalletdescriptor"
    << "migratewallet"
    << "signmessagewithprivkey"
    << "signrawtransactionwithkey"
    << "walletpassphrase"
    << "walletpassphrasechange"
    << "encryptwallet";

} // namespace

struct RpcCommandExecutor::Plan {
    struct Argument {
        std::string literal;
        std::optional<size_t> result;
        std::vector<std::string> queries;
    };
    std::vector<std::vector<Argument>> calls;
    Argument result;
    std::string filtered;
};

const std::string& RpcCommandExecutor::Prepared::filtered() const { return m_plan->filtered; }

std::optional<RpcCommandExecutor::Prepared> RpcCommandExecutor::Prepare(const std::string& strCommand)
{
    auto plan = std::make_shared<Plan>();
    std::vector<std::vector<Plan::Argument>> stack;
    stack.emplace_back();

    enum CmdParseState {
        STATE_EATING_SPACES,
        STATE_EATING_SPACES_IN_ARG,
        STATE_EATING_SPACES_IN_BRACKETS,
        STATE_ARGUMENT,
        STATE_SINGLEQUOTED,
        STATE_DOUBLEQUOTED,
        STATE_ESCAPE_OUTER,
        STATE_ESCAPE_DOUBLEQUOTED,
        STATE_COMMAND_EXECUTED,
        STATE_COMMAND_EXECUTED_INNER
    } state = STATE_EATING_SPACES;

    std::string curarg;
    Plan::Argument last_result;
    unsigned nDepthInsideSensitive = 0;
    size_t filter_begin_pos = 0, chpos;
    std::vector<std::pair<size_t, size_t>> filter_ranges;

    auto add_to_current_stack = [&](const std::string& strArg) {
        if (stack.empty()) throw std::runtime_error("Invalid Syntax");
        if (stack.back().empty() && (!nDepthInsideSensitive) &&
            HISTORY_FILTER.contains(QString::fromStdString(strArg), Qt::CaseInsensitive)) {
            nDepthInsideSensitive = 1;
            filter_begin_pos = chpos;
        }
        stack.back().push_back({strArg, {}, {}});
    };

    auto close_out_params = [&]() {
        if (nDepthInsideSensitive) {
            if (!--nDepthInsideSensitive) {
                // filter_begin_pos is set to chpos at the delimiter *after* the
                // sensitive command name, which is always > 0 (at minimum the
                // opening paren or space character follows the name).
                assert(filter_begin_pos);
                filter_ranges.emplace_back(filter_begin_pos, chpos);
                filter_begin_pos = 0;
            }
        }
        stack.pop_back();
    };

    std::string strCommandTerminated = strCommand;
    if (strCommandTerminated.empty() || strCommandTerminated.back() != '\n')
        strCommandTerminated += '\n';

    for (chpos = 0; chpos < strCommandTerminated.size(); ++chpos) {
        char ch = strCommandTerminated[chpos];
        switch (state) {
        case STATE_COMMAND_EXECUTED_INNER:
        case STATE_COMMAND_EXECUTED: {
            bool breakParsing = true;
            switch (ch) {
            case '[':
                curarg.clear();
                state = STATE_COMMAND_EXECUTED_INNER;
                break;
            default:
                if (state == STATE_COMMAND_EXECUTED_INNER) {
                    if (ch != ']') {
                        curarg += ch;
                        break;
                    }
                    if (!curarg.empty()) last_result.queries.push_back(curarg);
                    state = STATE_COMMAND_EXECUTED;
                    break;
                }
                breakParsing = false;

                close_out_params();

                if (!stack.empty()) stack.back().push_back(last_result);
                else plan->result = last_result;
                curarg.clear();
                state = STATE_EATING_SPACES;
            }
            if (breakParsing)
                break;
            [[fallthrough]];
        }
        case STATE_ARGUMENT:
        case STATE_EATING_SPACES_IN_ARG:
        case STATE_EATING_SPACES_IN_BRACKETS:
        case STATE_EATING_SPACES:
            switch (ch) {
            case '"':
                state = STATE_DOUBLEQUOTED;
                break;
            case '\'':
                state = STATE_SINGLEQUOTED;
                break;
            case '\\':
                state = STATE_ESCAPE_OUTER;
                break;
            case '(':
            case ')':
            case '\n':
                if (state == STATE_EATING_SPACES_IN_ARG)
                    throw std::runtime_error("Invalid Syntax");
                if (state == STATE_ARGUMENT) {
                    if (ch == '(' && stack.size() && stack.back().size() > 0) {
                        if (nDepthInsideSensitive) {
                            ++nDepthInsideSensitive;
                        }
                        stack.emplace_back();
                    }
                    if (!stack.size())
                        throw std::runtime_error("Invalid Syntax");
                    add_to_current_stack(curarg);
                    curarg.clear();
                    state = STATE_EATING_SPACES_IN_BRACKETS;
                }
                if ((ch == ')' || ch == '\n') && !stack.empty() && !stack.back().empty()) {
                    plan->calls.push_back(stack.back());
                    last_result = {{}, plan->calls.size() - 1, {}};
                    state = STATE_COMMAND_EXECUTED;
                    curarg.clear();
                }
                break;
            case ' ':
            case ',':
            case '\t':
                if (state == STATE_EATING_SPACES_IN_ARG && curarg.empty() && ch == ',')
                    throw std::runtime_error("Invalid Syntax");
                else if (state == STATE_ARGUMENT) {
                    add_to_current_stack(curarg);
                    curarg.clear();
                }
                if ((state == STATE_EATING_SPACES_IN_BRACKETS || state == STATE_ARGUMENT) && ch == ',') {
                    state = STATE_EATING_SPACES_IN_ARG;
                    break;
                }
                state = STATE_EATING_SPACES;
                break;
            default:
                curarg += ch;
                state = STATE_ARGUMENT;
            }
            break;
        case STATE_SINGLEQUOTED:
            switch (ch) {
            case '\'':
                state = STATE_ARGUMENT;
                break;
            default:
                curarg += ch;
            }
            break;
        case STATE_DOUBLEQUOTED:
            switch (ch) {
            case '"':
                state = STATE_ARGUMENT;
                break;
            case '\\':
                state = STATE_ESCAPE_DOUBLEQUOTED;
                break;
            default:
                curarg += ch;
            }
            break;
        case STATE_ESCAPE_OUTER:
            curarg += ch;
            state = STATE_ARGUMENT;
            break;
        case STATE_ESCAPE_DOUBLEQUOTED:
            if (ch != '"' && ch != '\\')
                curarg += '\\';
            curarg += ch;
            state = STATE_DOUBLEQUOTED;
            break;
        }
    }

    if (STATE_COMMAND_EXECUTED == state) {
        assert(!stack.empty());
        close_out_params();
        plan->result = last_result;
    }
    plan->filtered = strCommand;
    for (auto i = filter_ranges.rbegin(); i != filter_ranges.rend(); ++i)
        plan->filtered.replace(i->first, i->second - i->first, "(…)");

    switch (state) {
    case STATE_COMMAND_EXECUTED:
    case STATE_ARGUMENT:
    case STATE_EATING_SPACES:
        return Prepared{std::move(plan)};
    default:
        return {};
    }
}

std::string RpcCommandExecutor::Execute(interfaces::Node& node, const Prepared& command,
                                       const RpcCommandContext& context, bool& attempted)
{
    attempted = false;
    const auto& plan = *command.m_plan;
    std::vector<UniValue> results;
    results.reserve(plan.calls.size());
    const auto value = [&](const Plan::Argument& argument) {
        UniValue result{argument.literal};
        if (argument.result) {
            result = std::move(results.at(*argument.result));
            results.at(*argument.result).setNull();
        }
        for (const auto& query : argument.queries) {
            UniValue next;
            if (result.isArray()) {
                const auto index = ToIntegral<size_t>(query);
                if (!index) throw std::runtime_error("Invalid result query");
                next = result[*index];
            } else if (result.isObject()) next = result.find_value(query);
            else throw std::runtime_error("Invalid result query");
            result = std::move(next);
        }
        return result.isStr() ? result.get_str() : result.write(2);
    };
    std::string uri;
    if (context.target == RpcCommandContext::Target::Wallet) {
        const auto encoded = QUrl::toPercentEncoding(context.wallet_name);
        uri = "/wallet/" + std::string{encoded.constData(), size_t(encoded.size())};
    }
    for (const auto& call : plan.calls) {
        const auto method = value(call.front());
        std::vector<std::string> arguments;
        for (size_t i = 1; i < call.size(); ++i) {
            auto argument = value(call[i]);
            // Widgets omits a nested string result when that string is empty.
            if (!call[i].result || !argument.empty()) arguments.push_back(std::move(argument));
        }
        const auto params = [&] {
            try { return RPCConvertValues(method, arguments); }
            catch (const std::runtime_error&) {
                // Core's public JSON converter includes the rejected argument
                // in its exception. That is still original input, not an RPC
                // reply; the history redaction policy must cover it as well.
                if (HISTORY_FILTER.contains(QString::fromStdString(method), Qt::CaseInsensitive))
                    throw std::runtime_error(QCoreApplication::translate("RpcCommandExecutor", "Invalid RPC arguments.").toStdString());
                throw;
            }
        }();
        attempted = true;
        results.push_back(node.executeRpc(method, params, uri));
    }
    return plan.result.result ? value(plan.result) : std::string{};
}

bool RpcCommandExecutor::RPCParseCommandLine(interfaces::Node* node, std::string& result,
    const std::string& command, bool execute, std::string* filtered, const QString& wallet)
{
    return RPCParseCommandLine(node, result, command, execute, filtered,
        wallet.isEmpty() ? RpcCommandContext::ForNode() : RpcCommandContext::ForWallet(wallet));
}

bool RpcCommandExecutor::RPCParseCommandLine(interfaces::Node* node, std::string& result,
    const std::string& command, bool execute, std::string* filtered, const RpcCommandContext& context)
{
    const auto prepared = Prepare(command);
    if (!prepared) return false;
    if (filtered) *filtered = prepared->filtered();
    if (execute) {
        if (!node) throw std::invalid_argument("RPC execution requires a node");
        bool attempted{false};
        result = Execute(*node, *prepared, context, attempted);
    }
    return true;
}
