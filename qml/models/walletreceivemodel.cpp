// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/walletreceivemodel.h>
#include <qml/guipresentation.h>

#include <QQmlEngine>

#include <algorithm>

namespace {
std::optional<int64_t> RequestId(const QString& id)
{
    if (id.isEmpty() || id.size() > 20) return {};
    bool okay{false}; const auto value = id.toLongLong(&okay);
    if (!okay || value <= 0 || QString::number(value) != id) return {};
    return value;
}
}
WalletReceiveModel::WalletReceiveModel(uint64_t view, Ports ports, QObject* parent)
    : QObject(parent), m_view(view), m_ports(std::move(ports)), m_reservation(view, m_ports.reservation)
{
    m_ports.editor.post = presentation::ReliablePost(std::move(m_ports.editor.post));
    m_editor = makeEditor(0);
    if (m_editor) m_editor->setParent(this);
    connect(&m_reservation, &AddressReservationModel::changed, this, [this] {
        const QPointer<WalletReceiveModel> guard{this};
        if (m_editor) m_editor->reservationChanged();
        if (!guard) return;
        refresh(); if (!guard) return;
        if (m_pending && m_pending->cancelling) {
            if (m_reservation.phase() == AddressReservationModel::Cancelled) { applyReplacement(); return; }
            if (m_reservation.phase() == AddressReservationModel::Failed) {
                m_error = m_reservation.error(); m_pending.reset(); Q_EMIT changed(); return;
            }
        }
        Q_EMIT changed();
    });
    connect(&m_reservation, &AddressReservationModel::requestSelected, this, &WalletReceiveModel::existingRequestRequested);
}
WalletReceiveModel::~WalletReceiveModel()
{
    retire();
    // QML can collect retired projections earlier; the bounded weak inventory
    // is not a second lifetime owner. C++ consumers have the same root teardown.
    const auto editors = std::exchange(m_editors, {});
    const auto contexts = std::exchange(m_sharing, {}); m_sharing_views.clear();
    for (const auto& context : contexts) if (context) delete context.data();
    for (const auto& editor : editors) if (editor) delete editor.data();
}
bool WalletReceiveModel::refuse(const QString& command, TextEditError error)
{
    m_error = error; const QPointer<WalletReceiveModel> guard{this};
    Q_EMIT changed(); if (guard) Q_EMIT commandRejected(command, error); return false;
}
bool WalletReceiveModel::owns(PaymentRequestEditorModel* editor) const
{
    return editor && std::any_of(m_editors.begin(), m_editors.end(), [editor](const auto& value) { return value == editor; });
}
PaymentRequestEditorModel* WalletReceiveModel::makeEditor(int64_t id, bool erase, std::optional<requestedit::Editor::Values> initial)
{
    if (m_closed) return nullptr;
    m_editors.removeIf([](const auto& editor) { return editor.isNull(); });
    if (m_closed || m_editors.size() >= 16) { refuse(QStringLiteral("editor"), {TextEditError::Refused, tr("Close another request editor before opening a new one.")}); return nullptr; }
    auto* editor = new PaymentRequestEditorModel(id, m_view, erase, m_ports.editor, std::move(initial), this);
    editor->setReceiptReconciler(m_ports.receipts);
    QQmlEngine::setObjectOwnership(editor, QQmlEngine::JavaScriptOwnership);
    m_editors.push_back(editor);
    connect(editor, &PaymentRequestEditorModel::persisted, this, &WalletReceiveModel::requestPersisted);
    connect(editor, &PaymentRequestEditorModel::closed, this, [this, editor](const QString& original) {
        editor->setParent(nullptr);
        if (!m_pending || !m_pending->closing || m_pending->original_editor != original || m_editor != editor) return;
        m_pending->closing = false; m_pending->cancelling = true;
        if (m_reservation.phase() == AddressReservationModel::Cancelled) applyReplacement(); else m_reservation.cancel();
    });
    connect(editor, &PaymentRequestEditorModel::keptEditing, this, [this, editor, erase](const QString& original, const QString&) {
        if (m_pending && m_pending->original_editor == original) { m_pending.reset(); m_error = {}; Q_EMIT changed(); }
        if (erase) editor->retire();
    });
    return editor;
}
bool WalletReceiveModel::ensureAddress()
{
    if (m_closed || replacementPending() || !m_editor) return false;
    if (m_editor->mutationKind() != PaymentRequestEditorModel::Create) return true;
    if (m_reservation.phase() == AddressReservationModel::Ready || m_reservation.busy() || m_reservation.prompt()) return true;
    // Observation or reopening a view cannot replay a cancelled/failed
    // request, nor silently rotate a used reservation under composed fields.
    if (m_reservation.phase() != AddressReservationModel::Idle || m_reservation.historicallyUsed() || !m_reservation.selectedRequestId().isEmpty()) return false;
    return m_reservation.ensure(false, {});
}
bool WalletReceiveModel::nextAddress()
{
    if (m_closed || replacementPending() || !m_editor || m_editor->mutationKind() != PaymentRequestEditorModel::Create || m_editor->busy())
        return refuse(QStringLiteral("nextAddress"), {TextEditError::Refused, tr("Finish the current request operation before choosing another address.")});
    return m_reservation.ensure(true, m_reservation.addressType(), true, false);
}
bool WalletReceiveModel::chooseAddressType(const QString& type)
{
    if (m_closed || replacementPending() || !m_editor || m_editor->mutationKind() != PaymentRequestEditorModel::Create || m_editor->busy()) return false;
    // This explicit preference command is the only type choice that persists.
    return m_reservation.ensure(false, type, true, true);
}
bool WalletReceiveModel::begin(Replacement target)
{
    if (m_closed || m_pending || m_repeat || m_token == UINT64_MAX || !m_editor || m_editor->busy())
        return refuse(QStringLiteral("replace"), {TextEditError::Refused, tr("Answer the open question about this request first.")});
    target.token = ++m_token; target.original_editor = m_editor->editorId();
    m_pending = std::move(target); m_error = {};
    if (!m_pending->reading) askToReplace();
    Q_EMIT changed(); return true;
}
void WalletReceiveModel::askToReplace()
{
    if (!m_pending || !m_editor || m_editor->editorId() != m_pending->original_editor) return;
    m_pending->closing = true;
    if (m_editor->phase() == PaymentRequestEditorModel::Closed) {
        m_pending->closing = false; m_pending->cancelling = true;
        if (m_reservation.phase() == AddressReservationModel::Cancelled) applyReplacement(); else m_reservation.cancel();
    } else m_editor->requestClose();
}
bool WalletReceiveModel::newRequest() { return begin({}); }
bool WalletReceiveModel::requestAgain(PaymentRequestEditorModel* original)
{
    if (m_closed || m_pending || m_repeat || !owns(original) || !original->available() || original->busy() || original->requestId().isEmpty() || m_token == UINT64_MAX)
        return refuse(QStringLiteral("requestAgain"), {TextEditError::Refused, tr("Wait for the current request to finish before requesting again.")});
    if (original == m_editor) return useTemplate(original->requestId());
    const auto token = ++m_token;
    m_repeat = Repeat{original->editorId(), original->requestId(), token};
    m_repeat_closed = connect(original, &PaymentRequestEditorModel::closed, this, [this, token](const QString& editor) {
        if (!m_repeat || m_repeat->token != token || m_repeat->editor != editor) return;
        const auto request = m_repeat->request; clearRepeat(); useTemplate(request);
    });
    m_repeat_kept = connect(original, &PaymentRequestEditorModel::keptEditing, this, [this, token](const QString& editor, const QString&) {
        if (m_repeat && m_repeat->token == token && m_repeat->editor == editor) { clearRepeat(); Q_EMIT changed(); }
    });
    original->requestClose(); Q_EMIT changed(); return true;
}
void WalletReceiveModel::clearRepeat()
{
    m_repeat.reset(); disconnect(m_repeat_closed); disconnect(m_repeat_kept);
    m_repeat_closed = {}; m_repeat_kept = {};
}
bool WalletReceiveModel::useTemplate(const QString& id)
{
    const auto parsed = RequestId(id);
    if (!parsed) return refuse(QStringLiteral("template"), {TextEditError::Invalid, tr("Choose an existing saved request.")});
    Replacement target; target.kind = Target::Template; target.reading = true; target.source = *parsed;
    if (!begin(std::move(target))) return false;
    const auto token = m_pending->token;
    readTemplate();
    return m_pending && m_pending->token == token;
}
void WalletReceiveModel::readTemplate()
{
    m_pending->waiting = m_pending->refresh_pending = false;
    const auto token = m_pending->token; const QPointer<WalletReceiveModel> observer{this};
    const auto post = m_ports.editor.post;
    const auto complete = [observer, post, token](requestedit::Source source) {
        post([observer, token, source = std::move(source)] {
            if (!observer || !observer->m_pending || observer->m_pending->token != token) return;
            if (!source.closed && source.error == walletrequests::Error::None && (!source.loaded || !source.current)) {
                // The store is busy. Await its next observation, as an open does.
                observer->m_pending->waiting = true;
                if (observer->m_pending->refresh_pending) observer->readTemplate();
                return;
            }
            if (!source.exists || source.closed || source.error != walletrequests::Error::None) {
                observer->m_pending.reset(); observer->refuse(QStringLiteral("template"), {TextEditError::Changed, tr("This saved request is no longer available.")}); return;
            }
            observer->m_pending->values = source.values; observer->m_pending->type = source.address_type;
            observer->m_pending->reading = false; observer->askToReplace();
        });
    };
    if (m_ports.editor.read && m_ports.editor.read(m_pending->source, m_view, complete)) return;
    m_pending.reset(); refuse(QStringLiteral("template"), {TextEditError::Refused, tr("The saved request could not be read.")});
}
bool WalletReceiveModel::useAddress(const QString& address)
{
    if (address.isEmpty() || address.size() > 128) return refuse(QStringLiteral("address"), {TextEditError::Invalid, tr("Choose a valid receiving address.")});
    if (m_editor && m_editor->mutationKind() == PaymentRequestEditorModel::Create && m_editor->address() == address) {
        return true;
    }
    Replacement target; target.kind = Target::Address; target.address = address;
    return begin(std::move(target));
}
void WalletReceiveModel::applyReplacement()
{
    if (!m_pending || !m_pending->cancelling || m_closed) return;
    const auto target = *m_pending;
    auto* editor = makeEditor(0, false, target.values);
    if (!editor) { m_pending.reset(); return; }
    m_pending.reset();
    if (m_editor) m_editor->setParent(nullptr);
    m_editor = editor; editor->setParent(this); m_error = {};
    const QPointer<WalletReceiveModel> guard{this};
    Q_EMIT changed(); if (!guard) return;
    if (target.kind == Target::Address) m_reservation.select(target.address);
    else m_reservation.ensure(target.kind == Target::Template, target.type,
        target.kind == Target::Fresh || !target.type.isEmpty(), false);
}
PaymentRequestOpenOperation* WalletReceiveModel::openRequest(const QString& id, QObject* owner)
{
    const auto parsed = RequestId(id);
    if (!parsed || m_closed || m_open || !m_ports.editor.read) {
        refuse(QStringLiteral("open"), {TextEditError::Refused, tr("This request could not be opened. Try again.")}); return nullptr;
    }
    auto* operation = new PaymentRequestOpenOperation(QString::number(*parsed), owner);
    m_open = operation; m_open_waiting = m_open_refresh_pending = false; m_open_id = *parsed;
    // A cancelled or deleted open stops being followed.
    connect(operation, &PaymentRequestOpenOperation::changed, this, [this, operation] { if (m_open == operation && !operation->pending()) endOpen(); });
    connect(operation, &QObject::destroyed, this, [this] { if (!m_open) endOpen(); });
    const QPointer<PaymentRequestOpenOperation> guard{operation};
    readOpenRequest();
    return guard;
}
void WalletReceiveModel::endOpen()
{
    m_open = nullptr; m_open_waiting = false; m_open_id = 0;
    m_open_refresh_pending = false; m_open_editor = nullptr; disconnect(m_open_ready);
}
void WalletReceiveModel::failOpen(const TextEditError& error)
{
    const QPointer<PaymentRequestOpenOperation> operation{m_open};
    endOpen();
    if (operation) operation->finish(PaymentRequestOpenOperation::State::Failed, nullptr, error);
}
void WalletReceiveModel::readOpenRequest()
{
    if (!m_open || m_closed) return;
    m_open_waiting = false;
    m_open_refresh_pending = false;
    const auto id = m_open_id; const QPointer<PaymentRequestOpenOperation> operation{m_open};
    const QPointer<WalletReceiveModel> observer{this}; const auto post = m_ports.editor.post;
    const auto complete = [observer, post, id, operation](requestedit::Source source) {
        post([observer, id, operation, source = std::move(source)] {
            if (!observer || observer->m_closed || !operation || observer->m_open != operation) return;
            if (!source.closed && source.error == walletrequests::Error::None && (!source.loaded || !source.current || (source.permission && !source.permission()))) {
                // Await a producer's next observation; never poll or repeat a
                // Core action. The open keeps its original view.
                observer->m_open_waiting = true;
                if (observer->m_open_refresh_pending) observer->readOpenRequest();
                return;
            }
            if (!source.exists || source.closed || source.error != walletrequests::Error::None) {
                observer->failOpen({source.closed ? TextEditError::Closed : source.error == walletrequests::Error::Uncertain ? TextEditError::Unknown :
                    source.error == walletrequests::Error::Rejected ? TextEditError::Refused : TextEditError::Changed,
                    source.error == walletrequests::Error::None ? tr("This saved request is no longer available.") : walletrequests::ErrorText(source.error)});
                return;
            }
            PaymentRequestEditorModel* editor{nullptr};
            for (const auto& held : observer->m_editors)
                if (held && held->available() && held->mutationKind() != PaymentRequestEditorModel::Delete && held->requestId() == QString::number(id)) { editor = held; break; }
            if (!editor) editor = observer->makeEditor(id);
            if (!editor) {
                if (observer) observer->failOpen(observer->m_error);
                return;
            }
            const auto& guard = observer;
            const QPointer<PaymentRequestEditorModel> original{editor};
            observer->m_open_editor = editor;
            observer->m_open_ready = connect(editor, &PaymentRequestEditorModel::changed, observer, [observer, original] {
                if (observer && original) observer->completeOpenRequest(original);
            });
            editor->refresh(); if (!guard || !original) return;
            observer->completeOpenRequest(editor);
        });
    };
    if (!m_ports.editor.read(id, m_view, complete)) {
        const TextEditError error{TextEditError::Refused, tr("The saved request could not be read.")};
        m_error = error;
        failOpen(error);
    }
}
void WalletReceiveModel::completeOpenRequest(PaymentRequestEditorModel* editor)
{
    if (!m_open || m_closed || m_open_editor != editor) return;
    if (editor->phase() == PaymentRequestEditorModel::Loading) return;
    if (!editor->available() || editor->requestId() != QString::number(m_open_id)) {
        const TextEditError error{TextEditError::Changed, tr("This saved request is no longer available.")};
        m_error = error;
        failOpen(error);
        return;
    }
    // The real facade must own its exact source projection before the open
    // finishes. A new facade is initially still Loading.
    const QPointer<PaymentRequestOpenOperation> operation{m_open};
    endOpen();
    if (operation) operation->finish(PaymentRequestOpenOperation::State::Opened, editor);
}
PaymentRequestEditorModel* WalletReceiveModel::deleteRequest(const QString& id)
{
    const auto parsed = RequestId(id); return parsed ? makeEditor(*parsed, true) : nullptr;
}
RequestSharingModel* WalletReceiveModel::sharing(PaymentRequestEditorModel* editor, QObject* view)
{
    const auto parsed = editor ? RequestId(editor->requestId()) : std::nullopt;
    m_sharing.removeIf([](const auto& context) { return context.isNull(); });
    m_sharing_views.removeIf([](const auto& value) { return value.context.isNull(); });
    if (view) for (auto& old : m_sharing_views) if (old.view == view) {
        if (old.editor == editor && old.id == (editor ? editor->requestId() : QString{}) &&
            old.context && old.context->phase() != RequestSharingModel::Closed) return old.context;
        if (old.context) { old.context->retire(); old.context->setParent(nullptr); }
        old.view = nullptr;
    }
    if (m_closed || !parsed || !owns(editor) || m_sharing.size() >= 16) return nullptr;
    auto* context = new RequestSharingModel(*parsed, m_ports.sharing, editor->sharingInputGuard(), view, editor->sharingInputEpoch());
    QQmlEngine::setObjectOwnership(context, QQmlEngine::JavaScriptOwnership);
    m_sharing.push_back(context);
    if (view) m_sharing_views.push_back({view, editor, context, editor->requestId()});
    connect(editor, &PaymentRequestEditorModel::changed, context, &RequestSharingModel::refresh);
    connect(editor, &QObject::destroyed, context, &RequestSharingModel::retire);
    return context;
}
RequestSharingModel* WalletReceiveModel::sharingRequest(const QString& id, QObject* view)
{
    const auto parsed = RequestId(id);
    if (m_closed || !parsed || !view) return nullptr;
    m_sharing.removeIf([](const auto& context) { return context.isNull(); });
    m_sharing_views.removeIf([](const auto& value) { return value.context.isNull(); });
    for (auto& old : m_sharing_views) if (old.view == view) {
        if (!old.editor && old.id == id && old.context && old.context->phase() != RequestSharingModel::Closed) return old.context;
        if (old.context) { old.context->retire(); old.context->setParent(nullptr); }
        old.view = nullptr;
    }
    if (m_sharing.size() >= 16) return nullptr;
    auto* context = new RequestSharingModel(*parsed, m_ports.sharing, {}, view);
    QQmlEngine::setObjectOwnership(context, QQmlEngine::JavaScriptOwnership);
    m_sharing.push_back(context); m_sharing_views.push_back({view, {}, context, id});
    return context;
}
void WalletReceiveModel::refresh()
{
    if (m_closed) return;
    if (m_open && !m_open_editor) {
        if (m_open_waiting) readOpenRequest(); else m_open_refresh_pending = true;
    }
    if (m_pending && m_pending->reading) {
        const QPointer<WalletReceiveModel> guard{this};
        if (m_pending->waiting) readTemplate(); else m_pending->refresh_pending = true;
        if (!guard || m_closed) return;
    }
    observeReservation();
    for (const auto& editor : m_editors) if (editor) editor->refresh();
    for (const auto& context : m_sharing) if (context) context->refresh();
}
void WalletReceiveModel::observeReservation()
{
    if (m_observing) { m_observe_again = true; return; }
    if (!m_ports.editor.read) return;
    m_observing = true;
    const auto intent = m_reservation.intent(); const QPointer<WalletReceiveModel> observer{this};
    const auto post = m_ports.editor.post;
    const auto complete = [observer, intent, post](requestedit::Source source) {
        post([observer, intent, source = std::move(source)] {
            if (!observer || observer->m_closed) return;
            observer->m_observing = false;
            const bool again = std::exchange(observer->m_observe_again, false);
            // While a new address is generated, the shown address is the old
            // one: only a source for the new attempt describes it.
            const bool same = observer->m_reservation.intent() == intent &&
                (source.reservation == intent || (!observer->m_reservation.busy() && source.address == observer->m_reservation.address()));
            const auto& guard = observer;
            if (same) observer->m_reservation.observe(source.used, source.current && source.reservation == intent);
            if (guard && again) guard->observeReservation();
        });
    };
    if (!m_ports.editor.read(0, m_view, complete)) m_observing = false;
}
void WalletReceiveModel::retire()
{
    if (std::exchange(m_closed, true)) return;
    m_pending.reset(); clearRepeat(); if (const QPointer<PaymentRequestOpenOperation> open{m_open}) { endOpen(); if (open) open->finish(PaymentRequestOpenOperation::State::Cancelled); } m_reservation.retire();
    const auto editors = m_editors; const auto contexts = m_sharing;
    for (const auto& editor : editors) if (editor) editor->retire();
    for (const auto& context : contexts) if (context) context->retire();
    Q_EMIT changed();
}
