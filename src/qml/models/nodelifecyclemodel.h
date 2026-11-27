// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_NODELIFECYCLEMODEL_H
#define BITCOIN_QML_MODELS_NODELIFECYCLEMODEL_H
#include <interfaces/node.h>
#include <QObject>
#include <QString>
class QTimerEvent;
class RuntimeDialogModel;

/** Owns initialization/shutdown only. Feature models borrow the same Core node. */
class NodeLifecycleModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString startupError READ errorMessage NOTIFY startupErrorChanged)
    Q_PROPERTY(bool faulted READ errorState NOTIFY errorStateChanged)
public:
    enum State { IDLE, INITIALIZING, RUNNING, FAILED, SHUTTING_DOWN, STOPPED };
    Q_ENUM(State)
    explicit NodeLifecycleModel(interfaces::Node& node, RuntimeDialogModel* dialogs = nullptr);
    ~NodeLifecycleModel() override;
    State state() const { return m_state; }
    QString statusText() const;
    QString errorMessage() const { return m_startup_error; }
    bool errorState() const { return m_faulted; }
    void setStartupError(const QString& error);
    void startShutdownPolling();
    void stopShutdownPolling();
    Q_INVOKABLE void start();
    Q_INVOKABLE void requestShutdown();
public Q_SLOTS:
    void initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info);
    void shutdownResult();
    void handleRunawayException(const QString& message);
Q_SIGNALS:
    void stateChanged();
    void statusTextChanged();
    void errorMessageChanged();
    void startupErrorChanged();
    void errorStateChanged(bool faulted);
    void requestedInitialize();
    // Stop feature workers before delivering this to the Core executor.
    void requestedShutdown();
    void nodeInitialized();
    void initializationFinished(bool success);
    void shutdownComplete();
protected:
    void timerEvent(QTimerEvent* event) override;
private:
    void setState(State state);
    void setErrorState(bool faulted);
    interfaces::Node& m_node;
    RuntimeDialogModel* m_dialogs;
    State m_state{IDLE};
    QString m_startup_error;
    bool m_faulted{false};
    bool m_initialization_requested{false};
    bool m_shutdown_requested{false};
    int m_shutdown_polling_timer_id{0};
};
#endif // BITCOIN_QML_MODELS_NODELIFECYCLEMODEL_H
