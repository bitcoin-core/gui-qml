// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_NODEMODEL_H
#define BITCOIN_QML_MODELS_NODEMODEL_H

#include <interfaces/handler.h>
#include <interfaces/node.h>

#include <memory>

#include <QObject>
#include <QString>
#include <QTimer>

class NodeModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(int blockTipHeight READ blockTipHeight NOTIFY blockTipChanged)
    Q_PROPERTY(double verificationProgress READ verificationProgress NOTIFY blockTipChanged)

public:
    enum State {
        IDLE,
        INITIALIZING,
        RUNNING,
        FAILED,
        SHUTTING_DOWN,
        STOPPED,
    };
    Q_ENUM(State)

    explicit NodeModel(interfaces::Node& node);

    State state() const { return m_state; }
    QString statusText() const;
    QString errorMessage() const { return m_error_message; }
    int blockTipHeight() const { return m_block_tip_height; }
    double verificationProgress() const { return m_verification_progress; }

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
    void blockTipChanged();
    void requestedInitialize();
    void requestedShutdown();
    void initializationFinished(bool success);
    void shutdownComplete();

private:
    void setState(State state);
    void setErrorMessage(const QString& message);
    void setBlockTip(int height, double verification_progress);

    interfaces::Node& m_node;
    std::unique_ptr<interfaces::Handler> m_handler_notify_block_tip;
    State m_state{IDLE};
    QString m_error_message;
    int m_block_tip_height{0};
    double m_verification_progress{0.0};
    QTimer m_node_poll_timer;
};

#endif // BITCOIN_QML_MODELS_NODEMODEL_H
