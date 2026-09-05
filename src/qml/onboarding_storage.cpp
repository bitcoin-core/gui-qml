// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/onboarding_storage.h>

#include <qml/guiconstants.h>

#include <QObject>

#include <algorithm>
#include <limits>

namespace {
constexpr int EXISTING_PROFILE_MINIMUM_REQUIRED_GB{1};

int BytesToGB(uint64_t bytes)
{
    return static_cast<int>(std::min<uint64_t>(bytes / GB_BYTES, std::numeric_limits<int>::max()));
}

uint64_t GBToBytes(int gb)
{
    if (gb <= 0) return 0;
    return static_cast<uint64_t>(gb) * GB_BYTES;
}
} // namespace

namespace QmlOnboardingStorage {

Info Evaluate(const State& state)
{
    Info info;
    info.full_required_gb = state.assumed_blockchain_size_gb + state.assumed_chainstate_size_gb;
    info.pruned_required_gb = state.prune_size_gb + state.assumed_chainstate_size_gb;
    info.minimum_required_gb = state.existing_profile
        ? EXISTING_PROFILE_MINIMUM_REQUIRED_GB
        : DEFAULT_PRUNE_TARGET_GB + state.assumed_chainstate_size_gb;
    const int selected_fresh_required_gb = state.prune && state.prune_size_gb <= state.assumed_blockchain_size_gb
        ? info.pruned_required_gb
        : info.full_required_gb;
    info.selected_required_gb = state.existing_profile ? info.minimum_required_gb : selected_fresh_required_gb;
    info.available_gb = BytesToGB(state.available_bytes);
    info.enough_for_selected = state.result_valid && state.available_bytes >= GBToBytes(info.selected_required_gb);
    info.enough_for_full = state.existing_profile
        ? info.enough_for_selected
        : state.result_valid && state.available_bytes >= GBToBytes(info.full_required_gb);

    if (state.check_pending) {
        info.status = QStringLiteral("checking");
        info.available_text = QObject::tr("Checking available storage...");
        return info;
    }

    if (!state.error_text.isEmpty()) {
        info.status = QStringLiteral("error");
        return info;
    }

    if (state.result_valid) {
        info.available_text = QObject::tr("%1GB available").arg(info.available_gb);
        if (state.available_bytes < GBToBytes(info.selected_required_gb)) {
            info.warning_text = QObject::tr("About %1GB is needed for the selected storage option.").arg(info.selected_required_gb);
        } else if (!state.existing_profile && info.available_gb - info.selected_required_gb < 10) {
            info.warning_text = QObject::tr("About %1GB is needed, so this disk is close to the recommended minimum.").arg(info.selected_required_gb);
        }
    }

    info.status = info.warning_text.isEmpty() ? QStringLiteral("ok") : QStringLiteral("warning");
    return info;
}

bool ShouldRecommendPrune(const State& state)
{
    if (!state.result_valid || !state.error_text.isEmpty()) return false;
    if (state.existing_profile) return false;
    const int full_required_gb = state.assumed_blockchain_size_gb + state.assumed_chainstate_size_gb;
    return state.available_bytes < GBToBytes(full_required_gb + 10);
}

} // namespace QmlOnboardingStorage
