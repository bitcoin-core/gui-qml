// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletnamescontroller.h>

#include <qml/executor.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/walletidentity.h>

#include <interfaces/node.h>
#include <interfaces/wallet.h>

#include <stdexcept>

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

WalletNamesController::WalletNamesController(Services services, QObject* parent)
    : QObject(parent), m_services(std::move(services)), m_home_path(qEnvironmentVariable("HOME")),
      m_open_local_path([](const QString& path) { return QDesktopServices::openUrl(QUrl::fromLocalFile(path)); })
{
}

QString WalletNamesController::displayName(const QString& name) const
{
    if (!m_names) return walletidentity::Display(name);
    const auto found = m_names->names.find(name);
    return found != m_names->names.end() && !found->second.isEmpty() ? found->second : walletidentity::Display(name);
}

bool WalletNamesController::setDisplayName(const QString& name, const QString& display_name)
{
    if (m_closed || m_write_pending || name.size() > 32768 || display_name.size() > 32768) return false;
    const auto alias = display_name.trimmed();
    const auto id = ++m_write_id;
    m_write_pending = true; m_write_error.clear();
    auto receipt = std::make_shared<WriteReceipt>(); m_write_receipt = receipt;
    auto weak = std::make_shared<std::weak_ptr<execution::OwnedCommand<WriteResult>>>();
    const QPointer<WalletNamesController> guard{this};
    auto operation = std::make_shared<execution::OwnedCommand<WriteResult>>([weak, receipt, guard, id, post = m_services.post] {
        if (const auto operation = weak->lock()) if (const auto result = operation->result()) {
            std::lock_guard lock{receipt->mutex}; receipt->ready = true;
            receipt->saved = result->value && result->value->saved;
            receipt->error = result->value ? result->value->error : WalletNamesController::tr("The wallet name may not have been saved.");
        }
        post([guard, id] { if (guard) guard->publishWrite(id); });
    });
    *weak = operation; m_write = operation;
    const auto work = [name, alias] {
        QSettings settings;
        walletidentity::WriteAlias(settings, name, alias);
        settings.sync();
        WriteResult result; result.saved = settings.status() == QSettings::NoError;
        if (!result.saved) result.error = tr("Wallet display name could not be saved.");
        return result;
    };
    const auto post = utilityPost();
    const auto admitted = operation->startWith([post, work](auto done, auto failed) {
        return post(execution::MakeTask(work, std::move(done), std::move(failed)));
    });
    if (!admitted) { m_write_pending = false; m_write_error = tr("The wallet name could not be saved. Try again."); }
    Q_EMIT nameWriteChanged(); return bool(admitted);
}

bool WalletNamesController::nameWriteSucceeded() const
{
    if (!m_write_receipt) return false;
    std::lock_guard lock{m_write_receipt->mutex}; return m_write_receipt->ready && m_write_receipt->saved;
}

QString WalletNamesController::nameWriteError() const
{
    if (m_write_receipt) { std::lock_guard lock{m_write_receipt->mutex}; if (m_write_receipt->ready) return m_write_receipt->error; }
    return m_write_error;
}

void WalletNamesController::publishWrite(quint64 id)
{
    if (m_closed || id != m_write_id || !m_write) return;
    const auto result = m_write->result(); if (!result) return;
    m_write_pending = false; m_write_error = nameWriteError();
    const QPointer<WalletNamesController> guard{this};
    if (nameWriteSucceeded()) refresh();
    if (guard) Q_EMIT nameWriteChanged();
}

void WalletNamesController::refresh()
{
    if (m_closed) return;
    if (!m_query) {
        const QPointer<WalletNamesController> guard{this};
        m_query = std::make_shared<execution::SnapshotQuery<std::shared_ptr<const Aliases>>>([] {
                auto result = std::make_shared<Aliases>();
                QSettings settings;
                result->names = walletidentity::ReadAliases(settings);
                if (settings.status() != QSettings::NoError) throw std::runtime_error("Wallet aliases could not be read");
                result->home = QDir::homePath();
                return std::shared_ptr<const Aliases>{std::move(result)};
            }, [guard, post = m_services.post] { post([guard] { if (guard) guard->publishAliases(); }); },
            utilityPost());
    }
    m_query->request();
}

void WalletNamesController::publishAliases()
{
    if (m_closed || !m_query) return;
    const auto result = m_query->take(); if (!result) return;
    if (!result->snapshot || !*result->snapshot) { Q_EMIT namesChanged(); return; }
    m_names = *result->snapshot; m_home_path = m_names->home;
    m_cursor.reset(); applyAliases(++m_generation);
}

void WalletNamesController::applyAliases(quint64 generation)
{
    if (m_closed || generation != m_generation || !m_services.next_wallet) return;
    const QPointer<WalletNamesController> guard{this};
    // A bounded batch per turn, independent of the number of open wallets.
    int budget{32};
    while (budget--) {
        auto* wallet = m_services.next_wallet(m_cursor ? *m_cursor : nullptr);
        if (!wallet) { Q_EMIT namesChanged(); return; }
        m_cursor = wallet;
        apply(wallet); if (!guard || m_closed) return;
    }
    QMetaObject::invokeMethod(this, [guard, generation] { if (guard) guard->applyAliases(generation); }, Qt::QueuedConnection);
}

void WalletNamesController::apply(WalletQmlModel* wallet) const
{
    if (wallet) wallet->setDisplayName(displayName(wallet->name()));
}

bool WalletNamesController::openLocation(WalletQmlModel* wallet)
{
    if (m_closed || m_location_pending) return false;
    if (!wallet || wallet == m_services.empty_wallet) {
        setLocationError(tr("No wallet file is available to view."));
        return false;
    }
    if (!wallet->available()) return false;
    const auto opening = wallet->opening();
    const auto action = opening->startAction(); if (!action) return false;
    const auto name = wallet->name();
    const auto id = ++m_location_id;
    m_location_wallet = wallet; m_location_pending = true; m_location_error.clear();
    const QPointer<WalletNamesController> guard{this};
    auto operation = std::make_shared<execution::OwnedCommand<LocationResult>>([guard, id, post = m_services.post] {
        post([guard, id] { if (guard) guard->publishLocation(id); });
    });
    m_location = operation;
    const auto work = [name, node = m_services.node] {
        LocationResult result;
        QFileInfo path{name.isEmpty() ? QString::fromStdString(node->walletLoader().getWalletDir()) : name};
        if (!path.isAbsolute()) path = QFileInfo{QDir{QString::fromStdString(node->walletLoader().getWalletDir())}.filePath(name)};
        if (!path.exists()) { result.error = tr("Wallet file not found: %1").arg(path.absoluteFilePath()); return result; }
        result.path = path.isDir() ? path.absoluteFilePath() : path.absolutePath();
        return result;
    };
    const auto admitted = operation->startWith([opening, action, work](auto done, auto failed) {
        return opening->perform(action, [work](interfaces::Wallet&) { return work(); }, std::move(done), std::move(failed));
    });
    if (!admitted) { m_location_pending = false; m_location_error = tr("Wallet location could not be checked."); }
    Q_EMIT locationChanged(); return bool(admitted);
}

void WalletNamesController::publishLocation(quint64 id)
{
    if (m_closed || id != m_location_id || !m_location) return;
    const auto result = m_location->result(); if (!result) return;
    m_location_pending = false;
    m_location_error = result->value ? result->value->error : tr("Wallet location could not be checked.");
    const QPointer<WalletNamesController> guard{this};
    if (m_location_error.isEmpty() && m_location_wallet && m_location_wallet->available()) {
        if (!m_open_local_path(result->value->path)) { if (!guard) return; m_location_error = tr("Could not open wallet file location."); }
    } else if (m_location_error.isEmpty()) m_location_error = tr("This wallet is closing.");
    if (guard) Q_EMIT locationChanged();
}

void WalletNamesController::clearLocationError() { setLocationError({}); }

void WalletNamesController::setLocationError(const QString& error)
{
    if (m_location_error == error) return;
    m_location_error = error;
    Q_EMIT locationChanged();
}

void WalletNamesController::close()
{
    if (m_closed) return;
    m_closed = true;
    if (m_query) m_query->close();
    m_query.reset(); m_names.reset();
    m_write.reset(); m_write_pending = false;
    m_location.reset(); m_location_pending = false;
    m_services.utility->close();
    m_services.utility->whenDrained([outcomes = m_services.outcomes] { outcomes->close(); });
}

execution::SnapshotQuery<std::shared_ptr<const WalletNamesController::Aliases>>::Post WalletNamesController::utilityPost() const
{
    return [utility = m_services.utility, outcomes = m_services.outcomes](std::shared_ptr<execution::Task> task) {
        const bool accepted = utility->post([outcomes, task = std::move(task)]() mutable {
            task->execute();
            auto deliver = [task = std::move(task)] { task->deliver(); };
            // Both queues use the same Utility worker. Finish an admitted task
            // here if shutdown has closed the outcome queue.
            if (!outcomes->post(deliver)) deliver();
        });
        return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
    };
}
