// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/secretinput.h>
#include <qml/retainedvalue.h>
#include <support/cleanse.h>
#include <QByteArray>
#include <limits>

namespace secretinput {
Input::Input(QString text, size_t bytes) : m_text(std::move(text)), m_bytes(bytes) {}
CaptureResult Capture(const std::shared_ptr<execution::Scheduler>& scheduler, const QString& text)
{
    // Peak conservative accounting: retained UTF16 plus temporary UTF8 and
    // secure UTF8 allocation. No scan of the original literal is necessary.
    constexpr size_t BYTES_PER_CODE_UNIT{8};
    constexpr size_t OVERHEAD{512};
    const auto units = static_cast<size_t>(text.size());
    if (units > (std::numeric_limits<size_t>::max() - OVERHEAD) / BYTES_PER_CODE_UNIT)
        return {{}, Error::TransportBudget};
    const size_t bytes = OVERHEAD + units * BYTES_PER_CODE_UNIT;
    if (!scheduler) return {{}, Error::Unavailable, bytes};
    std::shared_ptr<Input> retained;
    try {
        retained = execution::RetainValues<Input>(scheduler, [&text, bytes] { return std::make_unique<Input>(text, bytes); });
    } catch (...) { return {{}, Error::Unavailable, bytes}; }
    const auto error = retained ? Error::None : Error::Unavailable;
    return {std::move(retained), error, bytes};
}
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
