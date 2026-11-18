// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETSECURITY_H
#define BITCOIN_QML_WALLETSECURITY_H

#include <qml/walletopening.h>
#include <qml/secretinput.h>

#include <support/allocators/secure.h>

#include <functional>
#include <mutex>
#include <optional>

namespace walletsecurity {
struct Identity {};
enum class Kind { Encrypt, ChangePassphrase };
enum class Error { None, MissingCurrent, MissingNew, NoPrivateKeys, EncryptionFailed, IncorrectCurrent, ChangeFailed, Uncertain, Rejected, ConfirmationMismatch };
struct Request {
    Kind kind;
    SecureString current, replacement;
    secretinput::Captured captured_current{}, captured_replacement{};
    secretinput::Captured captured_confirmation{};
};
struct Result {
    Error error{Error::None};
    bool encrypted{false}, locked{false}, state_known{false};
    bool publication_failed{false};
};
std::optional<Error> Validate(Kind kind, bool current_empty, bool replacement_empty);
Result Execute(interfaces::Wallet& wallet, Request request);

/** Owns a single security effect and its consumed outcome, independently of
 * GUI/view lifetime. Only opaque retained editor inputs cross the GUI boundary;
 * secure conversion and use stay Native, with no current-selection reference. */
class Operation : public std::enable_shared_from_this<Operation>
{
public:
    using Notify = std::function<void()>;
    Operation(std::shared_ptr<walletopening::Session> opening, Kind kind, Notify notify)
        : m_opening(std::move(opening)), m_kind(kind), m_notify(std::move(notify)) {}
    execution::Admission start(Request request);
    std::optional<Result> result() const;
    void retryPublication();
    Kind kind() const { return m_kind; }
private:
    void complete(Result result);
    const std::shared_ptr<walletopening::Session> m_opening;
    const Kind m_kind;
    const Notify m_notify;
    mutable std::mutex m_mutex;
    bool m_started{false};
    std::optional<Result> m_result;
};
} // namespace walletsecurity

#endif // BITCOIN_QML_WALLETSECURITY_H
