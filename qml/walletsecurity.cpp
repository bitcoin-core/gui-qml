// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletsecurity.h>

#include <utility>

namespace walletsecurity {
std::optional<Error> Validate(Kind kind, bool current_empty, bool replacement_empty)
{
    if (kind == Kind::ChangePassphrase && current_empty) return Error::MissingCurrent;
    if (replacement_empty) return Error::MissingNew;
    return {};
}

Result Execute(interfaces::Wallet& wallet, Request request)
{
    SecureString confirmation;
    const bool confirmation_required = bool(request.captured_confirmation);
    try {
        if (request.captured_current) request.current = secretinput::Materialize(request.captured_current);
        if (request.captured_replacement) request.replacement = secretinput::Materialize(request.captured_replacement);
        if (request.captured_confirmation) confirmation = secretinput::Materialize(request.captured_confirmation);
        request.captured_current.reset(); request.captured_replacement.reset();
        request.captured_confirmation.reset();
    } catch (const secretinput::Failure&) { return {Error::Rejected}; }
    if (const auto error = Validate(request.kind, request.current.empty(), request.replacement.empty())) return {*error};
    if (confirmation_required && confirmation != request.replacement) return {Error::ConfirmationMismatch};
    confirmation.clear();
    if (wallet.privateKeysDisabled()) return {Error::NoPrivateKeys};
    Error error{Error::None};
    if (request.kind == Kind::Encrypt) {
        if (!wallet.encryptWallet(request.replacement)) error = Error::EncryptionFailed;
    } else {
        const auto result = wallet.changeWalletPassphrase(request.current, request.replacement);
        if (!result) error = result.error().code == wallet::WalletErrorCode::PassphraseIncorrect ? Error::IncorrectCurrent : Error::ChangeFailed;
    }
    // The secure allocator also cleanses capacity when this request dies. Do
    // not carry either secret into a result, notification or GUI transport.
    request.current.clear();
    request.replacement.clear();
    return {error, wallet.isCrypted(), wallet.isLocked(), true};
}

execution::SubmitResult Operation::start(Request request)
{
    {
        std::lock_guard lock{m_mutex};
        if (std::exchange(m_started, true) || request.kind != m_kind) return {{}, execution::Rejection::Invalid};
    }
    const auto self = shared_from_this();
    const auto submitted = m_opening->read([request = std::move(request)](interfaces::Wallet& wallet) mutable {
        return Execute(wallet, std::move(request));
    }, [self](Result result) { self->complete(std::move(result)); },
       [self](std::exception_ptr error) {
           try { std::rethrow_exception(error); }
           catch (const execution::NotStarted&) { self->complete({Error::Rejected}); }
           catch (...) { self->complete({Error::Uncertain}); }
       }, execution::WorkClass::Action);
    if (!submitted) complete({Error::Rejected});
    return submitted;
}

std::optional<Result> Operation::result() const
{
    std::lock_guard lock{m_mutex};
    return m_result;
}

void Operation::complete(Result result)
{
    { std::lock_guard lock{m_mutex}; if (m_result) return; m_result = std::move(result); }
    retryPublication();
}

void Operation::retryPublication()
{
    const auto lifetime = shared_from_this();
    { std::lock_guard lock{m_mutex}; if (!m_result) return; }
    try {
        m_notify();
        std::lock_guard lock{m_mutex}; m_result->publication_failed = false;
    } catch (...) {
        std::lock_guard lock{m_mutex}; m_result->publication_failed = true;
    }
}
} // namespace walletsecurity
