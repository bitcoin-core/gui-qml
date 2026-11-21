// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_PAYMENTIMPORTREVIEW_H
#define BITCOIN_QML_PAYMENTIMPORTREVIEW_H
#include <qml/paymentimportports.h>
#include <qml/psbtwork.h>
#include <atomic>
namespace paymentimport {
struct ReviewServices {
    std::shared_ptr<execution::Scheduler> scheduler;
    // Immutable view/opening validity. This callback never accesses a QObject.
    std::function<bool()> current;
    using Done = std::function<void(psbtwork::ImportResult)>;
    std::function<execution::Admission(std::shared_ptr<const Inspection>, Done, TargetPort::Failed)> prepare;
    // Presentation handoff only; logical outcomes are retained before delivery.
    std::function<void(CommandKey, Child, std::shared_ptr<const psbtwork::ImportResult>, std::shared_ptr<const std::atomic_bool>)> present;
    uint64_t view{0};
};
std::shared_ptr<const TargetPort> WithReviewFactory(std::shared_ptr<const TargetPort> payment, ReviewServices services);
} // namespace paymentimport
#endif // BITCOIN_QML_PAYMENTIMPORTREVIEW_H
