// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SENDDRAFTCONTROLLER_H
#define BITCOIN_QML_MODELS_SENDDRAFTCONTROLLER_H

#include <qml/models/preparationoperation.h>
#include <qml/models/psbtoperation.h>
#include <qml/models/senddraftinterface.h>

#include <QObject>
#include <QPointer>
#include <QString>

/** The Send page's actions that turn its draft into a review: preparing it
 * and importing a PSBT file. The page declares it and binds to its state. A
 * result answers for the draft it was started for: editing the draft, another
 * wallet or hiding the page drops it. */
class SendDraftController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(SendDraftInterface* draftInterface READ draftInterface WRITE setDraftInterface NOTIFY draftInterfaceChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(PreparationOperation::State preparationState READ preparationState NOTIFY preparationChanged)
    Q_PROPERTY(bool canPrepare READ canPrepare NOTIFY canPrepareChanged)
    Q_PROPERTY(bool preparing READ preparing NOTIFY preparationChanged)
    Q_PROPERTY(bool passwordRequested READ passwordRequested NOTIFY preparationChanged)
    Q_PROPERTY(QString preparationError READ preparationError NOTIFY preparationChanged)
    Q_PROPERTY(PsbtOperation::State importState READ importState NOTIFY importChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY importChanged)
    //! WalletModel::PsbtImportResult of a finished import.
    Q_PROPERTY(int importResult READ importResult NOTIFY importChanged)
    //! The user is asked to confirm a draft that sends all available funds.
    Q_PROPERTY(bool sweepConsentRequested READ sweepConsentRequested NOTIFY sweepConsentChanged)

public:
    explicit SendDraftController(QObject* parent = nullptr);
    ~SendDraftController() override;

    SendDraftInterface* draftInterface() const { return m_interface; }
    void setDraftInterface(SendDraftInterface* draft_interface);
    bool active() const { return m_active; }
    void setActive(bool active);

    PreparationOperation::State preparationState() const;
    bool preparing() const { return m_preparation && m_preparation->pending(); }
    bool passwordRequested() const { return m_preparation && m_preparation->passwordRequested(); }
    QString preparationError() const;
    PsbtOperation::State importState() const { return m_import ? m_import->state() : PsbtOperation::State::None; }
    bool importing() const { return m_import && m_import->pending(); }
    int importResult() const { return m_import ? m_import->importResult() : 0; }
    bool sweepConsentRequested() const { return m_sweep_consent; }

    bool canPrepare() const;
    Q_INVOKABLE void requestPreparation();
    Q_INVOKABLE bool resetDraft();
    Q_INVOKABLE void prepare();
    //! Answers the preparation's password prompt.
    Q_INVOKABLE void unlock(const QString& passphrase);
    Q_INVOKABLE void cancelPreparation();
    //! Clears a finished preparation's result once the page showed it.
    Q_INVOKABLE void dismissPreparationResult();
    Q_INVOKABLE void importPsbt(const QString& path);
    //! Asks before preparing a draft that sends all available funds; the
    //! answer belongs to that draft.
    Q_INVOKABLE void askSweepConsent();
    Q_INVOKABLE void confirmSweepConsent();
    Q_INVOKABLE void cancelSweepConsent();

Q_SIGNALS:
    void draftInterfaceChanged();
    void activeChanged();
    void preparationChanged();
    void importChanged();
    void canPrepareChanged();
    void sweepConsentChanged();

private:
    void dropAll();
    void dropPreparation();
    void dropImport();
    void importChangedState();

    QPointer<SendDraftInterface> m_interface;
    bool m_active{true};
    QPointer<PreparationOperation> m_preparation;
    //! The wallet refused the latest preparation because another action runs.
    bool m_preparation_refused{false};
    QPointer<PsbtOperation> m_import;
    //! The draft the current operations were started for.
    QString m_draft;
    bool m_sweep_consent{false};
    QString m_sweep_draft;
};

#endif // BITCOIN_QML_MODELS_SENDDRAFTCONTROLLER_H
