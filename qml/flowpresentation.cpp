// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/flowpresentation.h>
#include <consensus/amount.h>
#include <algorithm>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace {
bool Known(const QVariantMap& entry)
{
    return entry.value("amountKnown").toBool() && entry.value("amountSat").isValid() && !entry.value("amountSat").isNull()
        && MoneyRange(entry.value("amountSat").toLongLong());
}
std::optional<CAmount> Total(const QVariantList& entries, bool known_only = false)
{
    CAmount result{0};
    for (const auto& value : entries) {
        const auto entry = value.toMap();
        if (!Known(entry)) { if (known_only) continue; return {}; }
        const auto amount = entry.value("amountSat").toLongLong();
        if (!MoneyRange(result + amount)) return {};
        result += amount;
    }
    return result;
}
QVariantMap Group(const QVariantList& entries, const QString& id, bool input)
{
    QString ownership;
    qint64 count{0};
    for (const auto& value : entries) {
        const auto entry = value.toMap(); const auto owner = entry.value("ownership").toString();
        if (ownership.isEmpty()) ownership = owner; else if (ownership != owner) ownership = "unknown";
        count += entry.value(input ? "inputCount" : "outputCount", 1).toLongLong();
    }
    const auto total = Total(entries);
    return {{"id", id}, {"kind", input ? "input-group" : "output-group"}, {"ownership", ownership},
        {"amountKnown", total.has_value()}, {"amountSat", total ? QVariant{qint64(*total)} : QVariant{}},
        {input ? "inputCount" : "outputCount", count}, {"paymentRequests", QVariantList{}}};
}

QVariantList Limit(const QVariantList& full, int maximum, bool input)
{
    QVariantList result;
    if (full.size() <= maximum) { for (const auto& value : full) result.append(BoundedFlowEntry(value.toMap())); return result; }
    for (int i = 0; i < maximum - 1; ++i) result.append(BoundedFlowEntry(full[i].toMap()));
    QVariantList remaining; remaining.reserve(full.size() - maximum + 1);
    for (qsizetype i = maximum - 1; i < full.size(); ++i) remaining.append(full[i]);
    result.append(Group(remaining, input ? "remaining-inputs" : "remaining-outputs", input));
    return result;
}
std::vector<double> Widths(const QVariantList& entries, double scale, double width)
{
    std::vector<double> result(entries.size()); std::vector<std::pair<CAmount, qsizetype>> positive;
    double data_width{0};
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const auto entry = entries[i].toMap(); const auto amount = entry.value("amountSat").toLongLong();
        if (!Known(entry)) result[i] = entry.value("kind") == "fee" ? 2 : 4;
        else if (amount > 0) positive.emplace_back(amount, i);
        else if (entry.value("kind") == "data") { result[i] = 1; data_width += 1; }
    }
    std::sort(positive.begin(), positive.end());
    const auto known_total = Total(entries, true);
    if (!known_total) {
        // Aggregate validation failed. Keep neutral display widths while Facts
        // continues to report the amounts as unknown.
        for (const auto& entry : positive) result[entry.second] = 1.0;
        return result;
    }
    double amount = *known_total;
    double available = std::max<double>(positive.size(), scale > 0 ? amount / scale * width - data_width : 0);
    for (const auto& [value, index] : positive) {
        const auto allocated = std::max(1.0, value / amount * available);
        result[index] = allocated; amount -= value; available -= allocated;
    }
    return result;
}
QVariantMap Facts(const QVariantList& inputs, const QVariantList& outputs, bool complete)
{
    const auto input_total = Total(inputs), output_total = Total(outputs);
    const double scale = std::max(Total(inputs, true).value_or(0), Total(outputs, true).value_or(0));
    auto positive = [](const QVariantList& entries) {
        return std::count_if(entries.begin(), entries.end(), [](const QVariant& value) {
            const auto entry = value.toMap(); return Known(entry) && entry.value("amountSat").toLongLong() > 0;
        });
    };
    const double width = std::max<double>({64, double(positive(inputs)), double(positive(outputs))});
    auto left = Widths(inputs, scale, width), right = Widths(outputs, scale, width);
    std::vector<qsizetype> unknown; double known_width{0}, output_width{0}; QString ownership;
    for (qsizetype i = 0; i < inputs.size(); ++i) {
        const auto entry = inputs[i].toMap();
        if (Known(entry)) known_width += left[i]; else unknown.push_back(i);
        const auto owner = entry.value("ownership").toString();
        if (ownership.isEmpty()) ownership = owner; else if (ownership != owner) ownership = "unknown";
    }
    for (const auto value : right) output_width += value;
    const bool inferred = !unknown.empty() && output_width > known_width;
    if (inferred) for (const auto index : unknown) left[index] = (output_width - known_width) / unknown.size();
    QVariantList input_widths, output_widths;
    for (const auto value : left) input_widths.append(value);
    for (const auto value : right) output_widths.append(value);
    return {{"inputWidths", input_widths}, {"outputWidths", output_widths}, {"inferredInputs", inferred},
        {"proportional", complete && input_total && output_total && *input_total > 0 && *input_total == *output_total},
        {"amountsKnown", bool(input_total && output_total)}, {"junctionOwnership", ownership}, {"amountScale", scale}};
}
}

QVariantMap BoundedFlowEntry(QVariantMap entry)
{
    bool clipped{entry.value("textTruncated").toBool()};
    for (auto field = entry.begin(); field != entry.end(); ++field) if (field->metaType().id() == QMetaType::QString) {
        const auto text = field->toString();
        if (text.size() > 1024) {
            QString preview{text.left(1023)};
            preview.append(QChar{0x2026});
            *field = preview; clipped = true;
        }
    }
    const auto full = entry.value("paymentRequests").toList(); QVariantList requests;
    for (qsizetype i = 0; i < std::min<qsizetype>(4, full.size()); ++i) {
        auto request = full[i].toMap();
        for (auto field = request.begin(); field != request.end(); ++field)
            if (field->metaType().id() == QMetaType::QString) *field = field->toString().left(256);
        requests.append(request);
    }
    entry["paymentRequests"] = requests; entry["paymentRequestCount"] = full.size();
    entry["textTruncated"] = clipped; return entry;
}
QVariantList FlowEntryDocument(const QVariant& entry)
{
    QVariantList result; std::function<void(QString, const QVariant&)> flatten;
    flatten = [&](QString field, const QVariant& value) {
        if (value.metaType().id() == QMetaType::QVariantMap) {
            const auto map = value.toMap(); for (auto item = map.cbegin(); item != map.cend(); ++item)
                flatten(field.isEmpty() ? item.key() : field + QLatin1Char('.') + item.key(), item.value());
        } else if (value.metaType().id() == QMetaType::QVariantList) {
            const auto list = value.toList(); for (qsizetype i = 0; i < list.size(); ++i) flatten(field + QStringLiteral("[%1]").arg(i), list[i]);
        } else {
            const auto text = value.toString();
            for (qsizetype offset = 0; offset < std::max<qsizetype>(1, text.size()); offset += 1024)
                result.append(QVariantMap{{"field", field.left(256)}, {"content", text.mid(offset, 1024)}});
        }
    };
    flatten({}, entry); return result;
}

QVariantMap PrepareTransactionFlowPresentation(const QVariantMap& flow)
{
    const auto full_inputs = flow.value("inputs").toList(), full_outputs = flow.value("outputs").toList();
    const auto inputs = Limit(full_inputs, 16, true);
    QVariantList non_wallet;
    for (const auto& value : full_outputs) if (value.toMap().value("ownership") != "wallet") non_wallet.append(value);
    const bool collapse = flow.value("outputCount", full_outputs.size()).toLongLong() > 10 && non_wallet.size() > 1;
    QVariantList collapsed;
    bool inserted{false};
    for (const auto& value : full_outputs) {
        if (!collapse || value.toMap().value("ownership") == "wallet") collapsed.append(value);
        else if (!inserted) { collapsed.append(Group(non_wallet, "non-wallet-outputs", false)); inserted = true; }
    }
    auto expanded = Limit(full_outputs, 15, false); collapsed = Limit(collapsed, 15, false);
    if (!flow.value("coinbase").toBool() && !full_outputs.isEmpty()) {
        QVariantMap fee{{"id", "fee"}, {"kind", "fee"}, {"ownership", "fee"},
            {"amountKnown", flow.value("feeKnown")}, {"amountSat", flow.value("feeSat")}, {"paymentRequests", QVariantList{}}};
        // An unavailable observation may still carry its previous numeric value.
        // Publish no amount until both the status and value establish it as known.
        const bool fee_known = Known(fee);
        fee["amountKnown"] = fee_known;
        if (!fee_known) fee["amountSat"] = QVariant::fromValue(nullptr);
        expanded.append(fee); collapsed.append(fee);
    }
    return {{"inputs", inputs}, {"expandedOutputs", expanded}, {"collapsedOutputs", collapsed},
        {"canCollapseOutputs", collapse}, {"inputCount", flow.value("inputCount", full_inputs.size())},
        {"outputCount", flow.value("outputCount", full_outputs.size())},
        {"presentationLimited", full_inputs.size() > 16 || full_outputs.size() > 15},
        {"expandedFacts", Facts(inputs, expanded, flow.value("complete").toBool())},
        {"collapsedFacts", Facts(inputs, collapsed, flow.value("complete").toBool())}};
}
