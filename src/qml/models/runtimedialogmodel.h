// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_RUNTIMEDIALOGMODEL_H
#define BITCOIN_QML_MODELS_RUNTIMEDIALOGMODEL_H
#include <interfaces/handler.h>
#include <QObject>
#include <QStringList>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
namespace interfaces { class Node; }
class QEventLoop;

/** Core warning/dialog adapter. Shutdown rejects every pending synchronous response. */
class RuntimeDialogModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString warnings READ warnings NOTIFY warningsChanged)
    Q_PROPERTY(QStringList warningList READ warningList NOTIFY warningsChanged)
    Q_PROPERTY(bool hasWarnings READ hasWarnings NOTIFY warningsChanged)
    Q_PROPERTY(bool runtimeDialogVisible READ runtimeDialogVisible NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogTitle READ runtimeDialogTitle NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogMessage READ runtimeDialogMessage NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogIcon READ runtimeDialogIcon NOTIFY runtimeDialogChanged)
    Q_PROPERTY(unsigned int runtimeDialogButtons READ runtimeDialogButtons NOTIFY runtimeDialogChanged)
    Q_PROPERTY(bool runtimeDialogQuestion READ runtimeDialogQuestion NOTIFY runtimeDialogChanged)
public:
    explicit RuntimeDialogModel(interfaces::Node& node);
    ~RuntimeDialogModel() override;
    QString warnings() const { return m_warnings; }
    QStringList warningList() const { return m_warning_list; }
    bool hasWarnings() const { return !m_warning_list.empty(); }
    bool runtimeDialogVisible() const { return m_runtime_dialog_visible; }
    QString runtimeDialogTitle() const { return m_runtime_dialog_title; }
    QString runtimeDialogMessage() const { return m_runtime_dialog_message; }
    QString runtimeDialogIcon() const { return m_runtime_dialog_icon; }
    unsigned int runtimeDialogButtons() const { return m_runtime_dialog_buttons; }
    bool runtimeDialogQuestion() const { return m_runtime_dialog_question; }
    void addStartupWarnings(const QStringList& warnings);
    void completeInitialization(bool success);
    QString startupErrorSummary() const;
    bool startupFailureDialogShown() const { return m_startup_failure_dialog_shown; }
    Q_INVOKABLE void answerRuntimeDialog(unsigned int button);
public Q_SLOTS:
    void stop();
Q_SIGNALS:
    void warningsChanged();
    void runtimeDialogChanged();
private:
    struct RuntimeDialogRequest {
        QString message;
        unsigned int style{0};
        bool question{false};
        bool answer{false};
        bool answered{false};
        QEventLoop* loop{nullptr};
        std::mutex mutex;
        std::condition_variable completed;
    };
    interfaces::Node& m_node;
    QString m_warnings;
    QStringList m_warning_list;
    QStringList m_startup_error_messages;
    QStringList m_startup_warning_messages;
    bool m_runtime_dialogs_enabled{false};
    bool m_startup_failure_dialog_shown{false};
    bool m_runtime_dialog_visible{false};
    bool m_runtime_dialog_question{false};
    QString m_runtime_dialog_title, m_runtime_dialog_message, m_runtime_dialog_icon;
    unsigned int m_runtime_dialog_buttons{0};
    std::shared_ptr<RuntimeDialogRequest> m_runtime_dialog_active;
    std::deque<std::shared_ptr<RuntimeDialogRequest>> m_runtime_dialog_queue;
    std::mutex m_pending_mutex;
    std::vector<std::weak_ptr<RuntimeDialogRequest>> m_pending;
    std::atomic_bool m_stopping{false};
    std::unique_ptr<interfaces::Handler> m_handler_notify_alert_changed;
    std::unique_ptr<interfaces::Handler> m_handler_message_box;
    std::unique_ptr<interfaces::Handler> m_handler_question;
    void ConnectToAlertChangedSignal();
    void ConnectToRuntimeDialogSignals();
    void refreshWarnings();
    void setWarnings(const QString& warnings);
    void recordStartupErrorMessage(const QString& message);
    void recordStartupWarningMessage(const QString& message);
    void showStartupWarnings();
    void showRuntimeMessageBox(const QString& message, unsigned int style);
    bool showRuntimeQuestion(const QString& message, unsigned int style);
    bool showRuntimeDialogOnGuiThread(const QString& message, unsigned int style, bool question);
    bool submitDialog(const QString& message, unsigned int style, bool question);
    void enqueueRequest(const std::shared_ptr<RuntimeDialogRequest>& request);
    void showRuntimeDialogRequest(const std::shared_ptr<RuntimeDialogRequest>& request);
    void finishRequest(const std::shared_ptr<RuntimeDialogRequest>& request, bool answer);
};
#endif // BITCOIN_QML_MODELS_RUNTIMEDIALOGMODEL_H
