// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_EXTERNALSIGNERMODEL_H
#define BITCOIN_QML_MODELS_EXTERNALSIGNERMODEL_H

#include <qml/backendexecutor.h>

#include <QObject>
#include <QString>

#include <string>

class ArgsManager;
class OptionsQmlModel;

namespace interfaces {
class Node;
}

// The external signer path and what checking it found, shared by every page
// that shows them. Pages bind to the state here and forward user actions;
// saving goes through the settings layer and the signer command runs on a
// worker thread, never on the GUI thread.
class ExternalSignerModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString draftPath READ draftPath WRITE setDraftPath NOTIFY draftPathChanged)
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY stateChanged)
    Q_PROPERTY(bool canCheck READ canCheck NOTIFY stateChanged)
    Q_PROPERTY(bool pathPending READ pathPending NOTIFY stateChanged)
    Q_PROPERTY(QString pathError READ pathError NOTIFY stateChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY stateChanged)
    Q_PROPERTY(Status status READ status NOTIFY stateChanged)
    Q_PROPERTY(bool commandResponding READ commandResponding NOTIFY stateChanged)
    Q_PROPERTY(bool deviceReady READ deviceReady NOTIFY stateChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString suggestedWalletName READ suggestedWalletName NOTIFY stateChanged)

public:
    enum class Status {
        NotSet,          // no path saved or entered
        NotChecked,      // the path shown has no result yet
        Checking,        // a check is saving the path or running the command
        NotResponding,   // the command did not run or printed no device list
        NoDevice,        // the command works and found no device
        DeviceNotReady,  // the device answered with an error, e.g. locked
        MultipleDevices, // more than one device is connected
        Ready,           // exactly one usable device
    };
    Q_ENUM(Status)

    // What one run of the signer command found.
    struct Result {
        QString path;
        bool responding{false};
        int count{0};
        QString name;
        QString error;
    };

    ExternalSignerModel(interfaces::Node& node, ArgsManager& args, OptionsQmlModel& options, QObject* parent = nullptr);
    ~ExternalSignerModel() override;

    QString draftPath() const { return m_draft_path; }
    void setDraftPath(const QString& path);
    bool canEdit() const;
    bool canCheck() const;
    bool pathPending() const;
    QString pathError() const;
    bool checking() const { return m_checking; }
    Status status() const;
    bool commandResponding() const;
    bool deviceReady() const { return status() == Status::Ready; }
    QString deviceName() const;
    QString error() const;
    QString suggestedWalletName() const;

    // Saves the path in the field if it changed, then runs the signer command.
    // A check asked for while one runs is ignored.
    Q_INVOKABLE void check();
    // Puts the saved path back in the field, dropping an unsaved edit.
    Q_INVOKABLE void revertDraft();

    // Runs the signer command for the saved or command-line path and reports
    // what it found. Blocks on the subprocess, so only on a worker thread.
    static Result Discover(interfaces::Node& node, ArgsManager& args);
    static QString DeviceError(const std::string& error, const std::string& command);
    static QString SuggestedWalletName(const QString& signer_name);

    void beginShutdown();

Q_SIGNALS:
    void draftPathChanged();
    void stateChanged();
    // A check finished, with a result or without (the save failed).
    void checkFinished();
    void shutdownFinished();

private:
    QString effectivePath() const;
    void syncDraftToSaved();
    void dropResultOfAnotherPath();
    void continueCheck();
    void startDiscovery();
    void finishCheck();

    interfaces::Node& m_node;
    ArgsManager& m_args;
    OptionsQmlModel& m_options;
    // The command line can't change while the app runs.
    const bool m_command_line;
    BackendExecutor m_executor;
    QString m_draft_path;
    bool m_draft_edited{false};
    bool m_checking{false};
    bool m_waiting_for_validation{false};
    bool m_waiting_for_save{false};
    bool m_has_result{false};
    Result m_result;
    quint64 m_revision{0};
    bool m_stopping{false};
};

#endif // BITCOIN_QML_MODELS_EXTERNALSIGNERMODEL_H
