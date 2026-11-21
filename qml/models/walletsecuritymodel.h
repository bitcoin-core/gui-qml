// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETSECURITYMODEL_H
#define BITCOIN_QML_MODELS_WALLETSECURITYMODEL_H

#include <qml/walletsecurity.h>

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

/** An original opening's credential question and immutable mutation outcome.
 * No credential is a readable property; the Native owner outlives this view. */
class SecurityOperationModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString operationId READ operationId CONSTANT)
    Q_PROPERTY(QString promptId READ operationId CONSTANT)
    Q_PROPERTY(Kind kind READ kind CONSTANT)
    Q_PROPERTY(Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool terminal READ terminal NOTIFY changed)
    Q_PROPERTY(bool outcomeKnown READ outcomeKnown NOTIFY changed)
    Q_PROPERTY(bool canSubmit READ canSubmit NOTIFY changed)
    Q_PROPERTY(bool canCancel READ canSubmit NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool backupRecommended READ backupRecommended NOTIFY changed)
public:
    enum class Kind { Encrypt, ChangePassphrase };
    Q_ENUM(Kind)
    enum class Phase { AwaitingCredentials, Pending, Succeeded, Failed, Uncertain, Cancelled };
    Q_ENUM(Phase)
    SecurityOperationModel(std::shared_ptr<walletopening::Session> opening, Kind kind,
                           std::shared_ptr<QObject> bridge, std::function<bool()> admission,
                           QObject* parent = nullptr);
    QString operationId() const { return m_id; }
    Kind kind() const { return m_kind; }
    Phase phase() const { return m_phase; }
    bool busy() const { return m_phase == Phase::Pending; }
    bool terminal() const { return m_phase != Phase::AwaitingCredentials && !busy(); }
    bool outcomeKnown() const { return terminal() && m_phase != Phase::Uncertain; }
    bool canSubmit() const;
    QString error() const { return m_error; }
    bool backupRecommended() const { return m_kind == Kind::Encrypt && m_phase == Phase::Succeeded; }
    Q_INVOKABLE bool submitCredentials(const QString& prompt, const QString& current,
                                      const QString& replacement, const QString& confirmation);
    Q_INVOKABLE bool cancel();
    void close();
Q_SIGNALS:
    void changed();
private:
    void consume();
    const QString m_id;
    const Kind m_kind;
    const std::shared_ptr<walletopening::Session> m_opening;
    const std::shared_ptr<QObject> m_bridge;
    const std::function<bool()> m_admission;
    std::shared_ptr<walletsecurity::Operation> m_owner;
    Phase m_phase{Phase::AwaitingCredentials};
    QString m_error;
    bool m_closed{false};
};

/** Sole GUI projection of Session security observations. Capabilities describe
 * presentation; Session/public Wallet still enforce command admission. */
class WalletSecurityModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(ObservedState observedState READ observedState NOTIFY observedChanged)
    Q_PROPERTY(bool isEncrypted READ isEncrypted NOTIFY observedChanged)
    Q_PROPERTY(bool isLocked READ isLocked NOTIFY observedChanged)
    Q_PROPERTY(bool canManagePassphrase READ canManagePassphrase NOTIFY observedChanged)
    Q_PROPERTY(bool canEncrypt READ canEncrypt NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool canChangePassphrase READ canChangePassphrase NOTIFY capabilitiesChanged)
    Q_PROPERTY(SecurityOperationModel* activeOperation READ activeOperation NOTIFY operationChanged)
public:
    enum class ObservedState { Unknown, Unencrypted, Locked, Unlocked, Closed };
    Q_ENUM(ObservedState)
    explicit WalletSecurityModel(QObject* parent = nullptr) : QObject(parent) {
        connect(this, &WalletSecurityModel::operationChanged, this, &WalletSecurityModel::capabilitiesChanged);
    }
    void bind(std::shared_ptr<walletopening::Session> opening, std::shared_ptr<QObject> bridge,
              std::function<bool()> admission);
    void observe(const walletopening::Metadata& metadata);
    void close();
    ObservedState observedState() const;
    bool isEncrypted() const { return m_encrypted; }
    bool isLocked() const { return m_locked; }
    bool canManagePassphrase() const;
    bool canEncrypt() const { return canBegin() && canManagePassphrase() && !m_encrypted; }
    bool canChangePassphrase() const { return canBegin() && canManagePassphrase() && m_encrypted; }
    SecurityOperationModel* activeOperation() const { return m_active; }
    Q_INVOKABLE SecurityOperationModel* beginEncryption();
    Q_INVOKABLE SecurityOperationModel* beginPassphraseChange();
Q_SIGNALS:
    void observedChanged();
    void operationChanged();
    void capabilitiesChanged();
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
