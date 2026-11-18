// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BACKUPPOLICYIO_H
#define BITCOIN_QML_MODELS_BACKUPPOLICYIO_H

#include <qml/models/backuppolicy.h>

#include <functional>
#include <mutex>
#include <string>
#include <vector>

class QIODevice;

namespace backup {
/** Shared by the application's wallets; no wallet mutex is held during consent. */
struct InstallCoordinator {
    std::mutex mutex;
    //! The wallet database files that no save may replace. Called on a worker.
    std::function<std::vector<std::string>()> wallet_files;
};
//! Refuses an existing destination that is one of the coordinator's wallet
//! files, compared by file identity so links and aliases are caught.
Proposal Screen(Proposal proposal, const InstallCoordinator& coordinator);
using Producer = std::function<bool(const std::string&)>;
struct FileVersion { std::string digest; Error error{Error::None}; };
FileVersion ReadVersion(QIODevice& device);
Proposal Inspect(const std::string& raw, const std::string& wallet_name);
Proposal InspectPng(const std::string& raw);
/** Inspect a captured file destination with a caller-owned final suffix. */
Proposal Inspect(const std::string& raw, const std::string& suggested_name, const std::string& suffix);
WriteResult Write(const Proposal& approved, bool destination_approved, const Producer& producer, InstallCoordinator& coordinator,
                  std::function<bool()> submitted = {});
} // namespace backup

#endif // BITCOIN_QML_MODELS_BACKUPPOLICYIO_H
