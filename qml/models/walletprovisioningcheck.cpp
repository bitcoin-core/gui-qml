// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/walletprovisioningcheck.h>
#include <qml/walletcreation.h>
#include <qml/guipresentation.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
#include <QUrl>
#include <stdexcept>
#include <utility>
WalletProvisioningCheck::WalletProvisioningCheck(interfaces::Node& node, execution::SnapshotServices services, QObject* parent, std::shared_ptr<std::atomic_bool> names_ready, std::shared_ptr<WalletNameRegistry> registry, std::shared_ptr<std::atomic_bool> shutdown_gate, bool strict_name)
    : QObject(parent), m_node(node), m_names_ready(std::move(names_ready)), m_registry(std::move(registry)), m_services(std::move(services)), m_closed(std::move(shutdown_gate)), m_strict_name(strict_name)
{
    Q_ASSERT(m_services.scheduler);
    m_bridge = presentation::MakeGuiObject();
    if (!m_services.post) m_services.post = [bridge = m_bridge](auto turn) { QMetaObject::invokeMethod(bridge.get(), std::move(turn), Qt::QueuedConnection); };
    refresh();
}
WalletProvisioningCheck::~WalletProvisioningCheck() { close(); }
void WalletProvisioningCheck::close() { m_closed = true; m_pending = false; if (m_query) m_query->close(); m_query.reset(); m_registry.reset(); }
void WalletProvisioningCheck::setName(const QString& value) { if (m_closed || m_name == value) return; m_name = value; const QPointer<WalletProvisioningCheck> guard{this}; refresh(); if (guard) Q_EMIT inputChanged(); }
void WalletProvisioningCheck::setXpub(const QString& value) { if (m_closed || m_xpub == value) return; m_xpub = value; const QPointer<WalletProvisioningCheck> guard{this}; refresh(); if (guard) Q_EMIT inputChanged(); }
void WalletProvisioningCheck::setPath(const QString& value) { if (m_closed || m_path == value) return; m_path = value; const QPointer<WalletProvisioningCheck> guard{this}; refresh(); if (guard) Q_EMIT inputChanged(); }
void WalletProvisioningCheck::refresh()
{
    if (m_closed) return;
    ++m_revision; m_pending = true; m_error.clear();
    { std::lock_guard lock{m_input->mutex}; m_input->name = m_name; m_input->xpub = m_xpub; m_input->path = m_path; m_input->revision = m_revision; }
    if (!m_query) {
        const QPointer<WalletProvisioningCheck> guard{this};
        m_query = std::make_shared<execution::SnapshotQuery<Result>>(m_services.scheduler, [input = m_input, node = &m_node, ready = m_names_ready, registry = m_registry, gate = m_closed.shared(), strict = m_strict_name] {
            if (gate && gate->load()) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
            QString name, xpub, path; Result result;
            { std::lock_guard lock{input->mutex}; name = input->name; xpub = input->xpub; path = input->path; result.revision = input->revision; }
            if (name.size() > 32768 || path.size() > 32768 || xpub.size() > 4096) throw std::length_error("Wallet form input is too long");
            walletcreation::Request request; request.submitted_name = name; request.encrypt = false; request.legacy_normalize = !strict;
            result.name_error = ready && !ready->load() ? QCoreApplication::translate("WalletCreationModel", "Wallets are still loading. Try again in a moment.")
                : walletcreation::Validate(request, walletcreation::ReadFacts(*node, request, registry)).error.message;
            result.xpub_valid = DecodeExtPubKey(xpub.trimmed().toStdString()).pubkey.IsValid();
            const QUrl url(path);
            result.normalized_path = path.isEmpty() ? QString{} : QFileInfo(url.isLocalFile() ? url.toLocalFile() : path).absoluteFilePath();
            result.path_exists = !result.normalized_path.isEmpty() && QFileInfo::exists(result.normalized_path);
            return result;
        }, [guard, post = m_services.post] { post([guard] { if (guard) guard->publish(); }); });
    }
    m_query->request(); Q_EMIT changed();
}
void WalletProvisioningCheck::publish()
{
    if (m_closed || !m_query) return;
    const auto result = m_query->take(); if (!result) return;
    if (result->snapshot && result->snapshot->revision != m_revision) return;
    m_pending = false; m_completed = m_revision;
    m_error = result->error ? tr("Wallet form could not be checked.") : QString{};
    m_name_error = result->snapshot ? result->snapshot->name_error : QString{};
    m_xpub_valid = result->snapshot && result->snapshot->xpub_valid;
    m_path_exists = result->snapshot && result->snapshot->path_exists;
    m_normalized_path = result->snapshot ? result->snapshot->normalized_path : QString{};
    Q_EMIT changed();
}
