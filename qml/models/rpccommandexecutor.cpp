// Copyright (c) 2011-2022 The Bitcoin Core developers
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

#include <QStringList>
#include <QUrl>

namespace {

// Commands whose arguments must be redacted in history / display output.
const QStringList HISTORY_FILTER = QStringList()
    << "signmessagewithprivkey"
    << "signrawtransactionwithkey"
    << "walletpassphrase"
    << "walletpassphrasechange"
    << "encryptwallet";

} // namespace

/**
 * Split a shell-like command line and optionally execute the command(s).
 *
 * Ported from RPCConsole::RPCParseCommandLine() in src/qt/rpcconsole.cpp.
 * See that file for a full description of the accepted syntax.
 */
bool RpcCommandExecutor::RPCParseCommandLine(interfaces::Node* node,
                                             std::string& strResult,
                                             const std::string& strCommand,
                                             const bool fExecute,
                                             std::string* const pstrFilteredOut,
                                             const QString& wallet_name)
{
    std::vector<std::vector<std::string>> stack;
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
    UniValue lastResult;
    unsigned nDepthInsideSensitive = 0;
    size_t filter_begin_pos = 0, chpos;
    std::vector<std::pair<size_t, size_t>> filter_ranges;

    auto add_to_current_stack = [&](const std::string& strArg) {
        if (stack.back().empty() && (!nDepthInsideSensitive) &&
            HISTORY_FILTER.contains(QString::fromStdString(strArg), Qt::CaseInsensitive)) {
            nDepthInsideSensitive = 1;
            filter_begin_pos = chpos;
        }
        if (stack.empty()) {
            stack.emplace_back();
        }
        stack.back().push_back(strArg);
    };

    auto close_out_params = [&]() {
        if (nDepthInsideSensitive) {
            if (!--nDepthInsideSensitive) {
                // filter_begin_pos must be non-zero whenever we are inside a
                // sensitive command scope; guard defensively instead of asserting.
                if (filter_begin_pos) {
                    filter_ranges.emplace_back(filter_begin_pos, chpos);
                    filter_begin_pos = 0;
                }
            }
        }
        stack.pop_back();
    };

    std::string strCommandTerminated = strCommand;
    if (strCommandTerminated.back() != '\n')
        strCommandTerminated += "\n";

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
                    if (curarg.size() && fExecute) {
                        UniValue subelement;
                        if (lastResult.isArray()) {
                            const auto parsed{ToIntegral<size_t>(curarg)};
                            if (!parsed) {
                                throw std::runtime_error("Invalid result query");
                            }
                            subelement = lastResult[parsed.value()];
                        } else if (lastResult.isObject()) {
                            subelement = lastResult.find_value(curarg);
                        } else {
                            throw std::runtime_error("Invalid result query");
                        }
                        lastResult = subelement;
                    }
                    state = STATE_COMMAND_EXECUTED;
                    break;
                }
                breakParsing = false;

                close_out_params();

                if (lastResult.isStr())
                    curarg = lastResult.get_str();
                else
                    curarg = lastResult.write(2);

                if (curarg.size()) {
                    if (stack.size())
                        add_to_current_stack(curarg);
                    else
                        strResult = curarg;
                }
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
                if ((ch == ')' || ch == '\n') && stack.size() > 0) {
                    if (fExecute) {
                        UniValue params = RPCConvertValues(
                            stack.back()[0],
                            std::vector<std::string>(stack.back().begin() + 1, stack.back().end()));
                        std::string method = stack.back()[0];
                        std::string uri;
                        if (!wallet_name.isEmpty()) {
                            QByteArray encodedName = QUrl::toPercentEncoding(wallet_name);
                            uri = "/wallet/" + std::string(encodedName.constData(), encodedName.length());
                        }
                        assert(node);
                        lastResult = node->executeRpc(method, params, uri);
                    }
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

    if (pstrFilteredOut) {
        if (STATE_COMMAND_EXECUTED == state) {
            assert(!stack.empty());
            close_out_params();
        }
        *pstrFilteredOut = strCommand;
        for (auto i = filter_ranges.rbegin(); i != filter_ranges.rend(); ++i) {
            pstrFilteredOut->replace(i->first, i->second - i->first, "(…)");
        }
    }

    switch (state) {
    case STATE_COMMAND_EXECUTED:
        if (lastResult.isStr())
            strResult = lastResult.get_str();
        else
            strResult = lastResult.write(2);
        [[fallthrough]];
    case STATE_ARGUMENT:
    case STATE_EATING_SPACES:
        return true;
    default:
        return false;
    }
}
