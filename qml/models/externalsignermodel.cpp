// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/externalsignermodel.h>

#include <qml/core_settings.h>
#include <qml/models/options_model.h>

#include <common/args.h>
#include <common/settings.h>
#include <interfaces/node.h>
#include <univalue.h>

#include <QEventLoop>
#include <QRegularExpression>
#include <QTextDocument>
#include <QVariantMap>

#include <stdexcept>
#include <utility>

namespace {
// The signer path in effect: a command-line -signer, else the saved one.
// -nosigner is false, which would otherwise read as the path "0".
QString SignerPathSetting(interfaces::Node& node, ArgsManager& args, bool command_line)
{
    const common::SettingsValue setting{command_line
        ? QmlCoreSettings::DisplaySettingValue(node, args, QStringLiteral("signer"))
        : node.getPersistentSetting("signer")};
    if (setting.isFalse()) return {};
    return QString::fromStdString(SettingToString(setting).value_or("")).trimmed();
}
} // namespace

ExternalSignerModel::ExternalSignerModel(interfaces::Node& node, ArgsManager& args, OptionsQmlModel& options, QObject* parent)
    : QObject(parent), m_node{node}, m_args{args}, m_options{options},
      m_command_line{QmlCoreSettings::IsCommandLineOverridden(args, QStringLiteral("signer"))}
{
    m_executor.setObjectName(QStringLiteral("external-signer"));
    connect(&m_executor, &BackendExecutor::drained, this, &ExternalSignerModel::shutdownFinished);
    m_draft_path = m_options.externalSignerPath();

    // A path saved elsewhere, or loaded at startup, replaces the field's text
    // unless it holds an edit of its own.
    connect(&m_options, &OptionsQmlModel::externalSignerPathChanged, this, [this] {
        if (!m_draft_edited && !m_waiting_for_save) syncDraftToSaved();
        Q_EMIT stateChanged();
    });
    connect(&m_options, &OptionsQmlModel::coreSettingStatusesChanged, this, &ExternalSignerModel::stateChanged);
    connect(&m_options, &OptionsQmlModel::signerPathValidationChanged, this, [this] {
        Q_EMIT stateChanged();
        if (m_waiting_for_validation && !m_options.signerPathValidationPending()) {
            m_waiting_for_validation = false;
            continueCheck();
        }
    });
    connect(&m_options, &OptionsQmlModel::settingsPendingChanged, this, [this] {
        Q_EMIT stateChanged();
        if (m_waiting_for_save && !m_options.settingsPending()) {
            m_waiting_for_save = false;
            if (!m_options.settingsError().isEmpty()) {
                // The field keeps what was typed; nothing was checked.
                finishCheck();
                return;
            }
            if (OptionsQmlModel::normalizeSignerPath(m_draft_path) == m_options.externalSignerPath()) syncDraftToSaved();
            startDiscovery();
        }
    });
}

ExternalSignerModel::~ExternalSignerModel()
{
    // A running check uses Node and Args, so they must outlive it, like in
    // OptionsQmlModel. Normal shutdown has drained the worker already.
    QEventLoop loop;
    connect(&m_executor, &BackendExecutor::drained, &loop, &QEventLoop::quit);
    beginShutdown();
    while (!m_executor.isDrained()) loop.exec();
}

void ExternalSignerModel::beginShutdown()
{
    if (m_stopping) return;
    m_stopping = true;
    m_executor.shutdown();
}

void ExternalSignerModel::setDraftPath(const QString& path)
{
    if (path == m_draft_path) return;
    // A check saves and runs the path it started with, so the field keeps
    // showing that path until the result is in. The signal puts a field
    // that was typed into back on it.
    if (m_checking) {
        Q_EMIT draftPathChanged();
        return;
    }
    m_draft_path = path;
    m_draft_edited = OptionsQmlModel::normalizeSignerPath(path) != m_options.externalSignerPath();
    dropResultOfAnotherPath();
    if (canEdit()) m_options.requestExternalSignerPathValidation(path);
    Q_EMIT draftPathChanged();
    Q_EMIT stateChanged();
}

void ExternalSignerModel::revertDraft()
{
    if (m_checking) return;
    syncDraftToSaved();
    Q_EMIT stateChanged();
}

void ExternalSignerModel::syncDraftToSaved()
{
    m_draft_edited = false;
    if (m_draft_path == m_options.externalSignerPath()) return;
    m_draft_path = m_options.externalSignerPath();
    dropResultOfAnotherPath();
    Q_EMIT draftPathChanged();
}

void ExternalSignerModel::dropResultOfAnotherPath()
{
    // A result describes the path it ran with, at the time it ran. Once the
    // field moves off that path, going back to it takes a new check, since
    // the device may have been unplugged or swapped meanwhile.
    if (m_command_line || !m_has_result) return;
    if (OptionsQmlModel::normalizeSignerPath(m_draft_path) != m_result.path) m_has_result = false;
}

bool ExternalSignerModel::canEdit() const
{
    return m_options.coreSettingStatuses().value(QStringLiteral("signer")).toMap()
        .value(QStringLiteral("canEdit"), false).toBool();
}

bool ExternalSignerModel::pathPending() const
{
    return m_options.signerPathValidationPending() || m_options.settingsPending();
}

QString ExternalSignerModel::pathError() const
{
    if (!canEdit() || m_options.signerPathValidationPending()) return {};
    return m_options.externalSignerPathValidationError(m_draft_path);
}

bool ExternalSignerModel::canCheck() const
{
    if (m_checking || m_stopping) return false;
    // A path set on the command line can't be saved, but can still be
    // checked, for example after connecting a device.
    if (m_command_line) return true;
    // A check waits for the path to be validated and saved, so the button
    // stays usable while that runs instead of flickering with each keystroke.
    return canEdit() && pathError().isEmpty();
}

QString ExternalSignerModel::effectivePath() const
{
    if (m_command_line) return SignerPathSetting(m_node, m_args, /*command_line=*/true);
    return OptionsQmlModel::normalizeSignerPath(m_draft_path);
}

ExternalSignerModel::Status ExternalSignerModel::status() const
{
    if (m_checking) return Status::Checking;
    const bool command_line{m_command_line};
    if (effectivePath().isEmpty() && (command_line || m_options.externalSignerPath().isEmpty())) return Status::NotSet;
    // A result describes the path it ran with; an edited, unsaved or
    // still-saving path has none yet. A command-line path never changes.
    const bool current{m_has_result && (command_line || m_result.path == effectivePath())};
    if (!current || (!command_line && pathPending())) return Status::NotChecked;
    if (!m_result.responding) return Status::NotResponding;
    if (m_result.count > 1) return Status::MultipleDevices;
    if (m_result.count == 1) return Status::Ready;
    if (!m_result.error.isEmpty()) return Status::DeviceNotReady;
    return Status::NoDevice;
}

bool ExternalSignerModel::commandResponding() const
{
    switch (status()) {
    case Status::NoDevice:
    case Status::DeviceNotReady:
    case Status::MultipleDevices:
    case Status::Ready:
        return true;
    default:
        return false;
    }
}

QString ExternalSignerModel::deviceName() const
{
    return status() == Status::Ready ? m_result.name : QString{};
}

QString ExternalSignerModel::error() const
{
    switch (status()) {
    case Status::NotResponding:
    case Status::DeviceNotReady:
    case Status::MultipleDevices:
        return m_result.error;
    default:
        return {};
    }
}

QString ExternalSignerModel::suggestedWalletName() const
{
    return SuggestedWalletName(deviceName());
}

void ExternalSignerModel::check()
{
    if (m_checking || m_stopping) return;
    m_checking = true;
    ++m_revision;
    Q_EMIT stateChanged();
    continueCheck();
}

void ExternalSignerModel::continueCheck()
{
    if (!m_checking) return;
    if (m_command_line) {
        startDiscovery();
        return;
    }
    if (!canEdit()) {
        finishCheck();
        return;
    }
    if (m_options.signerPathValidationPending()) {
        m_waiting_for_validation = true;
        return;
    }
    if (!pathError().isEmpty()) {
        finishCheck();
        return;
    }
    if (OptionsQmlModel::normalizeSignerPath(m_draft_path) != m_options.externalSignerPath()) {
        m_options.setExternalSignerPath(m_draft_path);
    }
    if (m_options.settingsPending()) {
        m_waiting_for_save = true;
        return;
    }
    startDiscovery();
}

void ExternalSignerModel::startDiscovery()
{
    const quint64 revision{m_revision};
    const bool submitted = m_executor.submit(this, [node = &m_node, args = &m_args] {
        return Discover(*node, *args);
    }, [this, revision](Result result) {
        // A newer check owns the state; this result is for an older one.
        if (revision != m_revision) return;
        m_result = std::move(result);
        m_has_result = true;
        finishCheck();
    }, [this, revision](std::exception_ptr) {
        if (revision != m_revision) return;
        m_result = Result{effectivePath(), false, 0, {},
            //: Shown under the external signer status when the signer command could not run or printed no device list.
            tr("The signer command did not return valid output. Check that the path is correct.")};
        m_has_result = true;
        finishCheck();
    });
    if (!submitted) finishCheck();
}

void ExternalSignerModel::finishCheck()
{
    m_checking = false;
    m_waiting_for_validation = false;
    m_waiting_for_save = false;
    Q_EMIT stateChanged();
    Q_EMIT checkFinished();
}

ExternalSignerModel::Result ExternalSignerModel::Discover(interfaces::Node& node, ArgsManager& args)
{
    const bool command_line{QmlCoreSettings::IsCommandLineOverridden(args, QStringLiteral("signer"))};
    Result result;
    result.path = SignerPathSetting(node, args, command_line);
    // A saved path is applied to the node; a command-line one already is.
    if (!command_line) {
        node.forceSetting("signer", result.path.isEmpty() ? common::SettingsValue{}
                                                          : common::SettingsValue{result.path.toStdString()});
    }
    try {
        const auto signers = node.listExternalSigners();
        // Without a path Core returns no signers without running anything.
        result.responding = !result.path.isEmpty();
        result.count = static_cast<int>(signers.size());
        if (result.count == 1) {
            result.name = QString::fromStdString(signers.front()->getName());
        } else if (result.count > 1) {
            //: Shown under the external signer status when more than one hardware wallet is connected.
            result.error = tr("More than one external signer was found. Connect only one device.");
        }
    } catch (const std::runtime_error& e) {
        // A device that answers with an error, for example because it is
        // locked, is reported the same way as a command that cannot run.
        // Only the first means the signer command itself works.
        result.error = DeviceError(e.what(), result.path.toStdString());
        result.responding = !result.error.isEmpty();
        if (!result.responding) {
            //: Shown under the external signer status when the signer command could not run or printed no device list.
            result.error = tr("The signer command did not return valid output. Check that the path is correct.");
        }
    }
    return result;
}

QString ExternalSignerModel::DeviceError(const std::string& error, const std::string& command)
{
    // ExternalSigner::Enumerate() names the command in errors about an entry
    // of the signer list it printed. Other errors mean no list was printed.
    const std::string prefix{"'" + command + "' "};
    if (!error.starts_with(prefix)) return {};
    const std::string detail{error.substr(prefix.size())};
    if (detail.starts_with("error: ")) {
        // The reason is the signer tool's own text, and the QML text that
        // shows it reads anything tag-like as markup, so such a reason is
        // left out rather than rendered.
        const QString reason{QString::fromStdString(detail.substr(7))};
        if (!Qt::mightBeRichText(reason)) {
            //: %1 is the reason given by the external signer tool, for example "Device is locked".
            return tr("Your device isn't ready: %1").arg(reason);
        }
    }
    if (detail == "error" || detail.starts_with("error: ")) {
        //: The external signer tool reported an error for the connected hardware wallet without saying why.
        return tr("Your device isn't ready. Unlock it, then check again.");
    }
    if (detail == "received invalid response, missing signer fingerprint") {
        //: The connected hardware wallet did not report its fingerprint, usually because it is locked.
        return tr("Your device didn't share its fingerprint. Unlock it, then check again.");
    }
    return {};
}

QString ExternalSignerModel::SuggestedWalletName(const QString& signer_name)
{
    QString suggested = signer_name.trimmed();
    suggested.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    suggested.remove(QRegularExpression(QStringLiteral("^_+")));
    while (suggested.contains(QStringLiteral("__"))) {
        suggested.replace(QStringLiteral("__"), QStringLiteral("_"));
    }
    if (suggested.isEmpty()) {
        suggested = QStringLiteral("external_signer");
    }
    return suggested.left(20);
}
