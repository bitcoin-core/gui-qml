// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <QLocale>
#include <QScopeGuard>
#include <QPointer>
#include <memory>
#include <limits>

#include <qml/bitcoinamount.h>
#include <qml/amountparse.h>

class BitcoinAmountTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void amountIntentPrecedesMutation_data();
    void amountIntentPrecedesMutation();
    void exactMonetaryBoundaryIsIndependentOfDisplayUnit_data();
    void exactMonetaryBoundaryIsIndependentOfDisplayUnit();
    void signedPublicPresentation_data();
    void signedPublicPresentation();
    void overLongInputIsRejectedWhole_data();
    void overLongInputIsRejectedWhole();
    void pasteReplacesTheSelectedRange();
    void everyInputChangeIsAnnounced_data();
    void everyInputChangeIsAnnounced();
    void pasteSurvivesListenersThatDestroyOrEdit_data();
    void pasteSurvivesListenersThatDestroyOrEdit();
    void notificationCanDestroyTheAmount_data();
    void notificationCanDestroyTheAmount();
    void commitDoesNotAccessASourceDestroyedByTheTarget();
    void localizedInputKeepsExactSatoshis_data();
    void localizedInputKeepsExactSatoshis();
    void inputStatesGuardCommits();
    void canonicalAndUnitIntentsPreserveValue();
    void boundedInputAndDirtyNotifications();
    void satsToBtcString_basic();
    void satsToBtcString_negative();
    void btcToSats_roundtrip();
    void rejectMalformedInputWithoutChangingCanonicalValue();
    void display_flow_btc();
    void display_flow_mbtc_and_ubtc();
    void display_flow_sat();
    void displayWithUnit_formatsAmountAndUnit();
    void localizedDisplayWithUnit_usesLocaleAndKeepsCanonicalDisplay();
    void flipUnit_changesLabelAndDisplaySignal();
};

void BitcoinAmountTests::amountIntentPrecedesMutation_data()
{
    QTest::addColumn<int>("producer"); QTest::addColumn<int>("reentrant");
    for (int producer{0}; producer < 3; ++producer) for (int reentrant{0}; reentrant < 2; ++reentrant)
        QTest::newRow(qPrintable(QString::number(producer) + "-" + QString::number(reentrant))) << producer << reentrant;
}
void BitcoinAmountTests::amountIntentPrecedesMutation()
{
    QFETCH(int, producer); QFETCH(int, reentrant);
    auto amount = std::make_unique<BitcoinAmount>(); amount->setUnit(BitcoinAmount::Unit::SAT); amount->setSatoshi(3);
    bool observed{false}; CAmount before{-1};
    connect(amount.get(), &BitcoinAmount::valueEdited, this, [&] {
        if (observed) return;
        observed = true; before = amount->satoshi();
        if (reentrant == 0) amount.reset();
        if (reentrant == 1) amount->setSatoshi(7);
    });
    auto* original = amount.get();
    if (producer == 0) original->setSatoshi(3); // Equal canonical value is still user intent.
    if (producer == 1) original->editInput("3");
    if (producer == 2) original->clear();
    QVERIFY(observed); QCOMPARE(before, CAmount{3});
    if (reentrant == 0) QVERIFY(!amount);
    // A listener cannot cancel the user's edit by changing the value first.
    else QCOMPARE(amount->satoshi(), producer == 2 ? CAmount{0} : CAmount{3});
}
void BitcoinAmountTests::exactMonetaryBoundaryIsIndependentOfDisplayUnit_data()
{
    QTest::addColumn<int>("unit"); QTest::addColumn<QString>("text");
    QTest::newRow("btc") << 0 << "21000000.00000000";
    QTest::newRow("milli") << 1 << "21000000000.00000";
    QTest::newRow("micro") << 2 << "21000000000000.00";
    QTest::newRow("sat") << 3 << "2100000000000000";
}
void BitcoinAmountTests::exactMonetaryBoundaryIsIndependentOfDisplayUnit()
{
    QFETCH(int, unit); QFETCH(QString, text); BitcoinAmount amount;
    amount.setUnit(static_cast<BitcoinAmount::Unit>(unit)); amount.editInput(text);
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Valid); QCOMPARE(amount.satoshi(), MAX_MONEY);
    BitcoinAmount committed; QVERIFY(amount.commitTo(&committed)); QCOMPARE(committed.satoshi(), MAX_MONEY);
    amount.editInput(text + "1"); QVERIFY(!amount.inputAcceptable()); QVERIFY(!amount.commitTo(&committed));
    QCOMPARE(committed.satoshi(), MAX_MONEY);
}

void BitcoinAmountTests::signedPublicPresentation_data()
{
    QTest::addColumn<int>("unit");
    QTest::addColumn<qint64>("value");
    QTest::addColumn<QString>("display");
    const auto minimum = std::numeric_limits<qint64>::min();
    const auto maximum = std::numeric_limits<qint64>::max();
    QTest::newRow("btc-minimum") << 0 << minimum << QStringLiteral("-92233720368.54775808");
    QTest::newRow("btc-maximum") << 0 << maximum << QStringLiteral("92233720368.54775807");
    QTest::newRow("milli-minimum") << 1 << minimum << QStringLiteral("-92233720368547.75808");
    QTest::newRow("milli-maximum") << 1 << maximum << QStringLiteral("92233720368547.75807");
    QTest::newRow("micro-minimum") << 2 << minimum << QStringLiteral("-92233720368547758.08");
    QTest::newRow("micro-maximum") << 2 << maximum << QStringLiteral("92233720368547758.07");
    QTest::newRow("sat-minimum") << 3 << minimum << QStringLiteral("-9223372036854775808");
    QTest::newRow("sat-maximum") << 3 << maximum << QStringLiteral("9223372036854775807");
}

void BitcoinAmountTests::signedPublicPresentation()
{
    QFETCH(int, unit); QFETCH(qint64, value); QFETCH(QString, display);
    const QLocale previous;
    const auto restore = qScopeGuard([previous] { QLocale::setDefault(previous); });
    QLocale::setDefault(QLocale::c());
    BitcoinAmount amount, target;
    amount.setUnit(static_cast<BitcoinAmount::Unit>(unit));
    amount.setSatoshi(value);
    QCOMPARE(amount.satoshi(), value);
    QCOMPARE(amount.toDisplay(), display);
    QCOMPARE(amount.inputText(), display);
    QVERIFY(amount.isSet());
    amount.flipUnit();
    amount.setUnit(static_cast<BitcoinAmount::Unit>(unit));
    QCOMPARE(amount.satoshi(), value);
    QCOMPARE(amount.toDisplay(), display);

    // Exact signed presentation never grants an out-of-range amount authority
    // to commit into another editor, including after re-entering its text.
    target.setSatoshi(123);
    QVERIFY(!amount.commitTo(&target));
    QCOMPARE(target.satoshi(), qint64{123});
    amount.editInput(display);
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Invalid);
    QCOMPARE(amount.satoshi(), value);
    QVERIFY(!amount.commitTo(&target));
    QCOMPARE(target.satoshi(), qint64{123});
}

void BitcoinAmountTests::overLongInputIsRejectedWhole_data()
{
    QTest::addColumn<bool>("paste"); QTest::addColumn<QString>("literal"); QTest::addColumn<bool>("accepted");
    for (bool paste : {false, true}) {
        const auto prefix = paste ? QStringLiteral("paste-") : QStringLiteral("typed-");
        QTest::newRow(qPrintable(prefix + "boundary128")) << paste << (QStringLiteral("0.1") + QString(125, u' ')) << true;
        QTest::newRow(qPrintable(prefix + "boundary129")) << paste << (QStringLiteral("0.1") + QString(126, u' ')) << false;
        QTest::newRow(qPrintable(prefix + "hidden-numeric-tail")) << paste << (QStringLiteral("0.1") + QString(200000, u' ') + "9") << false;
        QTest::newRow(qPrintable(prefix + "oversized-leading-space")) << paste << (QString(200000, u' ') + "0.1") << false;
    }
}
void BitcoinAmountTests::overLongInputIsRejectedWhole()
{
    QFETCH(bool, paste); QFETCH(QString, literal); QFETCH(bool, accepted);
    BitcoinAmount amount, target;
    amount.setSatoshi(2 * COIN); target.setSatoshi(123);
    const auto before = amount.satoshi();
    QSignalSpy input{&amount, &BitcoinAmount::inputChanged};
    if (paste) QVERIFY(amount.paste(literal, 0, static_cast<int>(amount.inputText().size())) >= 0);
    else amount.editInput(literal);
    QCOMPARE(input.size(), 1);
    if (accepted) {
        QCOMPARE(amount.satoshi(), qint64{COIN / 10}); QVERIFY(amount.commitTo(&target));
        QCOMPARE(target.satoshi(), qint64{COIN / 10});
    } else {
        QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Invalid);
        QCOMPARE(amount.inputText().size(), qsizetype{BitcoinAmount::MAX_INPUT_LENGTH + 1});
        QCOMPARE(amount.inputError(), QStringLiteral("Amount input exceeds the 128-character limit."));
        QCOMPARE(amount.satoshi(), before);
        QVERIFY(!amount.commitTo(&target)); QCOMPARE(target.satoshi(), qint64{123});
        if (literal.startsWith("0.1")) {
            // The first 128 characters alone would parse as a valid amount.
            // The whole input must still be refused.
            QCOMPARE(amountinput::Parse(QStringView{literal}.left(128), 8, u'.').state, amountinput::State::Valid);
        }
    }
}
void BitcoinAmountTests::pasteReplacesTheSelectedRange()
{
    BitcoinAmount amount;
    amount.editInput("123");
    QCOMPARE(amount.paste("4", 1, 2), 2);
    QCOMPARE(amount.inputText(), QStringLiteral("143")); QCOMPARE(amount.satoshi(), qint64{143 * COIN});
    QCOMPARE(amount.paste("5", 3, 3), 4);
    QCOMPARE(amount.inputText(), QStringLiteral("1435"));
    // A range outside the current input changes nothing and is not an edit.
    QSignalSpy input{&amount, &BitcoinAmount::inputChanged};
    QSignalSpy edited{&amount, &BitcoinAmount::valueEdited};
    QCOMPARE(amount.paste("9", 0, 5), -1);
    QCOMPARE(amount.paste("9", 2, 1), -1);
    QCOMPARE(amount.paste("9", -1, 0), -1);
    QCOMPARE(amount.inputText(), QStringLiteral("1435")); QCOMPARE(amount.satoshi(), qint64{1435 * COIN});
    QCOMPARE(input.size(), 0); QCOMPARE(edited.size(), 0);
}
void BitcoinAmountTests::everyInputChangeIsAnnounced_data()
{
    QTest::addColumn<int>("change");
    QTest::newRow("keyboard-edit") << 0;
    QTest::newRow("unit") << 1;
    QTest::newRow("user-value") << 2;
    QTest::newRow("reset") << 3;
    QTest::newRow("clear") << 4;
    QTest::newRow("model-value") << 5;
    QTest::newRow("paste") << 6;
}
void BitcoinAmountTests::everyInputChangeIsAnnounced()
{
    // The amount field cancels a captured paste on inputChanged(), so the
    // paste cannot apply to text that has changed since it was captured.
    QFETCH(int, change);
    BitcoinAmount amount;
    amount.editInput("143");
    QSignalSpy input{&amount, &BitcoinAmount::inputChanged};
    if (change == 0) amount.editInput("12");
    if (change == 1) amount.setUnit(BitcoinAmount::Unit::SAT);
    if (change == 2) amount.setSatoshi(456);
    if (change == 3) amount.resetInput();
    if (change == 4) amount.clear();
    if (change == 5) amount.setSatoshi(456, BitcoinAmount::Origin::Model);
    if (change == 6) QCOMPARE(amount.paste("9", 0, 1), 1);
    QVERIFY(input.size() >= 1);
}
void BitcoinAmountTests::pasteSurvivesListenersThatDestroyOrEdit_data()
{
    QTest::addColumn<int>("action");
    QTest::newRow("destroy") << 0;
    QTest::newRow("newer-edit") << 1;
}
void BitcoinAmountTests::pasteSurvivesListenersThatDestroyOrEdit()
{
    QFETCH(int, action);
    auto amount = std::make_unique<BitcoinAmount>(); amount->editInput("1");
    const QPointer<BitcoinAmount> held{amount.get()};
    bool acted{false};
    if (action == 0) connect(amount.get(), &BitcoinAmount::inputChanged, this, [&] { amount.reset(); });
    if (action == 1) connect(amount.get(), &BitcoinAmount::amountChanged, this, [&] {
        if (acted) return;
        acted = true; amount->editInput("3");
    });
    const int cursor = amount->paste("2", 0, 1);
    if (action == 0) { QVERIFY(!held); QCOMPARE(cursor, -1); }
    else { QCOMPARE(amount->satoshi(), qint64{3 * COIN}); QCOMPARE(amount->inputText(), QStringLiteral("3")); }
}

void BitcoinAmountTests::notificationCanDestroyTheAmount_data()
{
    QTest::addColumn<int>("action"); QTest::addColumn<QByteArray>("signal");
    QTest::newRow("canonical-input") << 0 << QByteArray{SIGNAL(inputChanged())};
    QTest::newRow("canonical-amount") << 0 << QByteArray{SIGNAL(amountChanged())};
    QTest::newRow("canonical-display") << 0 << QByteArray{SIGNAL(displayChanged())};
    QTest::newRow("clear-input") << 1 << QByteArray{SIGNAL(inputChanged())};
    QTest::newRow("clear-amount") << 1 << QByteArray{SIGNAL(amountChanged())};
    QTest::newRow("unit-input") << 2 << QByteArray{SIGNAL(inputChanged())};
    QTest::newRow("unit-unit") << 2 << QByteArray{SIGNAL(unitChanged())};
    QTest::newRow("text-amount") << 3 << QByteArray{SIGNAL(amountChanged())};
    QTest::newRow("reset-dirty") << 4 << QByteArray{SIGNAL(inputDirtyChanged())};
    QTest::newRow("format-input") << 5 << QByteArray{SIGNAL(inputChanged())};
}

void BitcoinAmountTests::notificationCanDestroyTheAmount()
{
    QFETCH(int, action); QFETCH(QByteArray, signal);
    auto* amount = new BitcoinAmount;
    amount->setUnit(BitcoinAmount::Unit::SAT); amount->setSatoshi(1); amount->editInput("2");
    const QPointer<BitcoinAmount> held{amount};
    // QObject deletion is direct; deleteLater would not exercise continuation
    // after a signal's synchronous receiver destroys its sender.
    // A separate observer deletes the sender synchronously at the selected edge.
    auto destroy = [amount] { delete amount; };
    if (signal == SIGNAL(inputChanged())) connect(amount, &BitcoinAmount::inputChanged, destroy);
    else if (signal == SIGNAL(amountChanged())) connect(amount, &BitcoinAmount::amountChanged, destroy);
    else if (signal == SIGNAL(displayChanged())) connect(amount, &BitcoinAmount::displayChanged, destroy);
    else if (signal == SIGNAL(unitChanged())) connect(amount, &BitcoinAmount::unitChanged, destroy);
    else connect(amount, &BitcoinAmount::inputDirtyChanged, destroy);
    switch (action) {
    case 0: amount->setSatoshi(3); break;
    case 1: amount->clear(); break;
    case 2: amount->setUnit(BitcoinAmount::Unit::BTC); break;
    case 3: amount->editInput("3"); break;
    case 4: amount->resetInput(); break;
    case 5: amount->format(); break;
    }
    QVERIFY(!held);
}

void BitcoinAmountTests::commitDoesNotAccessASourceDestroyedByTheTarget()
{
    auto* source = new BitcoinAmount; source->setSatoshi(12);
    const QPointer<BitcoinAmount> held{source}; BitcoinAmount target;
    connect(&target, &BitcoinAmount::amountChanged, [source] { delete source; });
    QVERIFY(!source->commitTo(&target)); QVERIFY(!held);
    QCOMPARE(target.satoshi(), qint64{12});
}

void BitcoinAmountTests::localizedInputKeepsExactSatoshis_data()
{
    QTest::addColumn<QString>("locale"); QTest::addColumn<QString>("text");
    QTest::newRow("german-comma") << "de_DE" << "0,02";
    QTest::newRow("french-comma") << "fr_FR" << "0,02";
    QTest::newRow("german-dot-control") << "de_DE" << "0.02";
    QTest::newRow("english-dot-control") << "en_US" << "0.02";
}

void BitcoinAmountTests::localizedInputKeepsExactSatoshis()
{
    QFETCH(QString, locale); QFETCH(QString, text);
    const QLocale previous;
    const auto restore = qScopeGuard([previous] { QLocale::setDefault(previous); });
    QLocale::setDefault(QLocale(locale));
    BitcoinAmount amount;
    amount.fromDisplay(text);
    QCOMPARE(amount.satoshi(), qint64{2'000'000});
}

void BitcoinAmountTests::inputStatesGuardCommits()
{
    const QLocale previous;
    const auto restore = qScopeGuard([previous] { QLocale::setDefault(previous); });
    QLocale::setDefault(QLocale::c());
    BitcoinAmount amount, target;
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Empty);
    QVERIFY(amount.inputAcceptable()); QVERIFY(amount.inputError().isEmpty());
    amount.setSatoshi(2'000'000); target.setSatoshi(1234);
    amount.editInput("1.");
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Incomplete);
    QCOMPARE(amount.satoshi(), qint64{2'000'000});
    QVERIFY(!amount.inputAcceptable()); QVERIFY(!amount.inputError().isEmpty());
    QVERIFY(!amount.commitTo(&target)); QCOMPARE(target.satoshi(), qint64{1234});
    amount.format(); QCOMPARE(amount.inputText(), QString("1."));
    amount.editInput("1.2.3");
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Invalid);
    amount.setUnit(BitcoinAmount::Unit::SAT);
    QCOMPARE(amount.inputText(), QString("1.2.3"));
    QVERIFY(amount.inputError().contains("sats"));
    amount.setUnit(BitcoinAmount::Unit::BTC);
    QVERIFY(amount.inputError().contains("BTC"));
    QVERIFY(!amount.commitTo(&target));
    amount.setSatoshi(-1); QVERIFY(!amount.commitTo(&target));
    amount.setSatoshi(MAX_MONEY + 1); QVERIFY(!amount.commitTo(&target));
    amount.editInput("0.02"); QVERIFY(amount.inputAcceptable());
    QVERIFY(!amount.commitTo(nullptr));
    QVERIFY(amount.commitTo(&target));
    QCOMPARE(target.satoshi(), qint64{2'000'000});
    QVERIFY(!amount.inputDirty()); QCOMPARE(amount.inputText(), QString("0.02000000"));
    amount.editInput("0.03"); QVERIFY(amount.commitTo(&amount));
    QCOMPARE(amount.satoshi(), qint64{3'000'000}); QVERIFY(!amount.inputDirty());
    amount.editInput(""); QVERIFY(amount.inputAcceptable()); QVERIFY(amount.inputDirty());
    QVERIFY(amount.commitTo(&target)); QVERIFY(!target.isSet()); QCOMPARE(target.satoshi(), qint64{0});
    amount.editInput("junk"); amount.clear();
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Empty);
    QVERIFY(amount.inputText().isEmpty()); QVERIFY(!amount.inputDirty());
}

void BitcoinAmountTests::canonicalAndUnitIntentsPreserveValue()
{
    BitcoinAmount amount;
    amount.setSatoshi(2'000'000);
    amount.editInput("12"); QVERIFY(amount.inputDirty());
    amount.setUnit(BitcoinAmount::Unit::SAT);
    QCOMPARE(amount.satoshi(), qint64{1'200'000'000});
    QCOMPARE(amount.inputText(), QString("1200000000")); QVERIFY(amount.inputDirty());
    amount.format(); QVERIFY(!amount.inputDirty());
    amount.flipUnit(); QCOMPARE(amount.satoshi(), qint64{1'200'000'000});
    amount.setUnit(BitcoinAmount::Unit::BTC);
    amount.setUnit(static_cast<BitcoinAmount::Unit>(-1));
    amount.setUnit(static_cast<BitcoinAmount::Unit>(4));
    QCOMPARE(amount.unit(), BitcoinAmount::Unit::BTC);
    amount.setZeroIsEmpty(true); amount.setSatoshi(0);
    QVERIFY(amount.zeroIsEmpty());
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Empty);
    QVERIFY(amount.inputText().isEmpty());
    amount.setZeroIsEmpty(false); QVERIFY(!amount.inputText().isEmpty());
    QVERIFY(!amount.zeroIsEmpty());
    amount.editInput("2"); amount.setZeroIsEmpty(true);
    QCOMPARE(amount.inputText(), QString("2")); QVERIFY(amount.inputDirty());
    amount.setZeroIsEmpty(false); QCOMPARE(amount.inputText(), QString("2"));
    amount.setZeroIsEmpty(false);
    amount.editInput("oops"); amount.setSatoshi(amount.satoshi());
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Valid); QVERIFY(!amount.inputDirty());
    amount.setUnit(BitcoinAmount::Unit::SAT); amount.setSatoshi(1); amount.setSatoshi(-1);
    QCOMPARE(amount.unitLabel(), QString("sat"));
    amount.setSatoshi(2); QCOMPARE(amount.unitLabel(), QString("sats"));
}

void BitcoinAmountTests::boundedInputAndDirtyNotifications()
{
    BitcoinAmount amount;
    QSignalSpy dirty{&amount, &BitcoinAmount::inputDirtyChanged};
    QSignalSpy input{&amount, &BitcoinAmount::inputChanged};
    amount.setSatoshi(2'000'000); amount.resetInput();
    QCOMPARE(dirty.count(), 0);
    amount.editInput("0.02"); QCOMPARE(dirty.count(), 1);
    amount.editInput("0.03"); QCOMPARE(dirty.count(), 1);
    amount.format(); QCOMPARE(dirty.count(), 2);
    amount.resetInput(); amount.setSatoshi(amount.satoshi()); QCOMPARE(dirty.count(), 2);
    amount.editInput(QString(1000, u'9'));
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Invalid);
    QCOMPARE(amount.inputText().size(), qsizetype{BitcoinAmount::MAX_INPUT_LENGTH + 1});
    QCOMPARE(amount.satoshi(), qint64{3'000'000});
    amount.format(); QVERIFY(!amount.inputAcceptable());
    amount.clear(); amount.clear(); QVERIFY(amount.inputText().isEmpty());
}

void BitcoinAmountTests::satsToBtcString_basic()
{
    QCOMPARE(BitcoinAmount::satsToBtcString(0), QString("0.00000000"));
    QCOMPARE(BitcoinAmount::satsToBtcString(1), QString("0.00000001"));
    QCOMPARE(BitcoinAmount::satsToBtcString(COIN), QString("1.00000000"));
    QCOMPARE(BitcoinAmount::satsToBtcString(21 * COIN), QString("21.00000000"));
    QCOMPARE(BitcoinAmount::satsToBtcString(100000123), QString("1.00000123"));
    QCOMPARE(BitcoinAmount::satsToBtcString(std::numeric_limits<qint64>::max()), QString("92233720368.54775807"));
}

void BitcoinAmountTests::satsToBtcString_negative()
{
    QCOMPARE(BitcoinAmount::satsToBtcString(-1), QString("-0.00000001"));
    QCOMPARE(BitcoinAmount::satsToBtcString(std::numeric_limits<qint64>::min()), QString("-92233720368.54775808"));
    QCOMPARE(BitcoinAmount::satsToBtcString(std::numeric_limits<qint64>::min() + 1), QString("-92233720368.54775807"));
}

void BitcoinAmountTests::btcToSats_roundtrip()
{
    BitcoinAmount amt;
    amt.setUnit(BitcoinAmount::Unit::BTC);
    amt.fromDisplay("1.23456789");
    QCOMPARE(amt.toDisplay(), QString("1.23456789"));
}

void BitcoinAmountTests::rejectMalformedInputWithoutChangingCanonicalValue()
{
    BitcoinAmount amt;
    amt.setUnit(BitcoinAmount::Unit::BTC);
    amt.fromDisplay("1.2");
    amt.fromDisplay("abc1.2.3xyz");
    QCOMPARE(amt.toDisplay(), QString("1.20000000"));
    QCOMPARE(amt.inputState(), BitcoinAmount::InputState::Invalid);
    QVERIFY(!amt.inputAcceptable());
    amt.format();
    QCOMPARE(amt.inputText(), QString("abc1.2.3xyz"));
}

void BitcoinAmountTests::display_flow_btc()
{
    BitcoinAmount amt;
    amt.setUnit(BitcoinAmount::Unit::BTC);
    amt.setSatoshi(2 * COIN);
    QCOMPARE(amt.toDisplay(), QString("2.00000000"));
}

void BitcoinAmountTests::display_flow_mbtc_and_ubtc()
{
    BitcoinAmount amt;
    amt.setUnit(BitcoinAmount::Unit::MilliBTC);
    amt.setSatoshi(COIN);
    QCOMPARE(amt.toDisplay(), QString("1000.00000"));
    QCOMPARE(amt.unitLabel(), QString("mBTC"));

    amt.fromDisplay("1.23456");
    amt.fromDisplay("1.23456789");
    QCOMPARE(amt.inputState(), BitcoinAmount::InputState::Invalid);
    QCOMPARE(amt.satoshi(), qint64{123456});
    QCOMPARE(amt.toDisplay(), QString("1.23456"));

    amt.setUnit(BitcoinAmount::Unit::MicroBTC);
    QCOMPARE(amt.toDisplay(), QString("1234.56"));
    QCOMPARE(amt.unitLabel(), QString("bits"));

    amt.fromDisplay("1.23");
    amt.fromDisplay("1.239");
    QCOMPARE(amt.inputState(), BitcoinAmount::InputState::Invalid);
    QCOMPARE(amt.satoshi(), qint64{123});
    QCOMPARE(amt.toDisplay(), QString("1.23"));
}

void BitcoinAmountTests::display_flow_sat()
{
    BitcoinAmount amt;
    amt.setUnit(BitcoinAmount::Unit::SAT);
    amt.setSatoshi(123);
    QCOMPARE(amt.toDisplay(), QString("123"));
    QSignalSpy labels{&amt, &BitcoinAmount::unitChanged};
    amt.setSatoshi(1);
    QCOMPARE(labels.count(), 1);
    QCOMPARE(amt.unitLabel(), QStringLiteral("sat"));
    amt.setSatoshi(std::numeric_limits<qint64>::min());
    QCOMPARE(labels.count(), 2);
    QCOMPARE(amt.unitLabel(), QStringLiteral("sats"));
    amt.setSatoshi(std::numeric_limits<qint64>::max());
    QCOMPARE(labels.count(), 2);
    amt.setSatoshi(-1);
    QCOMPARE(labels.count(), 3);
    QCOMPARE(amt.unitLabel(), QStringLiteral("sat"));
}

void BitcoinAmountTests::displayWithUnit_formatsAmountAndUnit()
{
    BitcoinAmount amt;
    QVERIFY(amt.displayWithUnit().isEmpty());

    amt.setSatoshi(COIN);
    QCOMPARE(amt.displayWithUnit(), QStringLiteral("1.00000000 BTC"));

    amt.setUnit(BitcoinAmount::Unit::SAT);
    QCOMPARE(amt.displayWithUnit(), QStringLiteral("100000000 sats"));

    amt.setSatoshi(1);
    QCOMPARE(amt.displayWithUnit(), QStringLiteral("1 sat"));
}

void BitcoinAmountTests::localizedDisplayWithUnit_usesLocaleAndKeepsCanonicalDisplay()
{
    const QLocale previous;
    const auto restore_locale = qScopeGuard([previous] { QLocale::setDefault(previous); });
    QLocale::setDefault(QLocale{"de_DE"});

    BitcoinAmount amount;
    QVERIFY(amount.localizedDisplayWithUnit().isEmpty());
    QVERIFY(amount.localizedDisplay().isEmpty());
    amount.setSatoshi(123'456'789);
    QCOMPARE(amount.localizedDisplayWithUnit(), QStringLiteral("1,23456789 BTC"));
    QCOMPARE(amount.localizedDisplay(), QStringLiteral("1,23456789"));
    QCOMPARE(amount.displayWithUnit(), QStringLiteral("1.23456789 BTC"));

    amount.setUnit(BitcoinAmount::Unit::SAT);
    QCOMPARE(amount.localizedDisplayWithUnit(), QStringLiteral("123.456.789 sats"));
    QCOMPARE(amount.localizedDisplay(), QStringLiteral("123.456.789"));
    QCOMPARE(amount.displayWithUnit(), QStringLiteral("123456789 sats"));
}

void BitcoinAmountTests::flipUnit_changesLabelAndDisplaySignal()
{
    BitcoinAmount amt;
    amt.setSatoshi(100);
    QSignalSpy spy(&amt, &BitcoinAmount::displayChanged);
    amt.flipUnit();
    QVERIFY(spy.count() >= 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(BitcoinAmountTests)
#else
QTEST_MAIN(BitcoinAmountTests)
#endif
#include <test_bitcoinamount.moc>
