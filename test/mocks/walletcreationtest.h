// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_WALLETCREATIONTEST_H
#define BITCOIN_QML_TEST_MOCKS_WALLETCREATIONTEST_H

#include <qml/walletcreationcontroller.h>
#include <qml/walletcontroller.h>

#include <QList>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QtTest/QtTest>

#include <memory>

//! The creation controller started \p operation and will deliver it.
inline bool Accepted(const WalletOperationModel* operation) { return operation && operation->acceptedForDelivery(); }
inline bool SignerPending(const WalletCreationController* creation) { return creation->signers() && creation->signers()->pending(); }
inline QString SignerName(const WalletCreationController* creation) { return creation->signers() ? creation->signers()->uniqueSignerName() : QString{}; }
inline QString SignerError(const WalletCreationController* creation) { return creation->signers() ? creation->signers()->error().message : QString{}; }
inline QString SignerWalletName(const WalletCreationController* creation)
{
    return creation->signers() ? creation->signers()->suggestedWalletName() : QStringLiteral("external_signer");
}
//! The active operation's full error text, or empty.
inline QString OperationError(const WalletCreationController* creation) { return creation->operation() ? creation->operation()->errorText() : QString{}; }
//! The active operation's bounded error message, or empty.
inline QString OperationMessage(const WalletCreationController* creation) { return creation->operation() ? creation->operation()->error().message : QString{}; }

//! The active operation's state in the wallet list's terms.
inline WalletController::WalletOperationState CreationState(const WalletCreationController* creation)
{
    using State = WalletController::WalletOperationState;
    const auto* operation = creation->operation();
    if (!operation) return State::WalletOperationIdle;
    switch (operation->phase()) {
    case WalletOperationModel::AwaitingConfirmation: return State::WalletOperationNeedsMigration;
    case WalletOperationModel::AwaitingPassphrase: return State::WalletOperationNeedsPassphrase;
    case WalletOperationModel::Succeeded: return State::WalletOperationSucceeded;
    case WalletOperationModel::Partial: return State::WalletOperationIncomplete;
    case WalletOperationModel::Uncertain: return State::WalletOperationUncertain;
    case WalletOperationModel::Failed: case WalletOperationModel::Rejected: case WalletOperationModel::Cancelled: return State::WalletOperationFailed;
    default: return State::WalletOperationPending;
    }
}
inline WalletModel* CreationResult(const WalletCreationController* creation) { return creation->operation() ? creation->operation()->resultWallet() : nullptr; }
inline bool CreationCanOpen(const WalletCreationController* creation) { return creation->operation() && creation->operation()->canOpenResult(); }
inline QString OperationWarnings(const WalletCreationController* creation) { return creation->operation() ? creation->operation()->warningText() : QString{}; }

//! Records one kind of outcome of the operations a creation controller runs,
//! with QSignalSpy's reading API: each entry holds the outcome's arguments.
//! The records outlive the spy, so late notifications are harmless.
class OutcomeSpy
{
public:
    enum class Kind { Created, SignerOpened, Imported, MigrationSucceeded, MigrationFailed, PassphraseRequired, ErrorChanged };
    using Entry = QList<QVariant>;
    OutcomeSpy(WalletCreationController* creation, Kind kind) : m_state(std::make_shared<State>())
    {
        const auto state = m_state;
        state->creation = creation;
        const auto watch = [state, kind](WalletOperationModel* operation) {
            if (!operation || state->seen.contains(operation)) return;
            state->seen.insert(operation);
            const QPointer<WalletOperationModel> original{operation};
            QObject::connect(operation, &WalletOperationModel::finished, state->creation, [state, kind, original] {
                if (!original) return;
                const bool ok = original->phase() == WalletOperationModel::Succeeded;
                const QVariant wallet = QVariant::fromValue(original->resultWallet());
                if (original->kind() == WalletOperationModel::Restore) { if (ok && kind == Kind::Imported) state->records.append(Entry{wallet}); return; }
                if (original->kind() == WalletOperationModel::Migrate) {
                    if ((kind == Kind::MigrationSucceeded && ok) || (kind == Kind::MigrationFailed && !ok)) state->records.append(Entry{});
                    return;
                }
                if (!ok || !original->canOpenResult()) return;
                const bool signer = original->creationKind() == walletcreation::Kind::ExternalSigner;
                if ((kind == Kind::SignerOpened && signer) || (kind == Kind::Created && !signer)) state->records.append(Entry{wallet});
            });
            QObject::connect(operation, &WalletOperationModel::changed, state->creation, [state, kind, original] {
                if (!original || kind != Kind::PassphraseRequired) return;
                if (original->kind() == WalletOperationModel::Migrate && original->phase() == WalletOperationModel::AwaitingPassphrase)
                    state->records.append(Entry{original->targetWalletId()});
            });
        };
        QObject::connect(creation, &WalletCreationController::operationChanged, creation, [state, kind, watch] {
            auto* operation = state->creation ? state->creation->operation() : nullptr;
            watch(operation);
            if (kind != Kind::ErrorChanged) return;
            const auto* error = operation ? operation->errorText().constData() : nullptr;
            const bool fresh = operation != state->last;
            state->last = operation;
            // A newly started operation starts from its own text; a refused
            // one, or a later change, replaces the shown error.
            if (fresh && operation && operation->acceptedForDelivery()) { state->error = error; return; }
            if (error != state->error) { state->error = error; state->records.append(Entry{}); }
        });
        watch(creation->operation());
    }
    qsizetype count() const { return m_state->records.size(); }
    qsizetype size() const { return m_state->records.size(); }
    bool isEmpty() const { return m_state->records.isEmpty(); }
    bool empty() const { return m_state->records.isEmpty(); }
    const Entry& at(qsizetype i) const { return m_state->records.at(i); }
    const Entry& front() const { return m_state->records.front(); }
    const Entry& first() const { return m_state->records.first(); }
    const Entry& last() const { return m_state->records.last(); }
    const Entry& operator[](qsizetype i) const { return m_state->records.at(i); }
    Entry takeFirst() { return m_state->records.takeFirst(); }
    bool wait(int timeout = 5000)
    {
        const auto before = size();
        return QTest::qWaitFor([&] { return size() > before; }, timeout);
    }
private:
    struct State {
        QPointer<WalletCreationController> creation;
        QSet<const WalletOperationModel*> seen;
        QPointer<WalletOperationModel> last;
        const QChar* error{nullptr};
        QList<Entry> records;
    };
    std::shared_ptr<State> m_state;
};

#endif // BITCOIN_QML_TEST_MOCKS_WALLETCREATIONTEST_H
