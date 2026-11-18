// Copyright (c) 2024-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinamount.h>
#include <qml/amountparse.h>

#include <qml/bitcoinunits.h>

#include <algorithm>
#include <cassert>

#include <QLocale>
#include <QPointer>

namespace {
int UnitDecimals(BitcoinAmount::Unit unit)
{
    switch (unit) {
    case BitcoinAmount::Unit::BTC: return 8;
    case BitcoinAmount::Unit::MilliBTC: return 5;
    case BitcoinAmount::Unit::MicroBTC: return 2;
    case BitcoinAmount::Unit::SAT: return 0;
    }
    assert(false);
}

QmlBitcoinUnits::Unit ToQmlUnit(BitcoinAmount::Unit unit)
{
    switch (unit) {
    case BitcoinAmount::Unit::BTC: return QmlBitcoinUnits::Unit::BTC;
    case BitcoinAmount::Unit::MilliBTC: return QmlBitcoinUnits::Unit::mBTC;
    case BitcoinAmount::Unit::MicroBTC: return QmlBitcoinUnits::Unit::uBTC;
    case BitcoinAmount::Unit::SAT: return QmlBitcoinUnits::Unit::SAT;
    }
    assert(false);
}
} // namespace

BitcoinAmount::BitcoinAmount(QObject* parent)
    : QObject(parent)
{
}

bool BitcoinAmount::notify(std::initializer_list<void (BitcoinAmount::*)()> emitted)
{
    const QPointer<BitcoinAmount> self{this};
    for (const auto signal : emitted) {
        (this->*signal)();
        if (!self) return false;
    }
    return true;
}

bool BitcoinAmount::announceEdit(Origin origin)
{
    return origin == Origin::Model || notify({&BitcoinAmount::valueEdited});
}

void BitcoinAmount::assignValue(qint64 value, bool is_set, bool reset_input)
{
    const bool changed = m_satoshi != value || m_isSet != is_set;
    // "sat" and "sats" differ, so the unit label changes at plus or minus one.
    const bool label_changes = m_unit == Unit::SAT
        && ((m_satoshi == 1 || m_satoshi == -1) != (value == 1 || value == -1));
    m_satoshi = value;
    m_isSet = is_set;
    // Listeners of amountChanged() read the input, so update it first.
    const QPointer<BitcoinAmount> self{this};
    if (reset_input) resetInput();
    if (!self || !changed) return;
    if (label_changes && !notify({&BitcoinAmount::unitChanged})) return;
    notify({&BitcoinAmount::amountChanged, &BitcoinAmount::displayChanged, &BitcoinAmount::displayWithUnitChanged});
}

void BitcoinAmount::setSatoshi(qint64 value, Origin origin)
{
    if (announceEdit(origin)) assignValue(value, true, /*reset_input=*/true);
}

void BitcoinAmount::clear(Origin origin)
{
    if (announceEdit(origin)) assignValue(0, false, /*reset_input=*/true);
}

void BitcoinAmount::setUnit(const Unit unit)
{
    if (unit < Unit::BTC || unit > Unit::SAT || m_unit == unit) return;
    m_unit = unit;
    if (inputAcceptable()) {
        m_input_text = formattedInput();
        if (!m_input_dirty) m_baseline_text = m_input_text;
    }
    notify({&BitcoinAmount::inputChanged, &BitcoinAmount::unitChanged,
            &BitcoinAmount::displayChanged, &BitcoinAmount::displayWithUnitChanged});
}

QString BitcoinAmount::unitLabel() const
{
    return QmlBitcoinUnits::label(ToQmlUnit(m_unit), m_satoshi);
}

void BitcoinAmount::flipUnit()
{
    setUnit(m_unit == Unit::SAT ? Unit::BTC : Unit::SAT);
}

QString BitcoinAmount::satsToBtcString(qint64 sat)
{
    return QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, sat, false, QmlBitcoinUnits::SeparatorStyle::NEVER);
}

QString BitcoinAmount::toDisplay() const
{
    if (!m_isSet) {
        return "";
    }
    return QmlBitcoinUnits::format(ToQmlUnit(m_unit), m_satoshi, false, QmlBitcoinUnits::SeparatorStyle::NEVER);
}

QString BitcoinAmount::displayWithUnit() const
{
    const QString display{toDisplay()};
    return display.isEmpty() ? QString{} : display + QStringLiteral(" ") + unitLabel();
}

QString BitcoinAmount::localizedDisplay() const
{
    if (!m_isSet) return {};
    return QmlBitcoinUnits::formatForDisplay(ToQmlUnit(m_unit), m_satoshi);
}

QString BitcoinAmount::localizedDisplayWithUnit() const
{
    if (!m_isSet) return {};
    const auto unit = ToQmlUnit(m_unit);
    return localizedDisplay() + QLatin1Char(' ')
        + QmlBitcoinUnits::label(unit, m_satoshi);
}

QString BitcoinAmount::formattedInput() const
{
    if (!m_isSet || (m_zero_is_empty && m_satoshi == 0)) return {};
    return toDisplay().replace(QLatin1Char('.'), QLocale().decimalPoint());
}

void BitcoinAmount::resetInput()
{
    const bool was_dirty = m_input_dirty;
    m_input_text = formattedInput();
    m_baseline_text = m_input_text;
    m_input_state = m_input_text.isEmpty() ? InputState::Empty : InputState::Valid;
    m_input_dirty = false;
    if (was_dirty && !notify({&BitcoinAmount::inputDirtyChanged})) return;
    notify({&BitcoinAmount::inputChanged});
}

void BitcoinAmount::setZeroIsEmpty(bool empty)
{
    if (m_zero_is_empty == empty) return;
    m_zero_is_empty = empty;
    if (!m_input_dirty) resetInput();
    else notify({&BitcoinAmount::inputChanged});
}

QString BitcoinAmount::inputError() const
{
    if (m_input_state == InputState::Incomplete) return tr("Complete the amount.");
    if (m_input_state != InputState::Invalid) return {};
    if (m_input_text.size() > MAX_INPUT_LENGTH) return tr("Amount input exceeds the 128-character limit.");
    return m_unit == Unit::SAT
        ? tr("Enter a whole number of sats, up to 2,100,000,000,000,000.")
        : tr("Enter a valid amount, up to 21,000,000 BTC.");
}

void BitcoinAmount::fromDisplay(const QString& text)
{
    if (!announceEdit(Origin::User)) return;
    const QPointer<BitcoinAmount> self{this};
    const auto parsed = amountinput::Parse(text, UnitDecimals(m_unit), QLocale().decimalPoint().at(0));
    static_assert(static_cast<int>(InputState::Empty) == static_cast<int>(amountinput::State::Empty));
    static_assert(static_cast<int>(InputState::Incomplete) == static_cast<int>(amountinput::State::Incomplete));
    static_assert(static_cast<int>(InputState::Invalid) == static_cast<int>(amountinput::State::Invalid));
    static_assert(static_cast<int>(InputState::Valid) == static_cast<int>(amountinput::State::Valid));
    m_input_state = static_cast<InputState>(parsed.state);
    // Keep one character past the limit so over-long input stays invalid
    // without retaining an unbounded string.
    m_input_text = text.left(MAX_INPUT_LENGTH + 1);
    const bool was_dirty = m_input_dirty;
    m_input_dirty = m_input_text != m_baseline_text;
    if (parsed.state == amountinput::State::Valid) assignValue(parsed.satoshi, true, /*reset_input=*/false);
    else if (parsed.state == amountinput::State::Empty) assignValue(0, false, /*reset_input=*/false);
    if (!self) return;
    if (was_dirty != m_input_dirty && !notify({&BitcoinAmount::inputDirtyChanged})) return;
    notify({&BitcoinAmount::inputChanged});
}

int BitcoinAmount::paste(const QString& text, int start, int end)
{
    if (start < 0 || end < start || end > m_input_text.size()) return -1;
    const QString replaced = m_input_text.left(start) + text + m_input_text.mid(end);
    const int cursor = std::min(start + int(text.size()), MAX_INPUT_LENGTH + 1);
    const QPointer<BitcoinAmount> self{this};
    fromDisplay(replaced);
    return self ? std::min(cursor, int(m_input_text.size())) : -1;
}

bool BitcoinAmount::commitTo(BitcoinAmount* target)
{
    if (!target || !inputAcceptable() || !MoneyRange(m_satoshi)) return false;
    const QPointer<BitcoinAmount> self{this}, receiver{target};
    if (m_input_state == InputState::Empty) target->clear();
    else target->setSatoshi(m_satoshi);
    if (!self || !receiver) return false;
    resetInput();
    return self && receiver;
}

void BitcoinAmount::format()
{
    if (!inputAcceptable()) return;
    const QPointer<BitcoinAmount> self{this};
    resetInput();
    if (self) notify({&BitcoinAmount::displayChanged, &BitcoinAmount::displayWithUnitChanged});
}
