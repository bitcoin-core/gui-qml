// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BITCOINURIMODEL_H
#define BITCOIN_QML_MODELS_BITCOINURIMODEL_H

#include <qml/snapshotquery.h>
#include <bitcoin-build-config.h> // IWYU pragma: keep
#ifdef ENABLE_WALLET
#include <qml/models/paymentimportoperation.h>
#include <qml/paymentimportcreation.h>
#endif
#include <QObject>
#include <atomic>
#include <memory>

struct BitcoinUriOperations;
class BitcoinUriModel;
class WalletQmlModel;
class SendRecipient;

/** Recognizes pasted payment URIs and starts payment request and PSBT
 * imports into a send draft. Reading and analysis run on the injected
 * scheduler; each import is observed through its PaymentImportOperation. */
class BitcoinUriModel : public QObject
{
    Q_OBJECT
public:
    enum class InputField { AddressField, AmountField, PrivateNoteField };
    Q_ENUM(InputField)
    explicit BitcoinUriModel(QObject* parent = nullptr);
    ~BitcoinUriModel() override;
    void setServices(execution::SnapshotServices services);
    void closeAdmission();
    //! Whether pasted text is a payment URI. Decided by its scheme, so a paste
    //! is routed at once: other text is pasted into the field as typed.
    Q_INVOKABLE bool isPaymentUri(const QString& text) const;
    //! The payment URI that an edit from \p previous to \p current inserted
    //! from \p clipboard, or an empty string.
    Q_INVOKABLE QString pastedPaymentUri(const QString& previous, const QString& current, const QString& clipboard) const;
#ifdef ENABLE_WALLET
    Q_INVOKABLE PaymentImportOperation* requestImportText(const QString& text, WalletQmlModel* wallet, SendRecipient* recipient, QObject* owner);
    Q_INVOKABLE PaymentImportOperation* requestImportFile(const QString& path, WalletQmlModel* wallet, SendRecipient* recipient, QObject* owner);
    Q_INVOKABLE PaymentImportOperation* captureImport(WalletQmlModel* wallet, SendRecipient* recipient, QObject* owner);
    Q_INVOKABLE PaymentImportOperation* requestImportDrop(int urlCount, const QUrl& firstUrl, const QString& text, WalletQmlModel* wallet, SendRecipient* recipient, QObject* owner);
    Q_INVOKABLE PaymentImportOperation* requestImportPaste(const QString& text, InputField field, WalletQmlModel* wallet, SendRecipient* recipient, QObject* owner);
    PaymentImportOperation* requestImport(paymentimport::Input input, std::shared_ptr<const paymentimport::TargetPort> target, QObject* owner,
                                          std::shared_ptr<paymentimport::CreationReceipt> creation = {});
#endif
private:
#ifdef ENABLE_WALLET
    PaymentImportOperation* createImport(std::optional<paymentimport::Input> input, std::shared_ptr<const paymentimport::TargetPort> target, QObject* owner,
                                         std::shared_ptr<paymentimport::CreationReceipt> creation = {});
#endif
    execution::SnapshotServices m_services;
    const std::shared_ptr<BitcoinUriOperations> m_operations;
    const std::shared_ptr<std::atomic_bool> m_admission{std::make_shared<std::atomic_bool>(false)};
    quint64 m_sequence{0};
    bool m_closed{false};
};

#endif // BITCOIN_QML_MODELS_BITCOINURIMODEL_H
