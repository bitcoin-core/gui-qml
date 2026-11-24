// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_TEST_MOCKS_RECEIVEQMLFIXTURE_H
#define BITCOIN_QML_TEST_MOCKS_RECEIVEQMLFIXTURE_H
#include <test/mocks/receiveeditortest.h>

class ReceiveQmlFixture : public QObject {
    Q_OBJECT
    Q_PROPERTY(WalletReceiveModel* receive READ receive CONSTANT)
    Q_PROPERTY(PaymentRequestEditorModel* savedEditor READ savedEditor NOTIFY changed)
    Q_PROPERTY(int writes READ writes NOTIFY changed)
    Q_PROPERTY(int generations READ generations NOTIFY changed)
    Q_PROPERTY(int preferences READ preferences NOTIFY changed)
    Q_PROPERTY(int clipboardWrites READ clipboardWrites NOTIFY changed)
    Q_PROPERTY(QString copiedText READ copiedText NOTIFY changed)
    Q_PROPERTY(qint64 persistedAmount READ persistedAmount NOTIFY changed)
    Q_PROPERTY(QString persistedMessage READ persistedMessage NOTIFY changed)
    Q_PROPERTY(QString persistedNote READ persistedNote NOTIFY changed)
public:
    explicit ReceiveQmlFixture(QObject* parent = nullptr) : QObject(parent), m_receipts(std::make_unique<ReceiptReconciliationModel>(receiptPorts())), m_receive(std::make_unique<WalletReceiveModel>(2, receivePorts()))
    {
        publishCredit(0, false);
    }
    ~ReceiveQmlFixture() override { m_receive.reset(); m_receipts.reset(); m_ports.drain(); }
    WalletReceiveModel* receive() { return m_receive.get(); }
    PaymentRequestEditorModel* savedEditor() const { return m_saved; }
    int writes() const { return m_ports.writes; }
    int generations() const { return m_ports.generations; }
    int preferences() const { return m_ports.preferences; }
    int clipboardWrites() const { return m_ports.clipboard; }
    QString copiedText() const { return m_ports.clipboard_text; }
    qint64 persistedAmount() const { return persisted().values[requestedit::Amount].amount; }
    QString persistedMessage() const { return persisted().values[requestedit::Message].text; }
    QString persistedNote() const { return persisted().values[requestedit::Note].text; }
    Q_INVOKABLE void settle() { m_ports.drain(); Q_EMIT changed(); }
    Q_INVOKABLE void domain() { m_ports.domain(); Q_EMIT changed(); }
    Q_INVOKABLE void backend() { m_ports.drainWorkers(); Q_EMIT changed(); }
    Q_INVOKABLE void publish() { m_ports.publish(); Q_EMIT changed(); }
    Q_INVOKABLE void setNextGenerationError(ReceiveError::Code error) { m_ports.next_generation = error; }
    Q_INVOKABLE void setNextWriteError(ReceiveError::Code error) { m_ports.next_write = error; }
    Q_INVOKABLE void markDraftUsed() { m_ports.useDraft(); m_receive->refresh(); }
    Q_INVOKABLE void markSavedUsed() { m_ports.useSaved(17); m_receive->refresh(); }
    Q_INVOKABLE void publishCredit(qint64 amount, bool used) {
        if (used && !m_ports.records.at(17).used) { m_ports.useSaved(17); m_receive->refresh(); }
        auto snapshot = std::make_shared<walletreceipts::Snapshot>();
        snapshot->opening = m_ports.opening;
        snapshot->native_revision = m_native_revision;
        const auto address = m_ports.records.at(17).address;
        snapshot->addresses.insert(address);
        if (amount > 0) snapshot->totals.emplace(address, amount);
        if (used) snapshot->observed.insert(address);
        m_latest = walletreceipts::Publication{std::move(snapshot), {}, walletreceipts::Status::Ready, false, false, ++m_revision};
        m_current = true; m_receipts->consume();
    }
    Q_INVOKABLE void invalidateCredit() { m_current = false; ++m_native_revision; m_receipts->invalidate(); }
    Q_INVOKABLE void failCredit() { m_current = false; m_latest = walletreceipts::Publication{{}, {}, walletreceipts::Status::Failed}; m_receipts->consume(); }
    Q_INVOKABLE bool openSaved()
    {
        auto* open = m_receive->openRequest(QStringLiteral("17"), this);
        if (!open) return false;
        const auto take = [this, open] { if (open->editor()) { m_saved = open->editor(); Q_EMIT changed(); } };
        connect(open, &PaymentRequestOpenOperation::changed, this, take);
        take();
        return true;
    }
Q_SIGNALS:
    void changed();
private:
    const requestedit::Source& persisted() const { return m_ports.records.at(m_ports.records.contains(47) ? 47 : 17); }
    ReceiptReconciliationModel::Ports receiptPorts() {
        ReceiptReconciliationModel::Ports ports;
        ports.opening = m_ports.opening;
        ports.take = [this] { return std::exchange(m_latest, {}); };
        ports.current = [this] { return m_current; };
        ports.nativeRevision = [this] { return m_native_revision; };
        ports.retire = [this](auto snapshot) {
            const auto admitted = m_ports.execution.outcomes.post([snapshot = std::move(snapshot)]() mutable { snapshot.reset(); });
            Q_ASSERT(admitted);
        };
        return ports;
    }
    WalletReceiveModel::Ports receivePorts() { auto ports = m_ports.all(); ports.receipts = m_receipts.get(); return ports; }
    ReceiveEditorPorts m_ports;
    std::optional<walletreceipts::Publication> m_latest;
    uint64_t m_revision{0}, m_native_revision{0};
    bool m_current{false};
    std::unique_ptr<ReceiptReconciliationModel> m_receipts;
    std::unique_ptr<WalletReceiveModel> m_receive;
    QPointer<PaymentRequestEditorModel> m_saved;
};
#endif // BITCOIN_QML_TEST_MOCKS_RECEIVEQMLFIXTURE_H
