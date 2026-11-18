// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_WALLETCREATIONMODEL_H
#define BITCOIN_QML_MODELS_WALLETCREATIONMODEL_H
#include <qml/models/walletoperationmodel.h>
#include <qml/models/walletprovisioningcheck.h>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

class WalletCreationController;
Q_MOC_INCLUDE("qml/walletcreationcontroller.h")
class WalletCreationText final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit WalletCreationText(QObject* parent, qsizetype maximum = 32768) : QObject(parent), m_maximum(maximum) {}
    QString text() const { return m_text; }
    QString error() const { return m_transport_rejected ? tr("This input is too long.") : m_error; }
    Q_INVOKABLE void edit(const QString& text);
    void setError(QString error);
    bool transportRejected() const { return m_transport_rejected; }
Q_SIGNALS:
    void changed();
    void edited();
private:
    QString m_text, m_error;
    const qsizetype m_maximum;
    bool m_transport_rejected{false};
};
class WalletCreationModel : public QObject {
    Q_OBJECT
    //! The creation controller this form creates through. QML declares the
    //! form and binds this once; the controller supplies its services.
    Q_PROPERTY(WalletCreationController* creation READ creation WRITE setCreation NOTIFY creationChanged)
    Q_PROPERTY(QString editorId READ editorId CONSTANT)
    Q_PROPERTY(quint64 revision READ revision NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(WalletOperationError error READ error NOTIFY changed)
    Q_PROPERTY(FactoryPhase factoryPhase READ factoryPhase NOTIFY changed)
    Q_PROPERTY(WalletOperationError factoryError READ factoryError NOTIFY changed)
    Q_PROPERTY(WalletCreationText* name READ name CONSTANT)
    Q_PROPERTY(WalletCreationText* extendedPublicKey READ extendedPublicKey CONSTANT)
    Q_PROPERTY(Kind kind READ kind NOTIFY changed)
    Q_PROPERTY(QString selectedSignerId READ selectedSignerId NOTIFY changed)
    Q_PROPERTY(bool encryptRequested READ encryptRequested NOTIFY changed)
    Q_PROPERTY(bool pendingValidation READ pendingValidation NOTIFY changed)
    Q_PROPERTY(bool canCreate READ canCreate NOTIFY changed)
    Q_PROPERTY(WalletOperationModel* operation READ operation NOTIFY changed)
public:
    enum Kind { Software, WatchOnly, ExternalSigner };
    Q_ENUM(Kind)
    enum FactoryPhase { FactoryRequested, FactoryReady, FactoryRejected, FactoryCancelled };
    Q_ENUM(FactoryPhase)
    struct Services {
        WalletOperationModel::Services operation;
        std::function<walletcreation::SignerSnapshot()> signer;
    };
    explicit WalletCreationModel(QObject* parent = nullptr);
    explicit WalletCreationModel(Services services, QObject* parent = nullptr);
    ~WalletCreationModel() override;
    WalletCreationController* creation() const { return m_creation_alive ? m_creation : nullptr; }
    //! Defined with WalletCreationController, which supplies the services.
    void setCreation(WalletCreationController* creation);
    QString editorId() const { return m_editor_id; }
    quint64 revision() const { return m_revision; }
    FactoryPhase factoryPhase() const;
    bool pending() const { return factoryPhase() == FactoryRequested; }
    bool ready() const { return factoryPhase() == FactoryReady; }
    WalletOperationError factoryError() const;
    WalletOperationError error() const { return m_command_error.empty() ? factoryError() : m_command_error; }
    WalletCreationText* name() { return &m_name; }
    WalletCreationText* extendedPublicKey() { return &m_key; }
    Kind kind() const { return m_kind; }
    QString selectedSignerId() const;
    bool encryptRequested() const { return m_encrypt; }
    bool pendingValidation() const { return m_check && m_check->pending(); }
    bool canCreate() const;
    WalletOperationModel* operation() const { return m_operation; }
    Q_INVOKABLE void chooseKind(Kind kind);
    Q_INVOKABLE void chooseSigner(const QString& id);
    Q_INVOKABLE void setEncryptRequested(bool encrypt);
    Q_INVOKABLE WalletOperationModel* create(const QString& passphrase, const QString& confirmation);
    Q_INVOKABLE void discard();
    void refreshCapabilities();
    // C++ compatibility adapters only; the new editor always validates exact text.
Q_SIGNALS:
    void creationChanged();
    void changed();
    void operationCreated(WalletOperationModel* operation);
    void commandRejected(const QString& command_id, const WalletOperationError& error);
private:
    friend class WalletCreationController;
    //! Supplies the services once; later calls are ignored.
    void setServices(Services services);
    void edited();
    void validationChanged();
    WalletCreationController* m_creation{nullptr};
    QPointer<QObject> m_creation_alive;
    Services m_services;
    const QString m_editor_id;
    quint64 m_revision{0};
    WalletCreationText m_name{this}, m_key{this, 4096};
    std::unique_ptr<WalletProvisioningCheck> m_check;
    Kind m_kind{Software};
    QString m_signer;
    bool m_encrypt{true}, m_closed{false};
    FactoryPhase m_factory_phase{FactoryRequested};
    WalletOperationError m_command_error;
    QPointer<WalletOperationModel> m_operation;
};
#endif // BITCOIN_QML_MODELS_WALLETCREATIONMODEL_H
