// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_ACTIVITYEXPORTDATA_H
#define BITCOIN_QML_MODELS_ACTIVITYEXPORTDATA_H

#include <qml/models/backuppolicy.h>
#include <qml/retainedvalue.h>
#include <QString>

#include <functional>
#include <memory>

class QSaveFile;
namespace backup { struct InstallCoordinator; }
namespace activityview { struct Hold; }
namespace activityexport {
struct Request {
    QString path; std::shared_ptr<const activityview::Hold> snapshot; int unit{0};
    // Native destination construction; the default is ordinary QSaveFile.
    std::function<std::unique_ptr<QSaveFile>(const QString&)> destination{};
};
struct Result { bool saved{false}; QString path, error; };
struct Receipt {
    QString operation, snapshot_revision, final_path;
    int unit{0};
    backup::Phase phase{backup::Phase::Queued};
    backup::Error error{backup::Error::None};
    bool present{false}, written{false}, outcome_known{true};
};
struct Files {
    using Producer = std::function<bool(const std::string&)>;
    std::function<backup::Proposal(const QString&)> inspect;
    std::function<backup::WriteResult(const backup::Proposal&, bool, const Producer&)> install;
    std::function<bool()> available;
    std::function<void(const Receipt&)> outcome{};
    std::shared_ptr<execution::SerialQueue> utility{};
    execution::ValueCleanup value_cleanup{};
};
//! Local CSV destinations, inspected and installed through the staged write policy.
Files LocalFiles(std::shared_ptr<backup::InstallCoordinator> coordinator);
} // namespace activityexport
#endif // BITCOIN_QML_MODELS_ACTIVITYEXPORTDATA_H
