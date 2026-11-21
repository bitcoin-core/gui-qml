// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/walletreceiveadapter.h>
#include <qml/guipresentation.h>
#include <qml/retainedsnapshot.h>

#include <QClipboard>
#include <QGuiApplication>

namespace {
requestedit::Source Source(walletrequests::Inspection value, uint64_t view)
{
    auto source = requestedit::PrepareSource(value.record, view);
    source.loaded = value.loaded; source.current = value.current; source.closed = value.closed; source.used = value.used;
    if (value.error != walletrequests::Error::None) source.error = value.error;
    source.permission = std::move(value.permission);
    source.current_label = std::move(value.current_label);
    source.reservation = std::move(value.reservation);
    if (!value.record && value.address) {
        source.address = value.address->address; source.address_type = value.address->address_type;
        if (value.address->label.size() <= 16'384) source.values[requestedit::Note].text = value.address->label;
        else source.error = walletrequests::Error::Invalid;
    }
    if (source.error == walletrequests::Error::None && source.exists && !source.used)
        source.uri = walletrequests::BuildBitcoinUri(source.address, source.values[requestedit::Amount].amount,
            source.values[requestedit::Label].text, source.values[requestedit::Message].text);
    return source;
}
}
WalletReceiveModel::Ports MakeReceivePorts(const std::shared_ptr<walletopening::Session>& opening,
    const std::shared_ptr<walletrequests::Store>& store, execution::SnapshotServices execution,
    std::shared_ptr<backup::InstallCoordinator> coordinator)
{
    WalletReceiveModel::Ports ports;
    execution.post = presentation::ReliablePost(std::move(execution.post));
    const auto weak_store = std::weak_ptr<walletrequests::Store>{store};
    const auto weak_opening = std::weak_ptr<walletopening::Session>{opening};
    ports.editor.execution = execution;
    ports.editor.read = [weak_store](int64_t id, uint64_t view, auto done) {
        const auto writer = weak_store.lock();
        if (!writer) return execution::Admission{{}, execution::Rejection::Closed};
        return writer->inspect(id, view, [done = std::move(done), view](walletrequests::Inspection value) { done(Source(std::move(value), view)); });
    };
    ports.editor.admitWrite = [weak_store, weak_opening]() -> PaymentRequestEditorModel::Writer {
        const auto writer = weak_store.lock(); const auto owner = weak_opening.lock();
        const auto original = owner ? owner->startAction() : nullptr;
        if (!writer || !original) return {};
        return [writer, original](const requestedit::Intent& intent, auto done, const auto&) {
            walletrequests::Command command;
            command.kind = intent.erase ? walletrequests::Kind::Erase : intent.source.id > 0 ? walletrequests::Kind::Edit : walletrequests::Kind::Create;
            command.id = intent.source.id; command.revision = intent.source.revision; command.view = intent.source.view;
            command.reservation = intent.source.reservation;
            command.require_unused = command.consume_address = intent.source.id == 0;
            command.preserve_current_note = intent.edit.values[requestedit::Note] == intent.edit.base[requestedit::Note];
            if (!intent.erase && !command.preserve_current_note) {
                if (!intent.source.current_label) return execution::Admission{{}, execution::Rejection::Invalid};
                command.expected_label = intent.source.current_label;
            }
            command.draft = {intent.source.address, intent.edit.values[requestedit::Label].text,
                intent.edit.values[requestedit::Message].text, intent.edit.values[requestedit::Note].text,
                intent.source.address_type, intent.edit.values[requestedit::Amount].amount};
            const auto admission = writer->submitResult(std::move(command), [done = std::move(done)](const walletrequests::Result& actual) {
                done({actual, {}});
            }, original);
            return execution::Admission{{}, admission.rejection};
        };
    };
    ports.reservation.execution = execution;
    const auto submit = [weak_store, scheduler = execution.scheduler](walletrequests::Command command, auto done) {
        const auto writer = weak_store.lock();
        if (!writer || !scheduler) return ReceiveReservationModel::Admission{{}, {}, execution::Rejection::Closed};
        // Reserve destruction before retaining a potentially large original
        // result. GUI aliases own only its cleanup lease, never final storage.
        auto retained = execution::RetainSnapshot<std::shared_ptr<walletrequests::Operation>>(scheduler,
            [] { return std::make_unique<std::shared_ptr<walletrequests::Operation>>(); });
        if (!retained) return ReceiveReservationModel::Admission{{}, {}, execution::Rejection::Closed};
        const auto admission = writer->submitResult(std::move(command), std::move(done));
        *retained = admission.operation;
        return ReceiveReservationModel::Admission{admission.operation ? admission.operation->address_intent : nullptr,
            std::move(retained), admission.rejection};
    };
    ports.reservation.submit = submit;
    ports.reservation.authorize = [submit, scheduler = execution.scheduler](walletrequests::Command command, const QString& text, auto done) {
        auto captured = secretinput::Capture(scheduler, text);
        command.captured_secret = std::move(captured.input); command.secret_capture_error = captured.error;
        return submit(std::move(command), std::move(done));
    };
    ports.sharing.execution = execution;
    ports.sharing.read = [weak_store, identity = opening->identity()](int64_t id, auto done) {
        const auto writer = weak_store.lock();
        if (!writer) return execution::Admission{{}, execution::Rejection::Closed};
        return writer->inspect(id, 0, [done = std::move(done), identity, id](walletrequests::Inspection value) {
            requestshare::Source source;
            source.opening = identity; source.request_id = id; source.revision = value.revision;
            source.receipt_epoch = value.receipt_epoch; source.native_revision = value.native_revision;
            source.current = std::move(value.permission);
            if (value.closed) source.error = requestshare::Error::Closed;
            else if (!value.loaded || !value.current) source.error = requestshare::Error::Changed;
            else if (!value.record) source.error = requestshare::Error::Missing;
            else if (value.used) source.error = requestshare::Error::Used;
            else {
                const auto prepared = requestedit::PrepareSource(value.record, 0);
                source.record_revision = value.record->revision;
                if (prepared.error != walletrequests::Error::None) source.error = requestshare::Error::Oversize;
                else source.payload = walletrequests::BuildBitcoinUri(prepared.address, prepared.values[requestedit::Amount].amount,
                    prepared.values[requestedit::Label].text, prepared.values[requestedit::Message].text);
            }
            done(std::move(source));
        });
    };
    ports.sharing.render = requestshare::RenderQr;
    ports.sharing.deliver = [](requestshare::Kind kind, const QString& payload, const QImage& pixels) {
        auto* clipboard = QGuiApplication::clipboard();
        if (!clipboard) return requestshare::Delivery::Failed;
        if (kind == requestshare::Kind::Uri) clipboard->setText(payload);
        else clipboard->setImage(pixels);
        return requestshare::Delivery::Accepted;
    };
    auto files = std::make_shared<requestimageexport::Services>();
    files->inspect = [](const QUrl& url) {
        const auto bytes = url.toString(QUrl::FullyEncoded).toUtf8();
        return backup::InspectPng({bytes.constData(), static_cast<size_t>(bytes.size())});
    };
    files->write = [coordinator = std::move(coordinator)](const backup::Proposal& target, bool approved,
        const QImage& image, const std::shared_ptr<requestshare::Grant>& grant) {
        if (!coordinator || !grant || image.isNull()) return backup::WriteResult{backup::Error::Transport};
        return backup::Write(target, approved, [image](const std::string& path) {
            return image.save(QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size())), "PNG");
        }, *coordinator, [grant] { return grant->admit(); });
    };
    ports.sharing.files = std::move(files);
    return ports;
}
