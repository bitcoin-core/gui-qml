// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_SECRETINPUT_H
#define BITCOIN_QML_SECRETINPUT_H
#include <qml/execution.h>
#include <support/allocators/secure.h>
#include <QString>
#include <cstddef>
#include <exception>
#include <memory>
#include <utility>

namespace secretinput {
enum class Error { None, Unavailable, TransportBudget, Materialization };
class Failure final : public std::exception {
public:
    explicit Failure(Error error) : m_error(error) {}
    Error error() const noexcept { return m_error; }
    const char* what() const noexcept override { return "The secret input could not be materialized"; }
private:
    Error m_error;
};
// Immutable Qt-native editor handoff. All UTF8 allocation/conversion, cleansing
// and retirement run inside the original Native action, never on GUI.
class Input final {
public:
    Input(QString text, size_t bytes);
    size_t bytes() const { return m_bytes; }
    SecureString materialize() const;
private:
    const QString m_text;
    const size_t m_bytes;
};
using Captured = std::shared_ptr<const Input>;
struct CaptureResult {
    Captured input;
    Error error{Error::None};
    size_t bytes{0};
    explicit operator bool() const { return bool(input) && error == Error::None; }
};
// O(1) metadata admission. result.bytes is the retained secret's accounted size.
CaptureResult Capture(const std::shared_ptr<execution::Scheduler>& scheduler, const QString& text);
size_t Bytes(const Captured& input);
SecureString Materialize(const Captured& input);
}
#endif // BITCOIN_QML_SECRETINPUT_H
