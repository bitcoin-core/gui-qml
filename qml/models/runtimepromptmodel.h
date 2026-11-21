// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_RUNTIMEPROMPTMODEL_H
#define BITCOIN_QML_MODELS_RUNTIMEPROMPTMODEL_H
#include <qml/runtimepromptworker.h>
#include <qml/models/startupdiagnosticsmodel.h>
#include <QObject>
#include <QPointer>

/** Immutable presentation/command handle for exactly one prompt. Retaining an
 * old popup cannot answer the next prompt or retain a Native cleanup scope. */
class RuntimePromptModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString message READ message CONSTANT)
    Q_PROPERTY(QString icon READ icon CONSTANT)
    Q_PROPERTY(unsigned int buttons READ buttons CONSTANT)
    Q_PROPERTY(bool question READ question CONSTANT)
    Q_PROPERTY(QAbstractItemModel* diagnostics READ diagnostics CONSTANT)
public:
    RuntimePromptModel(std::shared_ptr<const runtimeprompt::Prompt> prompt,
        std::weak_ptr<runtimeprompt::Service> service, StartupDiagnosticsModel* diagnostics, QObject* parent=nullptr);
    runtimeprompt::Id id()const{return m_descriptor.id;}
    QString title()const;
    QString message()const{return m_message;}
    QString icon()const;
    unsigned int buttons()const{return m_descriptor.buttons;}
    bool question()const;
    QAbstractItemModel* diagnostics()const{return m_diagnostics;}
    Q_INVOKABLE bool presented();
    Q_INVOKABLE bool answer(unsigned int button);
    Q_INVOKABLE bool dismiss();
    Q_INVOKABLE bool unavailable();
private:
    const runtimeprompt::Descriptor m_descriptor;
    const QString m_message;
    const std::weak_ptr<runtimeprompt::Service> m_service;
    const QPointer<StartupDiagnosticsModel> m_diagnostics;
};
#endif // BITCOIN_QML_MODELS_RUNTIMEPROMPTMODEL_H
