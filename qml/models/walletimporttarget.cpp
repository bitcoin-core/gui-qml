// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletimporttarget.h>

#include <qml/bitcoinamount.h>
#include <qml/models/bitcoinaddress.h>
#include <qml/models/bitcoinurimodel.h>
#include <qml/models/paymentimportoperation.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/walletmodel.h>

WalletImportTarget::WalletImportTarget(QObject* parent) : ImportTarget(parent) {}

void WalletImportTarget::setWallet(WalletModel* wallet)
{
    if (m_wallet == wallet) return;
    if (m_wallet) {
        disconnect(m_wallet, nullptr, this, nullptr);
        disconnect(m_wallet->sendRecipientList(), nullptr, this, nullptr);
    }
    m_wallet = wallet;
    if (wallet) {
        auto* recipients = wallet->sendRecipientList();
        connect(wallet, &WalletModel::availableChanged, this, &ImportTarget::availableChanged);
        connect(wallet, &WalletModel::currentTransactionChanged, this, &ImportTarget::importedReviewChanged);
        connect(recipients, &SendRecipientsListModel::revisionChanged, this, &ImportTarget::draftRevisionChanged);
        connect(recipients, &SendRecipientsListModel::validationChanged, this, &ImportTarget::importPendingChanged);
        connect(recipients, &SendRecipientsListModel::currentRecipientChanged, this, [this] {
            watchRecipient();
            Q_EMIT currentRecipientChanged();
        });
    }
    watchRecipient();
    Q_EMIT walletChanged();
    Q_EMIT availableChanged();
}

void WalletImportTarget::setUri(BitcoinUriModel* uri)
{
    if (m_uri == uri) return;
    m_uri = uri;
    Q_EMIT uriChanged();
    Q_EMIT availableChanged();
}

SendRecipient* WalletImportTarget::currentRecipient() const
{
    return m_wallet ? m_wallet->sendRecipientList()->currentRecipient() : nullptr;
}

void WalletImportTarget::watchRecipient()
{
    for (const auto& connection : std::as_const(m_recipient_connections)) disconnect(connection);
    m_recipient_connections.clear();
    m_recipient = currentRecipient();
    if (!m_recipient) return;
    const auto edited = [this] { Q_EMIT recipientEdited(); };
    m_recipient_connections = {
        connect(m_recipient->address(), &BitcoinAddress::addressChanged, this, edited),
        connect(m_recipient->amount(), &BitcoinAmount::amountChanged, this, edited),
        connect(m_recipient, &SendRecipient::labelChanged, this, edited),
        connect(m_recipient, &SendRecipient::messageChanged, this, edited),
        connect(m_recipient, &SendRecipient::paymentRequestChanged, this, edited),
    };
}

bool WalletImportTarget::available() const { return m_wallet && m_uri && m_wallet->available(); }
QString WalletImportTarget::draftRevision() const { return m_wallet ? m_wallet->sendRecipientList()->revision() : QString{}; }
bool WalletImportTarget::importPending() const { return m_wallet && m_wallet->sendRecipientList()->importPending(); }
bool WalletImportTarget::recipientAddressEmpty() const
{
    const auto* recipient = currentRecipient();
    return !recipient || recipient->address()->address().isEmpty();
}
QString WalletImportTarget::importedReviewToken() const { return m_wallet ? m_wallet->importedReviewToken() : QString{}; }

ImportOperation* WalletImportTarget::importText(const QString& text, QObject* owner)
{
    return m_uri && m_wallet && currentRecipient() ? m_uri->requestImportText(text, m_wallet, currentRecipient(), owner) : nullptr;
}

ImportOperation* WalletImportTarget::importPaste(const QString& text, int field, QObject* recipient, QObject* owner)
{
    auto* target = recipient ? qobject_cast<SendRecipient*>(recipient) : currentRecipient();
    return m_uri && m_wallet && target
        ? m_uri->requestImportPaste(text, static_cast<BitcoinUriModel::InputField>(field), m_wallet, target, owner) : nullptr;
}

ImportOperation* WalletImportTarget::importFile(const QString& path, QObject* owner)
{
    return m_uri && m_wallet ? m_uri->requestImportFile(path, m_wallet, currentRecipient(), owner) : nullptr;
}

ImportOperation* WalletImportTarget::importDrop(int url_count, const QUrl& first_url, const QString& text, QObject* owner)
{
    return m_uri && m_wallet ? m_uri->requestImportDrop(url_count, first_url, text, m_wallet, currentRecipient(), owner) : nullptr;
}

ImportOperation* WalletImportTarget::captureFile(QObject* owner)
{
    return m_uri && m_wallet ? m_uri->captureImport(m_wallet, currentRecipient(), owner) : nullptr;
}

bool WalletImportTarget::adopt(ImportOperation* operation)
{
    auto* original = qobject_cast<PaymentImportOperation*>(operation);
    auto* recipient = currentRecipient();
    if (!original || !m_wallet || !recipient || !available()) return false;
    original->chooseTarget(m_wallet, recipient);
    return true;
}
