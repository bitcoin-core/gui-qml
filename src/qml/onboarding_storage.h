// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_ONBOARDING_STORAGE_H
#define BITCOIN_QML_ONBOARDING_STORAGE_H

#include <QString>

#include <cstdint>

namespace QmlOnboardingStorage {

struct State {
    bool check_pending{false};
    bool result_valid{false};
    uint64_t available_bytes{0};
    QString error_text;
    int assumed_blockchain_size_gb{0};
    int assumed_chainstate_size_gb{0};
    bool prune{false};
    int prune_size_gb{0};
    bool existing_profile{false};
};

struct Info {
    QString status;
    int available_gb{0};
    QString available_text;
    QString warning_text;
    int minimum_required_gb{0};
    int full_required_gb{0};
    int pruned_required_gb{0};
    int selected_required_gb{0};
    bool enough_for_selected{false};
    bool enough_for_full{false};
};

Info Evaluate(const State& state);
bool ShouldRecommendPrune(const State& state);

} // namespace QmlOnboardingStorage

#endif // BITCOIN_QML_ONBOARDING_STORAGE_H
