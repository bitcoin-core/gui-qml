// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/transactionflow.h>

#include <consensus/validation.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <policy/policy.h>
#include <script/solver.h>
#include <util/rbf.h>

#include <algorithm>

#include <QByteArray>
#include <QStringList>

namespace {
QString Address(const CTxOut& output)
{
    CTxDestination destination;
    return ExtractDestination(output.scriptPubKey, destination)
        ? QString::fromStdString(EncodeDestination(destination)) : QString{};
}

QVariantMap DataOutput(const CScript& script)
{
    if (script.empty() || script.front() != OP_RETURN) return {};
    const QByteArray raw(reinterpret_cast<const char*>(script.data()), qsizetype(script.size()));
    QVariantMap result{{"dataType", "OP_RETURN"}, {"scriptHex", QString::fromLatin1(raw.toHex())}};
    QStringList hex_parts, text_parts;
    bool readable{true};
    auto it = script.begin() + 1;
    while (it != script.end()) {
        opcodetype opcode;
        std::vector<unsigned char> bytes;
        // Preserve the original script if it contains other opcodes or an
        // incomplete push. Such scripts are not necessarily text payloads.
        if (!script.GetOp(it, opcode, bytes) || opcode > OP_PUSHDATA4) return result;
        const QByteArray payload(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
        const QString text = QString::fromUtf8(payload);
        const auto codepoints = text.toUcs4();
        readable &= text.toUtf8() == payload && std::all_of(codepoints.begin(), codepoints.end(), [](auto c) {
            return QChar::isPrint(c) || c == '\n' || c == '\r' || c == '\t';
        });
        hex_parts.append(QString::fromLatin1(payload.toHex()));
        text_parts.append(text);
    }
    result.insert("dataHex", hex_parts.join(' '));
    if (readable) result.insert("dataText", text_parts.join('\n'));
    return result;
}
}

QVariantMap BuildTransactionFlow(const interfaces::WalletTx& wtx,
                               const std::vector<std::optional<CTxOut>>& prevouts, bool collapse_outputs)
{
    if (!wtx.tx || wtx.txin_is_mine.size() != wtx.tx->vin.size()
        || wtx.txout_is_mine.size() != wtx.tx->vout.size()
        || wtx.txout_is_change.size() != wtx.tx->vout.size()) return {};

    const auto& tx = *wtx.tx;
    QVariantList inputs, outputs;
    CAmount total_input{0}, total_output{0}, grouped_amount{0};
    const auto non_wallet_outputs = std::count(wtx.txout_is_mine.begin(), wtx.txout_is_mine.end(), false);
    const bool group_outputs = collapse_outputs && tx.vout.size() > 10 && non_wallet_outputs > 1;
    qsizetype group_index{-1};
    bool inputs_known{!tx.vin.empty()};
    for (size_t i{0}; i < tx.vout.size(); ++i) {
        const auto& output = tx.vout[i];
        if (!MoneyRange(output.nValue) || !MoneyRange(total_output + output.nValue)) return {};
        total_output += output.nValue;
        if (group_outputs && !wtx.txout_is_mine[i]) {
            grouped_amount += output.nValue;
            if (group_index < 0) {
                group_index = outputs.size();
                outputs.append(QVariantMap{});
            }
            continue;
        }
        QVariantMap entry{
            {"id", QStringLiteral("output:%1").arg(i)}, {"index", int(i)},
            {"kind", output.scriptPubKey.IsUnspendable() ? "data" : "output"},
            {"amountSat", qint64(output.nValue)}, {"amountKnown", true},
            {"address", Address(output)}, {"ownership", wtx.txout_is_mine[i] ? "wallet" : "external"},
            {"isChange", bool(wtx.txout_is_change[i])},
        };
        entry.insert(DataOutput(output.scriptPubKey));
        outputs.append(entry);
    }
    if (group_index >= 0) {
        outputs[group_index] = QVariantMap{{"id", "non-wallet-outputs"}, {"kind", "output-group"},
            {"outputCount", qint64(non_wallet_outputs)}, {"ownership", "external"},
            {"amountKnown", true}, {"amountSat", qint64(grouped_amount)}, {"address", ""}};
    }
    const bool coinbase = tx.IsCoinBase();
    if (coinbase) {
        // A coinbase has a synthetic source, not a spendable previous output.
        total_input = total_output;
        inputs.append(QVariantMap{{"id", "coinbase"}, {"index", -1}, {"kind", "coinbase"},
            {"amountSat", qint64(total_output)}, {"amountKnown", true}, {"ownership", "unknown"}, {"address", ""}});
    } else {
        for (size_t i{0}; i < tx.vin.size(); ++i) {
            const auto& input = tx.vin[i];
            const bool known = i < prevouts.size() && prevouts[i] && MoneyRange(prevouts[i]->nValue);
            if (known) {
                if (!MoneyRange(total_input + prevouts[i]->nValue)) return {};
                total_input += prevouts[i]->nValue;
            } else {
                inputs_known = false;
            }
            inputs.append(QVariantMap{
                {"id", QStringLiteral("input:%1").arg(i)}, {"index", int(i)}, {"kind", "input"},
                {"previousTxid", QString::fromStdString(input.prevout.hash.ToString())},
                {"previousOutputIndex", input.prevout.n},
                {"amountSat", known ? QVariant{qint64(prevouts[i]->nValue)} : QVariant{}},
                {"amountKnown", known}, {"address", known ? Address(*prevouts[i]) : QString{}},
                {"ownership", wtx.txin_is_mine[i] ? "wallet" : "external"},
            });
        }
    }
    std::optional<CAmount> fee;
    if (!coinbase) {
        const bool all_mine = !tx.vin.empty() && std::all_of(wtx.txin_is_mine.begin(), wtx.txin_is_mine.end(), [](bool mine) { return mine; });
        const std::optional<CAmount> debit = inputs_known ? std::optional{total_input}
            : all_mine && MoneyRange(wtx.debit) ? std::optional{wtx.debit} : std::nullopt;
        if (debit && *debit >= total_output) fee = *debit - total_output;
    }
    return {
        {"inputs", inputs}, {"outputs", outputs}, {"inputCount", int(tx.vin.size())}, {"outputCount", int(tx.vout.size())},
        {"totalInputKnown", inputs_known}, {"totalInputSat", inputs_known ? QVariant{qint64(total_input)} : QVariant{}},
        {"totalOutputSat", qint64(total_output)}, {"feeKnown", fee.has_value()},
        {"feeSat", fee ? QVariant{qint64(*fee)} : QVariant{}}, {"coinbase", coinbase},
        {"complete", inputs_known && (coinbase || fee.has_value())},
        {"virtualSize", qint64(GetVirtualTransactionSize(tx))}, {"signalsRbf", SignalsOptInRBF(tx)},
        {"size", qint64(GetSerializeSize(TX_WITH_WITNESS(tx)))}, {"weight", qint64(GetTransactionWeight(tx))},
        {"version", qint64(tx.version)}, {"lockTime", qint64(tx.nLockTime)},
    };
}
