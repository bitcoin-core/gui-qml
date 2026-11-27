// Copyright (c) 2024-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BITCOINAMOUNT_H
#define BITCOIN_QML_BITCOINAMOUNT_H

#include <consensus/amount.h>

#include <QObject>
#include <QString>

#include <initializer_list>

/** One amount: its exact value in satoshis, the display unit, and the text
 * the user is editing. Input that does not parse keeps the previous value.
 *
 * valueEdited() is emitted before a user edit changes the value, so an owner
 * can record the edit as user intent before amountChanged() and inputChanged()
 * trigger validation. Values set by a model (setSatoshi with Origin::Model)
 * are not user edits and do not emit valueEdited().
 */
class BitcoinAmount : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Unit unit READ unit WRITE setUnit NOTIFY unitChanged)
    Q_PROPERTY(QString unitLabel READ unitLabel NOTIFY displayWithUnitChanged)
    Q_PROPERTY(QString display READ toDisplay WRITE fromDisplay NOTIFY displayChanged)
    Q_PROPERTY(QString displayWithUnit READ displayWithUnit NOTIFY displayWithUnitChanged)
    Q_PROPERTY(QString localizedDisplay READ localizedDisplay NOTIFY displayChanged)
    Q_PROPERTY(QString localizedDisplayWithUnit READ localizedDisplayWithUnit NOTIFY displayWithUnitChanged)
    Q_PROPERTY(QString inputText READ inputText NOTIFY inputChanged)
    Q_PROPERTY(InputState inputState READ inputState NOTIFY inputChanged)
    Q_PROPERTY(bool inputAcceptable READ inputAcceptable NOTIFY inputChanged)
    Q_PROPERTY(bool inputDirty READ inputDirty NOTIFY inputDirtyChanged)
    Q_PROPERTY(QString inputError READ inputError NOTIFY inputChanged)
    Q_PROPERTY(bool zeroIsEmpty READ zeroIsEmpty WRITE setZeroIsEmpty NOTIFY inputChanged)
    Q_PROPERTY(qint64 satoshi READ satoshi WRITE setSatoshi NOTIFY amountChanged)

public:
    // QML only exposes enum names that begin with an uppercase letter.
    enum class Unit {
        BTC,
        MilliBTC,
        MicroBTC,
        SAT
    };
    Q_ENUM(Unit)

    enum class InputState { Empty, Incomplete, Invalid, Valid };
    Q_ENUM(InputState)

    /** Whether a value comes from the user or from a model. */
    enum class Origin { User, Model };

    /** Longest input that is parsed. Longer input is invalid as a whole and
     * is never shortened to a valid prefix. */
    static constexpr int MAX_INPUT_LENGTH{128};

    explicit BitcoinAmount(QObject* parent = nullptr);

    Unit unit() const { return m_unit; }
    void setUnit(Unit unit);
    QString unitLabel() const;

    QString toDisplay() const;
    QString displayWithUnit() const;
    QString localizedDisplay() const;
    QString localizedDisplayWithUnit() const;
    void fromDisplay(const QString& text);
    qint64 satoshi() const { return m_satoshi; }
    void setSatoshi(qint64 value) { setSatoshi(value, Origin::User); }
    void setSatoshi(qint64 value, Origin origin);
    void clear(Origin origin);

    bool isSet() const { return m_isSet; }

    QString inputText() const { return m_input_text; }
    InputState inputState() const { return m_input_state; }
    bool inputAcceptable() const { return m_input_state == InputState::Empty || m_input_state == InputState::Valid; }
    bool inputDirty() const { return m_input_dirty; }
    QString inputError() const;
    bool zeroIsEmpty() const { return m_zero_is_empty; }
    void setZeroIsEmpty(bool empty);
    Q_INVOKABLE void editInput(const QString& text) { fromDisplay(text); }
    /** Replace the characters from start to end of the input with text, as a
     * user edit. Returns the cursor position after the pasted text, or -1 if
     * the range is not inside the current input. */
    Q_INVOKABLE int paste(const QString& text, int start, int end);
    Q_INVOKABLE bool commitTo(BitcoinAmount* target);
    Q_INVOKABLE void resetInput();
    Q_INVOKABLE void format();

    static QString satsToBtcString(qint64 sat);

public Q_SLOTS:
    void flipUnit();
    void clear() { clear(Origin::User); }

Q_SIGNALS:
    void valueEdited();
    void inputDirtyChanged();
    void inputChanged();
    void unitChanged();
    void amountChanged();
    void displayChanged();
    void displayWithUnitChanged();

private:
    /** Emit signals in order. Stops and returns false if a slot destroyed
     * this object. */
    bool notify(std::initializer_list<void (BitcoinAmount::*)()> emitted);
    /** Emit valueEdited() for a user edit. Returns false if a slot destroyed
     * this object. */
    bool announceEdit(Origin origin);
    void assignValue(qint64 value, bool is_set, bool reset_input);
    QString formattedInput() const;

    QString m_input_text;
    QString m_baseline_text;
    InputState m_input_state{InputState::Empty};
    bool m_input_dirty{false};
    bool m_zero_is_empty{false};
    qint64 m_satoshi{0};
    bool m_isSet{false};
    Unit m_unit{Unit::BTC};
};

#endif // BITCOIN_QML_BITCOINAMOUNT_H
