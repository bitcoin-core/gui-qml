// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETSECURITYMODEL_H
#define BITCOIN_QML_MODELS_WALLETSECURITYMODEL_H

#include <qml/models/securityoperation.h>
#include <qml/models/walletsecurityinterface.h>
#include <qml/walletsecurity.h>

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

/** An original opening's credential question and immutable mutation outcome.
 * No credential is a readable property; the Native owner outlives this view. */
class SecurityOperationModel : public SecurityOperation
{
    Q_OBJECT
    Q_PROPERTY(QString operationId READ operationId CONSTANT)
    Q_PROPERTY(bool backupRecommended READ backupRecommended NOTIFY changed)
public:
    SecurityOperationModel(std::shared_ptr<walletopening::Session> opening, Kind kind,
                           std::shared_ptr<QObject> bridge, std::function<bool()> submitted,
                           QObject* parent = nullptr);
    QString operationId() const { return m_id; }
    bool canSubmit() const override;
    bool backupRecommended() const { return kind() == Kind::Encrypt && phase() == Phase::Succeeded; }
    bool submit(const QString& current, const QString& replacement, const QString& confirmation) override
    {
        return submitCredentials(operationId(), current, replacement, confirmation);
    }
    //! Submits the passwords for \p prompt; refused unless it is this operation's prompt.
    bool submitCredentials(const QString& prompt, const QString& current,
                           const QString& replacement, const QString& confirmation);
    bool cancel() override;
    void close();
private:
    void consume();
    const QString m_id;
    const std::shared_ptr<walletopening::Session> m_opening;
    const std::shared_ptr<QObject> m_bridge;
    const std::function<bool()> m_admission;
    std::shared_ptr<walletsecurity::Operation> m_owner;
    bool m_closed{false};
};

/** Sole GUI projection of Session security observations. Capabilities describe
 * presentation; Session/public Wallet still enforce command admission. */
class WalletSecurityModel : public WalletSecurityInterface
{
    Q_OBJECT
    Q_PROPERTY(ObservedState observedState READ observedState NOTIFY observedChanged)
    Q_PROPERTY(bool isEncrypted READ isEncrypted NOTIFY observedChanged)
    Q_PROPERTY(bool isLocked READ isLocked NOTIFY observedChanged)
    Q_PROPERTY(bool canManagePassphrase READ canManagePassphrase NOTIFY observedChanged)
    Q_PROPERTY(SecurityOperationModel* activeOperation READ activeOperation NOTIFY operationChanged)
public:
    enum class ObservedState { Unknown, Unencrypted, Locked, Unlocked, Closed };
    Q_ENUM(ObservedState)
    explicit WalletSecurityModel(QObject* parent = nullptr) : WalletSecurityInterface(parent) {
        connect(this, &WalletSecurityModel::operationChanged, this, &WalletSecurityModel::capabilitiesChanged);
    }
    void bind(std::shared_ptr<walletopening::Session> opening, std::shared_ptr<QObject> bridge,
              std::function<bool()> submitted);
    void observe(const walletopening::Metadata& metadata);
    void close();
    ObservedState observedState() const;
    bool isEncrypted() const { return m_encrypted; }
    bool isLocked() const { return m_locked; }
    bool canManagePassphrase() const;
    bool canEncrypt() const override { return canBegin() && canManagePassphrase() && !m_encrypted; }
    bool canChangePassphrase() const override { return canBegin() && canManagePassphrase() && m_encrypted; }
    bool busy() const override { return m_active && m_active->busy(); }
    bool available() const override { return !m_closed && m_opening && m_opening->available(); }
    SecurityOperationModel* activeOperation() const { return m_active; }
    SecurityOperationModel* beginEncryption() override;
    SecurityOperationModel* beginPassphraseChange() override;
Q_SIGNALS:
    void observedChanged();
    void operationChanged();
private:
    bool canBegin() const;
    SecurityOperationModel* begin(SecurityOperationModel::Kind kind);
    std::shared_ptr<walletopening::Session> m_opening;
    std::shared_ptr<QObject> m_bridge;
    std::function<bool()> m_admission;
    QPointer<SecurityOperationModel> m_active;
    unsigned int m_live_operations{0};
    bool m_known{false}, m_encrypted{false}, m_locked{false}, m_no_keys{false}, m_closed{false};
};

#endif // BITCOIN_QML_MODELS_WALLETSECURITYMODEL_H
