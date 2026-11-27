// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_FEEESTIMATEVALUES_H
#define BITCOIN_QML_MODELS_FEEESTIMATEVALUES_H
#include <qml/senddraft.h>
#include <qml/models/coinselection.h>
#include <wallet/coincontrol.h>
#include <util/fees.h>
#include <QHash>
#include <QString>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace interfaces { class Wallet; }
namespace feeestimate {
void ApplyAutomaticPolicy(interfaces::Wallet& wallet, wallet::CCoinControl& control);
enum class Provenance { Unknown, Estimated, Fallback, Required, Static, Manual };
Provenance ProvenanceFor(FeeReason reason);
enum class Error { None, Unavailable, Rejected, Backend, Closed, Invalid };
enum class Phase { Idle, Scheduled, Reading, ReadingWithPendingWork, Applying, Current, Failed, Stopping, Stopped };
struct Dependencies {
    quint64 draft{0}, intent{0}, coins{0}, policy{0}, backend{0}, requirements{0};
    bool operator==(const Dependencies&) const = default;
};
}
struct SendFeePreview {
    CAmount fee{0};
    int inputs{0};
    CAmount rate_per_kvb{0};
    std::optional<CAmount> maximum_amount;
    bool sweeps_wallet{false};
    feeestimate::Provenance provenance{feeestimate::Provenance::Unknown};
    int observed_target{0};
    bool operator==(const SendFeePreview&) const = default;
};
struct FeeReadInput {
    struct Recipient { std::string address; CAmount amount; };
    std::vector<Recipient> recipients;
    wallet::CCoinControl coin_control;
    coinselection::InputMode input_mode{coinselection::InputMode::Automatic};
    std::shared_ptr<const std::set<COutPoint>> selected;
    int remainder_index{-1};
    bool custom_fee_enabled{false};
    QString custom_fee_rate;
    std::shared_ptr<const senddraft::Snapshot> draft;
    quint64 maximum_token{0};
    feeestimate::Dependencies dependencies;
    std::optional<CAmount> custom_rate_per_kvb;
    bool eligible{true};
    bool scheduled{false};
};
struct FeeReadResult {
    QHash<unsigned int, SendFeePreview> estimates;
    std::optional<SendFeePreview> custom;
    QHash<unsigned int, QString> errors;
    QString custom_error;
    feeestimate::Error error{feeestimate::Error::None};
    QString message;
};
#endif // BITCOIN_QML_MODELS_FEEESTIMATEVALUES_H
