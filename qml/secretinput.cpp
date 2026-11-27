// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/secretinput.h>
#include <bitcoin-build-config.h> // IWYU pragma: keep
#include <qml/retainedvalue.h>
#include <qml/walletopening.h>
#include <support/cleanse.h>
#include <QByteArray>
#include <limits>

namespace secretinput {
namespace {
template <typename Retain>
CaptureResult CaptureInput(const QString& text, Retain retain)
{
    // Peak conservative accounting: retained UTF16 plus temporary UTF8 and
    // secure UTF8 allocation. No scan of the original literal is necessary.
    constexpr size_t BYTES_PER_CODE_UNIT{8};
    constexpr size_t OVERHEAD{512};
    const auto units = static_cast<size_t>(text.size());
    if (units > (std::numeric_limits<size_t>::max() - OVERHEAD) / BYTES_PER_CODE_UNIT)
        return {{}, Error::TransportBudget};
    const size_t bytes = OVERHEAD + units * BYTES_PER_CODE_UNIT;
    std::shared_ptr<Input> retained;
    try {
        retained = retain([&text, bytes] { return std::make_unique<Input>(text, bytes); });
    } catch (...) { return {{}, Error::Unavailable, bytes}; }
    const auto error = retained ? Error::None : Error::Unavailable;
    return {std::move(retained), error, bytes};
}
} // namespace

Input::Input(QString text, size_t bytes) : m_text(std::move(text)), m_bytes(bytes) {}
CaptureResult Capture(const execution::ValueCleanup& hold, const QString& text)
{
    return CaptureInput(text, [&hold](auto make) { return execution::RetainValues<Input>(hold, make); });
}

#ifdef ENABLE_WALLET
CaptureResult Capture(const std::shared_ptr<walletopening::Session>& opening, const QString& text)
{
    return CaptureInput(text, [&opening](auto make) { return walletopening::RetainValues<Input>(opening, make); });
}
#endif

size_t Bytes(const Captured& input) { return input ? input->bytes() : 0; }
SecureString Materialize(const Captured& input)
{
    if (!input) throw Failure{Error::Unavailable};
    return input->materialize();
}
SecureString Input::materialize() const
{
    // Native-only boundary. Manual executors intentionally control this same
    // action in tests without relying on a physical GUI/thread identity check.
    QByteArray utf8;
    try {
        utf8 = m_text.toUtf8();
        SecureString secret;
        try {
            secret.assign(utf8.constData(), utf8.constData() + utf8.size());
        } catch (...) {
            if (!utf8.isEmpty()) memory_cleanse(utf8.data(), static_cast<size_t>(utf8.size()));
            throw;
        }
        if (!utf8.isEmpty()) memory_cleanse(utf8.data(), static_cast<size_t>(utf8.size()));
        return secret;
    } catch (...) {
        throw Failure{Error::Materialization};
    }
}
}
