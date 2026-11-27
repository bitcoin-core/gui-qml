// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/desktopintentmodel.h>
#include <bitcoin-build-config.h> // IWYU pragma: keep
#include <qml/executor.h>
#include <qml/guipresentation.h>
#ifdef ENABLE_WALLET
#include <qml/models/bitcoinurimodel.h>
#include <qml/paymentimportcreation.h>
#endif
#include <QTimer>
#include <QCoreApplication>
#include <QFileOpenEvent>
#include <QStringList>
#include <limits>
#include <map>
#include <mutex>
#include <utility>

DesktopIntentModel::DesktopIntentModel(execution::Executor& executor, presentation::Post post_to_gui, bool wallet_enabled, QObject* parent)
    : QObject(parent), m_post(presentation::ReliablePost(std::move(post_to_gui))),
      m_queue(std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility))),
      m_wallet_enabled(wallet_enabled)
{
    const QPointer<DesktopIntentModel> observer{this};
    const auto post = m_post;
    const auto timer = presentation::MakeGuiObject();
    desktopintent::Services ports;
    ports.available = [gate = m_parent_closed] { const auto parent = gate->load(); return !parent || !parent->load(); };
    ports.wake = [post, timer](auto delay, auto callback) {
        post([timer, delay, callback = std::move(callback)] { QTimer::singleShot(delay, timer.get(), callback); });
    };
    m_service = desktopintent::Service::Create(std::move(ports), [observer, post](auto snapshot) {
        post([observer, snapshot = std::move(snapshot)]() mutable { if (observer) observer->project(std::move(snapshot)); });
    }, m_queue);
}
DesktopIntentModel::~DesktopIntentModel()
{
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeEventFilter(this);
    close();
}
bool DesktopIntentModel::hasInspection() const
{
#ifdef ENABLE_WALLET
    return !m_inspection.isNull();
#else
    return false;
#endif
}
bool DesktopIntentModel::inspectionPending() const
{
#ifdef ENABLE_WALLET
    return m_inspection && m_inspection->pending();
#else
    return false;
#endif
}
bool DesktopIntentModel::canContinue() const
{
#ifdef ENABLE_WALLET
    return !m_closed && m_inspection && !m_inspection->pending() && m_inspection->state() == PaymentImportOperation::State::Inspecting;
#else
    return false;
#endif
}
QString DesktopIntentModel::inspectionText() const
{
#ifdef ENABLE_WALLET
    if (m_inspection) {
        if (!m_inspection->error().isEmpty()) return m_inspection->error();
        if (m_inspection->pending()) return tr("Inspecting the desktop request…");
        if (m_inspection->sourceKind() == PaymentImportOperation::SourceKind::PaymentSource) {
            QStringList fields{m_inspection->address()};
            if (!m_inspection->publicLabel().isEmpty()) fields.push_back(m_inspection->publicLabel());
            if (m_inspection->hasAmount()) fields.push_back(tr("Amount: %1 satoshis").arg(QString::number(m_inspection->amount())));
            if (!m_inspection->message().isEmpty()) fields.push_back(m_inspection->message());
            return fields.join(QLatin1Char('\n'));
        }
        if (m_inspection->sourceKind() == PaymentImportOperation::SourceKind::PsbtSource) return tr("A transaction is ready for inspection in a wallet.");
    }
#endif
    return {};
}
void DesktopIntentModel::installFileOpenHandler()
{
    if (!m_closed && QCoreApplication::instance()) QCoreApplication::instance()->installEventFilter(this);
}
bool DesktopIntentModel::eventFilter(QObject*, QEvent* event)
{
    if (event->type() != QEvent::FileOpen) return false;
    if (m_closed || !m_service || m_file_sequence == std::numeric_limits<uint64_t>::max()) return true;
    const auto& open = *static_cast<QFileOpenEvent*>(event);
    // Copy the native event payload only. Path validation and all file reads
    // belong to the Source worker; no QFileInfo/QDir/filesystem calls here.
    desktopintent::Input input;
    input.producer_key = QStringLiteral("file-open:") + QString::number(QCoreApplication::applicationPid()) + QLatin1Char(':') + QString::number(++m_file_sequence);
    input.origin = desktopintent::Origin::FileOpen;
    input.value = open.file();
    input.kind = desktopintent::Kind::File;
    if (input.value.isEmpty()) {
        input.value = open.url().toString(QUrl::FullyEncoded);
        if (input.value.startsWith(QLatin1String{"bitcoin:"}, Qt::CaseInsensitive)) input.kind = desktopintent::Kind::Text;
    }
    const QPointer<DesktopIntentModel> self{this};
    m_ingress_error.clear();
    const auto admitted = m_service->admit({std::move(input)}, {});
    if (!admitted && self && !self->m_closed) {
        self->m_ingress_error = tr("The request could not be opened. Try opening it again.");
        Q_EMIT self->changed();
    }
    return true;
}
void DesktopIntentModel::setWalletEnabled(bool enabled)
{
    if (m_closed || m_wallet_enabled == enabled) return;
    m_wallet_enabled = enabled;
    const QPointer<DesktopIntentModel> self{this};
    setReady(m_ready);
    if (self) Q_EMIT self->changed();
}
QString DesktopIntentModel::error() const
{
    if (m_closed) return {};
    if (!m_service) return tr("Desktop request services are unavailable.");
    if (!m_ingress_error.isEmpty()) return m_ingress_error;
    if (!m_wallet_enabled && m_snapshot.pending) return tr("Wallet support is unavailable. The request remains unapplied.");
    switch (m_snapshot.error) {
    case desktopintent::Error::None: return {};
    case desktopintent::Error::Invalid: return tr("The desktop request is invalid.");
    case desktopintent::Error::Capacity: return tr("The desktop request queue is full.");
    case desktopintent::Error::Closed: return tr("The application is stopping.");
    case desktopintent::Error::UnknownDelivery: return tr("The request may not have been handled. Check its status before continuing.");
    case desktopintent::Error::TargetUnavailable: return tr("The request cannot be opened right now. It is kept for later.");
    }
    return {};
}
void DesktopIntentModel::project(desktopintent::Snapshot snapshot)
{
    if (snapshot.sequence <= m_snapshot.sequence) return;
    const bool attention = snapshot.pending > m_snapshot.pending && !m_closed;
    m_snapshot = std::move(snapshot);
    const QPointer<DesktopIntentModel> self{this};
    Q_EMIT changed();
    if (self && attention) Q_EMIT self->requestAttention();
}
void DesktopIntentModel::setReady(bool ready)
{
    m_ready = ready && !m_closed;
    if (m_service && m_readiness < std::numeric_limits<uint64_t>::max())
        m_service->readiness(++m_readiness, m_ready && static_cast<bool>(m_sink));
}
void DesktopIntentModel::close()
{
    if (std::exchange(m_closed, true)) return;
    m_closed_gate->store(true);
    if (const auto owner = std::exchange(m_service, {})) owner->stop();
#ifdef ENABLE_WALLET
    if (m_inspection) m_inspection->close();
#endif
}
void DesktopIntentModel::whenDrained(std::function<void()> done)
{
    m_queue->whenDrained(std::move(done));
}
void DesktopIntentModel::inspectNext()
{
    if (!canInspect() || !m_service) return;
    setReady(m_ready); // Explicit retry after a known refusal; Unknown cannot enter this path.
    m_service->review(m_sink);
}
void DesktopIntentModel::dismiss()
{
    if (!canDismiss() || !m_service) return;
    m_ingress_error.clear();
    m_service->dismiss();
}
void DesktopIntentModel::reconcile() { if (canReconcile() && m_service) m_service->reconcile(); }

#ifdef ENABLE_WALLET
namespace {
struct Creation {
    std::shared_ptr<paymentimport::CreationReceipt> receipt{std::make_shared<paymentimport::CreationReceipt>()};
    QPointer<PaymentImportOperation> operation; // Access only on GUI.
};
struct Creations {
    std::mutex mutex;
    std::map<desktopintent::DeliveryKey, std::shared_ptr<Creation>> records;
};
desktopintent::InspectionReceipt ReadCreation(const Creation& creation)
{
    using State = paymentimport::CreationReceipt::State;
    switch (creation.receipt->state()) {
    case State::Accepted: return {desktopintent::Knowledge::Accepted, creation.receipt->source()};
    case State::NotAccepted: return {desktopintent::Knowledge::NotAccepted, 0};
    case State::Reserved:
    case State::Creating: return {};
    }
    return {};
}
}
void DesktopIntentModel::setFactory(BitcoinUriModel* factory)
{
    // A receiver is installed once; captured keys cannot target a replacement.
    if (m_closed || m_sink || !factory || !m_wallet_enabled) return;
    const QPointer<DesktopIntentModel> self{this}; const QPointer<BitcoinUriModel> original{factory};
    const auto records = std::make_shared<Creations>();
    const auto post = m_post;
    const auto closed = m_closed_gate;
    const auto available = [closed, gate = m_parent_closed] {
        const auto parent = gate->load();
        return !closed->load() && (!parent || !parent->load());
    };
    const auto weak_owner = std::weak_ptr<desktopintent::Service>{m_service};
    auto sink = std::make_shared<desktopintent::Sink>(); sink->identity = 1;
    sink->inspect = [self, original, records, post, available, weak_owner](auto delivery, auto done) {
        std::shared_ptr<Creation> creation;
        {
            std::lock_guard lock{records->mutex};
            if (const auto found = records->records.find(delivery.key); found != records->records.end()) {
                creation = found->second;
            } else {
                while (records->records.size() >= desktopintent::Queue::MAX_RECEIPTS) {
                    const auto oldest = records->records.begin();
                    if (ReadCreation(*oldest->second).knowledge == desktopintent::Knowledge::Unknown)
                        return execution::SubmitResult{{}, execution::Rejection::Capacity};
                    records->records.erase(oldest);
                }
                creation = std::make_shared<Creation>();
                records->records.emplace(delivery.key, creation);
            }
        }
        post([self, original, creation, delivery = std::move(delivery), done = std::move(done), available, weak_owner]() mutable {
            // Native admission may close after Domain posts this handoff while
            // the terminal GUI projection is still queued.
            if (!available() || !self || !original) creation->receipt->withdraw();
            if (creation->receipt->state() == paymentimport::CreationReceipt::State::Reserved) {
                try {
                    const paymentimport::Input input{delivery.input.kind == desktopintent::Kind::File
                        ? paymentimport::Input::Kind::File : paymentimport::Input::Kind::Text, delivery.input.value};
                    creation->operation = original->requestImport(input, {}, self, creation->receipt);
                } catch (...) { creation->receipt->notAccepted(); }
                if (self && creation->operation && available()) {
                    self->m_inspection = creation->operation;
                    self->m_inspection_ticket = delivery.key;
                    QObject::connect(creation->operation, &PaymentImportOperation::changed, self, &DesktopIntentModel::inspectionChanged);
                    QObject::connect(creation->operation, &QObject::destroyed, self, [self, weak_owner, key = delivery.key] {
                        if (!self || self->m_inspection_ticket != key) return;
                        self->m_inspection = nullptr; self->m_inspection_ticket.reset();
                        if (const auto owner = weak_owner.lock()) owner->releasePresentation(key);
                        Q_EMIT self->inspectionChanged(); if (self) Q_EMIT self->changed();
                    });
                    Q_EMIT self->inspectionChanged();
                    if (self) Q_EMIT self->changed();
                }
            }
            if ((!self || !creation->operation || !available()) && ReadCreation(*creation).knowledge == desktopintent::Knowledge::Accepted) {
                if (const auto owner = weak_owner.lock()) owner->releasePresentation(delivery.key);
            }
            done(ReadCreation(*creation));
        });
        return execution::SubmitResult{};
    };
    sink->query = [records](auto key, auto done) {
        std::shared_ptr<Creation> creation;
        { std::lock_guard lock{records->mutex}; const auto found = records->records.find(key);
          if (found != records->records.end()) creation = found->second; }
        done(creation ? ReadCreation(*creation) : desktopintent::InspectionReceipt{});
        return execution::SubmitResult{};
    };
    sink->retire = [records, post](auto key) {
        std::shared_ptr<Creation> creation;
        { std::lock_guard lock{records->mutex}; const auto found = records->records.find(key);
          if (found != records->records.end()) creation = found->second; }
        if (!creation) return;
        creation->receipt->withdraw();
        post([creation] { if (creation->operation) creation->operation->cancel(); });
    };
    m_sink = std::move(sink);
    if (m_service) m_service->registerReceiver(m_sink);
    setReady(m_ready);
}
PaymentImportOperation* DesktopIntentModel::takeInspection(QObject* receiver)
{
    if (m_closed || !receiver || !m_inspection) return nullptr;
    const QPointer<DesktopIntentModel> self{this};
    const QPointer<PaymentImportOperation> operation{m_inspection};
    const auto key = std::exchange(m_inspection_ticket, {}); const auto owner = m_service;
    m_inspection = nullptr;
    QObject::disconnect(operation, nullptr, this, nullptr);
    operation->setParent(receiver);
    if (key && owner) owner->releasePresentation(*key);
    if (self) { Q_EMIT self->inspectionChanged(); if (self) Q_EMIT self->changed(); }
    return operation;
}
void DesktopIntentModel::closeInspection()
{
    const QPointer<DesktopIntentModel> self{this};
    const QPointer<PaymentImportOperation> operation{m_inspection};
    const auto key = std::exchange(m_inspection_ticket, {}); const auto owner = m_service;
    m_inspection = nullptr;
    if (operation) QObject::disconnect(operation, nullptr, this, nullptr);
    if (operation) { operation->cancel(); if (operation) operation->deleteLater(); }
    if (key && owner) owner->releasePresentation(*key);
    if (self) { Q_EMIT self->inspectionChanged(); if (self) Q_EMIT self->changed(); }
}
#else
void DesktopIntentModel::closeInspection() {}
#endif
