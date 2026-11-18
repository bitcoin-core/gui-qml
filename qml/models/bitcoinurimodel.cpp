// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bitcoinurimodel.h>

#include <bitcoin-build-config.h> // IWYU pragma: keep
#include <qml/models/bitcoinuri.h>
#include <qml/executor.h>
#ifdef ENABLE_WALLET
#include <qml/models/walletqmlmodel.h>
#endif
#include <QTimer>
#include <qml/guipresentation.h>
#include <QPointer>
#include <QUrl>
#include <QtCore/qscopeguard.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <map>

// Import owners, closed with the model.
struct BitcoinUriOperations {
#ifdef ENABLE_WALLET
    std::mutex import_mutex;
    std::map<uint64_t, std::shared_ptr<paymentimport::Owner>> imports;
    std::vector<std::function<void()>> drained;
    std::shared_ptr<execution::SerialQueue> reads, outcomes;
    std::shared_ptr<backup::InstallCoordinator> export_coordinator;
    std::map<uint64_t, QPointer<PaymentImportOperation>> projections;
    std::shared_ptr<QObject> timer{presentation::MakeGuiObject()};

    void remove(uint64_t identity)
    {
        decltype(imports)::node_type retired;
        std::vector<std::function<void()>> callbacks;
        {
            std::lock_guard lock{import_mutex};
            retired = imports.extract(identity);
            if (imports.empty()) callbacks.swap(drained);
        }
        for (const auto& done : callbacks) done();
    }

    void whenRetired(std::function<void()> done)
    {
        {
            std::lock_guard lock{import_mutex};
            if (!imports.empty()) {
                drained.push_back(std::move(done));
                return;
            }
        }
        done();
    }

    void retireImports() {
        std::vector<std::shared_ptr<paymentimport::Owner>> retained;
        { std::lock_guard lock{import_mutex}; for (const auto& [id, operation] : imports) retained.push_back(operation); }
        for (const auto& operation : retained) operation->close();
    }
#endif
};
BitcoinUriModel::BitcoinUriModel(QObject* parent)
    : QObject(parent), m_operations(std::make_shared<BitcoinUriOperations>())
{
}
BitcoinUriModel::~BitcoinUriModel()
{
    close();
}
void BitcoinUriModel::setServices(presentation::Post post, execution::Executor* executor)
{
    if (m_closed) return;
    m_post = std::move(post);
#ifdef ENABLE_WALLET
    if (executor && !m_operations->reads) {
        m_operations->reads = std::make_shared<execution::SerialQueue>(executor->makeQueue(execution::Lane::Utility));
        m_operations->outcomes = std::make_shared<execution::SerialQueue>(executor->makeQueue(execution::Lane::Utility));
    }
#else
    (void)executor;
#endif
}
void BitcoinUriModel::close()
{
    if (std::exchange(m_closed, true)) return;
    *m_admission = true;
#ifdef ENABLE_WALLET
    m_operations->retireImports();
    const auto projections = std::exchange(m_operations->projections, {});
    for (const auto& [id, operation] : projections) {
        if (operation && operation->fileExportOperation()) operation->fileExportOperation()->retire();
    }
#endif
}
void BitcoinUriModel::whenDrained(std::function<void()> done)
{
#ifdef ENABLE_WALLET
    if (const auto reads = m_operations->reads) {
        m_operations->whenRetired([reads, outcomes = m_operations->outcomes, done = std::move(done)]() mutable {
            reads->close();
            reads->whenDrained([outcomes, done = std::move(done)]() mutable {
                outcomes->close();
                outcomes->whenDrained(std::move(done));
            });
        });
        return;
    }
#endif
    done();
}

bool BitcoinUriModel::isPaymentUri(const QString& text) const
{
    return text.trimmed().startsWith(QLatin1String{"bitcoin:"}, Qt::CaseInsensitive);
}
QString BitcoinUriModel::pastedPaymentUri(const QString& previous, const QString& current, const QString& clipboard) const
{
    return isPaymentUri(clipboard) && BitcoinUri::PasteProvenance(previous, current, clipboard) ? clipboard.trimmed() : QString{};
}
#ifdef ENABLE_WALLET
void BitcoinUriModel::setExportCoordinator(std::shared_ptr<backup::InstallCoordinator> coordinator)
{
    m_operations->export_coordinator = std::move(coordinator);
}

PaymentImportOperation* BitcoinUriModel::requestImportText(const QString& text, WalletQmlModel* wallet, SendRecipient* recipient, QObject* owner)
{
    const QPointer<BitcoinUriModel> self{this}; const QPointer<QObject> original_owner{owner};
    const auto target = wallet ? wallet->importTarget(recipient) : nullptr;
    return self && original_owner ? self->requestImport({paymentimport::Input::Kind::Text, text}, target, original_owner) : nullptr;
}
PaymentImportOperation* BitcoinUriModel::requestImportFile(const QString& path, WalletQmlModel* wallet, SendRecipient* recipient, QObject* owner)
{
    const QPointer<BitcoinUriModel> self{this}; const QPointer<QObject> original_owner{owner};
    const auto target = wallet ? wallet->importTarget(recipient) : nullptr;
    return self && original_owner ? self->requestImport({paymentimport::Input::Kind::File, path}, target, original_owner) : nullptr;
}
PaymentImportOperation* BitcoinUriModel::requestImport(paymentimport::Input input, std::shared_ptr<const paymentimport::TargetPort> target, QObject* owner,
                                                      std::shared_ptr<paymentimport::CreationReceipt> creation)
{
    return createImport(std::move(input), std::move(target), owner, std::move(creation));
}
PaymentImportOperation* BitcoinUriModel::captureImport(WalletQmlModel* wallet, SendRecipient* recipient, QObject* owner)
{
    const QPointer<BitcoinUriModel> self{this}; const QPointer<QObject> original_owner{owner};
    const auto target = wallet ? wallet->importTarget(recipient) : nullptr;
    return self && original_owner ? self->createImport({}, target, original_owner) : nullptr;
}
PaymentImportOperation* BitcoinUriModel::requestImportDrop(int urlCount, const QUrl& firstUrl, const QString& text, WalletQmlModel* wallet, SendRecipient* recipient, QObject* owner)
{
    paymentimport::Input input{paymentimport::Input::Kind::Drop, text}; input.url_count = urlCount; input.url = firstUrl;
    const QPointer<BitcoinUriModel> self{this}; const QPointer<QObject> original_owner{owner};
    const auto target = wallet ? wallet->importTarget(recipient) : nullptr;
    return self && original_owner ? self->requestImport(std::move(input), target, original_owner) : nullptr;
}
PaymentImportOperation* BitcoinUriModel::requestImportPaste(const QString& text, InputField field, WalletQmlModel* wallet, SendRecipient* recipient, QObject* owner)
{
    paymentimport::Input input{paymentimport::Input::Kind::Text, text};
    if (field != InputField::AddressField && field != InputField::AmountField && field != InputField::PrivateNoteField) return nullptr;
    switch (field) {
    case InputField::AddressField: input.field = paymentimport::SourceField::Address; break;
    case InputField::AmountField: input.field = paymentimport::SourceField::Amount; break;
    case InputField::PrivateNoteField: input.field = paymentimport::SourceField::PrivateNote; break;
    }
    const QPointer<BitcoinUriModel> self{this}; const QPointer<QObject> original_owner{owner};
    const auto target = wallet ? wallet->importTarget(recipient) : nullptr;
    return self && original_owner ? self->requestImport(std::move(input), target, original_owner) : nullptr;
}
PaymentImportOperation* BitcoinUriModel::createImport(std::optional<paymentimport::Input> input, std::shared_ptr<const paymentimport::TargetPort> target, QObject* owner,
                                                     std::shared_ptr<paymentimport::CreationReceipt> creation)
{
    if (creation && !creation->begin()) return nullptr;
    const auto finish_creation = qScopeGuard([creation] { if (creation) creation->notAccepted(); });
    if (m_closed || !owner || !m_operations->reads || (input && input->value.size() > 1024 * 1024) ||
        m_sequence == std::numeric_limits<quint64>::max() || (creation && creation->withdrawn())) return nullptr;
    auto* operation = new PaymentImportOperation(++m_sequence, owner);
    const QPointer<PaymentImportOperation> observer{operation};
    const auto registry = m_operations;
    const auto post = presentation::ReliablePost(m_post);
    paymentimport::Services services;
    services.reads = registry->reads;
    services.outcomes = registry->outcomes;
    services.wake = [post, timer = registry->timer](auto delay, auto callback) {
        post([timer, delay, callback = std::move(callback)] { QTimer::singleShot(delay, timer.get(), callback); });
    };
    const auto submitted = m_admission;
    const auto identity = operation->requestId();
    auto actor = paymentimport::Owner::Create(identity, std::move(services),
        [post, observer](auto snapshot) { post([observer, snapshot = std::move(snapshot)]() mutable { if (observer) observer->project(std::move(snapshot)); }); },
        [weak = std::weak_ptr<BitcoinUriOperations>{registry}](uint64_t id) {
            if (const auto retained = weak.lock()) retained->remove(id);
        });
    if (!actor || !observer || submitted->load()) { if (actor) actor->close(); if (observer) delete observer.data(); return nullptr; }
    operation->m_owner = actor;
    operation->m_input_started = input.has_value();
    operation->m_snapshot.phase = input ? paymentimport::Phase::Reading : paymentimport::Phase::Empty; operation->m_snapshot.pending = input.has_value();
    operation->m_export_services.post = post;
    operation->m_export_services.utility = registry->reads;
    operation->m_export_services.coordinator = registry->export_coordinator;
    operation->m_export_services.available = [submitted] { return !submitted->load(); };
    registry->projections.emplace(identity, observer);
    connect(operation, &QObject::destroyed, this, [weak = std::weak_ptr<BitcoinUriOperations>{registry}, identity] {
        if (const auto retained = weak.lock()) retained->projections.erase(identity);
    });
    { std::lock_guard lock{registry->import_mutex}; registry->imports.emplace(operation->requestId(), actor); }
    if (creation && creation->withdrawn()) {
        actor->close(); if (observer) delete observer.data(); return nullptr;
    }
    if (input) {
        if (!actor->inspect(std::move(*input))) {
            actor->close(); if (observer) delete observer.data(); return nullptr;
        }
        // Admission is the authoritative creation event. A reentrant observer
        // loss or later target rejection cannot erase this original identity.
        if (creation) creation->accepted(identity);
    }
    if (!actor->chooseTarget(std::move(target)) || !observer || submitted->load() || (creation && creation->withdrawn())) {
        actor->close(); if (observer) delete observer.data(); return nullptr;
    }
    return observer;
}
#endif
