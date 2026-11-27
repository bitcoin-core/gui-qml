// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_TEST_MOCKS_RECEIVEEDITORTEST_H
#define BITCOIN_QML_TEST_MOCKS_RECEIVEEDITORTEST_H
#include <qml/models/walletreceivemodel.h>
#include <qml/sourceepoch.h>
#include <test/mocks/manualstartupexecution.h>

#include <deque>
#include <map>

/** Controlled public value ports, not a substitute request/editor reducer.
 * Production models make every validation, conflict, close and outcome choice.
 * This fixture records original effects and supplies explicit storage facts. */
struct ReceiveEditorPorts {
    using Source = requestedit::Source;
    using Values = requestedit::Editor::Values;
    using Error = walletrequests::Error;
    ManualStartupExecution execution;
    const std::shared_ptr<execution::ValueCleanupQueue> outcomes{std::make_shared<execution::ValueCleanupQueue>(
        std::make_shared<execution::SerialQueue>(execution.executor().makeQueue(execution::Lane::Utility)))};
    ManualStartupExecution editor_work;
    const std::shared_ptr<execution::ValueCleanupQueue> editor_values{std::make_shared<execution::ValueCleanupQueue>(
        std::make_shared<execution::SerialQueue>(editor_work.executor().makeQueue(execution::Lane::Utility)))};
    bool accepting_edits{true};
    std::deque<std::function<void()>> gui;
    const walletopening::Opening opening{std::make_shared<const walletopening::Identity>()};
    const std::shared_ptr<ChangeCounter> epoch{std::make_shared<ChangeCounter>()};
    std::map<int64_t, Source> records;
    Source draft;
    std::shared_ptr<const walletrequests::AddressIntent> reservation;
    uint64_t token{0};
    int reads{0}, writes{0}, generations{0}, authorizations{0}, preferences{0}, clipboard{0}, file_writes{0}, inspections{0};
    Error next_write{Error::None}, next_generation{Error::None};
    bool save_request{true}, save_label{true}, preference_saves{true}, closed{false}, target_exists{false}, target_changed{false};
    bool label_conflict_after_write{false};
    QString clipboard_text, written_path;
    QImage clipboard_image, written_image;
    requestshare::Delivery delivery{requestshare::Delivery::Accepted};
    std::vector<requestedit::Intent> attempts;
    std::string target_version{"original-version"};
    ReceiveEditorPorts()
    {
        draft.view = 2; draft.loaded = draft.current = true;
        Source saved; saved.id = 17; saved.revision = 1; saved.loaded = saved.current = saved.exists = true; saved.view = 2;
        saved.address = QStringLiteral("bcrt1q-original"); saved.address_type = QStringLiteral("bech32");
        saved.values[requestedit::Label].text = QStringLiteral("original label");
        saved.values[requestedit::Message].text = QStringLiteral("original message");
        saved.values[requestedit::Note].text = QStringLiteral("original private note");
        saved.values[requestedit::Amount].amount = 100;
        records.emplace(17, saved);
    }
    Source snapshot(int64_t id, uint64_t view)
    {
        ++reads;
        auto value = id == 0 ? draft : records.contains(id) ? records.at(id) : Source{};
        value.view = view; value.loaded = true; value.closed = closed;
        if (id > 0 && !records.contains(id)) { value.id = id; value.current = true; }
        const auto captured = epoch->value.load();
        value.permission = [epoch = epoch, captured] { return epoch->current(captured); };
        return value;
    }
    static walletrequests::SharedRecord record(const Source& source)
    {
        walletrequests::Record result;
        result.entry.id = source.id; result.revision = source.revision;
        result.entry.recipient.address = source.address.toStdString();
        result.entry.recipient.label = source.values[requestedit::Label].text.toStdString();
        result.entry.recipient.message = source.values[requestedit::Message].text.toStdString();
        result.entry.recipient.noteSelf = source.values[requestedit::Note].text.toStdString();
        result.entry.recipient.amount = source.values[requestedit::Amount].amount;
        result.entry.payment_received = source.used;
        result.display.address = source.address; result.display.address_type = source.address_type;
        result.display.label = source.values[requestedit::Label].text; result.display.message = source.values[requestedit::Message].text;
        result.display.note = source.values[requestedit::Note].text;
        return std::make_shared<const walletrequests::Record>(std::move(result));
    }
    PaymentRequestEditorModel::Ports editors()
    {
        PaymentRequestEditorModel::Ports ports;
        ports.post = [this](auto turn) { gui.push_back(std::move(turn)); };
        ports.postWork = [this](auto work) { return editor_work.outcomes.post(std::move(work)); };
        ports.value_cleanup = [this](auto cleanup) {
            return editor_values->hold(std::move(cleanup));
        };
        ports.read = [this](int64_t id, uint64_t view, auto done) {
            const bool accepted = editor_work.outcomes.post([this, id, view, done = std::move(done)] {
                done(snapshot(id, view));
            });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        };
        ports.admitWrite = [this]() -> PaymentRequestEditorModel::Writer {
            if (!accepting_edits || closed) return {};
            return [this](const requestedit::Intent& intent, auto done) {
                const bool accepted = editor_work.core.post([this, intent, done = std::move(done)] {
                    ++writes; attempts.push_back(intent);
                    requestedit::Write receipt;
                    receipt.actual.id = intent.source.id > 0 ? intent.source.id : 47;
                    receipt.actual.error = std::exchange(next_write, Error::None);
                    if (receipt.actual.error == Error::None || receipt.actual.error == Error::LabelPersistence || receipt.actual.error == Error::Uncertain ||
                        (receipt.actual.error == Error::Changed && label_conflict_after_write)) {
                        receipt.actual.request_saved = save_request && !intent.erase;
                        receipt.actual.erased = save_request && intent.erase;
                        receipt.actual.label_saved = save_label && !intent.erase;
                        if (receipt.actual.erased) records.erase(intent.source.id);
                        else if (receipt.actual.request_saved) {
                            auto saved = intent.source; saved.id = receipt.actual.id; ++saved.revision;
                            saved.exists = saved.current = saved.loaded = true;
                            saved.values = intent.edit.values; saved.values[requestedit::Mutation].intent = false;
                            saved.reservation.reset(); records[saved.id] = saved;
                            receipt.actual.record = record(saved);
                        }
                        epoch->advance();
                    }
                    const bool admitted = editor_work.outcomes.post([done, receipt = std::move(receipt)] {
                        done(receipt);
                    });
                    Q_ASSERT(admitted);
                });
                return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
            };
        };
        return ports;
    }
    AddressReservationModel::Ports reservations()
    {
        AddressReservationModel::Ports ports;
        ports.post = [this](auto turn) { gui.push_back(std::move(turn)); };
        const auto submit = [this](walletrequests::Command command, std::function<void(const walletrequests::Result&)> done, bool authorized) {
            if (closed) return AddressReservationModel::Admission{{}, {}, execution::Rejection::Closed};
            auto intent = command.resume;
            if (!intent) intent = std::make_shared<const walletrequests::AddressIntent>(walletrequests::AddressIntent{
                opening, ++token, command.view, command.view_revision, command.draft, command.next, command.explicit_type, false,
                command.kind == walletrequests::Kind::SelectAddress, command.remember_type});
            auto task = execution::MakeTask( [this, command, intent, authorized] {
                walletrequests::Result result;
                if (command.kind == walletrequests::Kind::ForgetAddress) {
                    reservation.reset(); draft.reservation.reset(); epoch->advance(); return result;
                }
                if (intent->revoked->load()) { result.error = Error::Changed; return result; }
                if (authorized) ++authorizations;
                result.error = std::exchange(next_generation, Error::None);
                if (result.error != Error::None) return result;
                QString type = command.draft.address_type;
                if (command.resume) type = command.resume->draft.address_type;
                if (type.isEmpty()) type = QStringLiteral("bech32");
                if (command.kind == walletrequests::Kind::SelectAddress && command.draft.address == records.at(17).address) {
                    result.request_loaded = true; result.id = 17; result.record = record(records.at(17)); return result;
                }
                QString address = draft.address;
                if (command.kind == walletrequests::Kind::SelectAddress) address = command.draft.address;
                else if (command.next || address.isEmpty() || draft.address_type != type) address = QStringLiteral("bcrt1q-generated-%1").arg(++generations);
                result.address = walletrequests::ReservedAddress{{}, address, type, {}, true};
                if (command.remember_type) {
                    ++preferences; result.preference_requested = true; result.preference_saved = preference_saves;
                    if (!preference_saves) result.error = Error::PreferencePersistence;
                }
                reservation = intent; draft.reservation = intent; draft.address = address; draft.address_type = type;
                draft.used = false; draft.current = true; epoch->advance();
                return result;
            }, [done = std::move(done)](walletrequests::Result result) { done(result); }, {});
            const auto submitted = outcomes->postWithOutcome(execution.core, std::move(task));
            return AddressReservationModel::Admission{intent, {}, submitted.rejection};
        };
        ports.submit = [submit](auto command, auto done) { return submit(std::move(command), std::move(done), false); };
        ports.authorize = [submit](auto command, const QString&, auto done) { return submit(std::move(command), std::move(done), true); };
        return ports;
    }
    requestshare::Services sharing()
    {
        requestshare::Services ports;
        ports.post = [this](auto turn) { gui.push_back(std::move(turn)); };
        ports.submit = [this](auto task, auto lane) { return outcomes->postWithOutcome(lane == execution::Lane::Core ? execution.core : execution.outcomes, std::move(task)); };
        ports.hold = [] { return std::make_shared<int>(); };
        ports.read = [this](int64_t id, auto done) {
            auto task = execution::MakeTask([this, id] {
                requestshare::Source result; result.opening = opening; result.request_id = id;
                const auto source = snapshot(id, 0); result.record_revision = source.revision; result.current = source.permission;
                if (closed) result.error = requestshare::Error::Closed;
                else if (!source.exists) result.error = requestshare::Error::Missing;
                else if (source.used) result.error = requestshare::Error::Used;
                else if (!source.current) result.error = requestshare::Error::Changed;
                // Opaque injected prepared payload. Production formatting and
                // independent QR decoding have separate Core/renderer oracles.
                else result.payload = QStringLiteral("bitcoin:bcrt1q-original?amount=0.000001&message=original%20message");
                return result;
            }, std::move(done), {});
            return outcomes->postWithOutcome(execution.outcomes, std::move(task));
        };
        ports.render = [](const QString&) { QImage image{21, 21, QImage::Format_RGB32}; image.fill(Qt::black); return image; };
        ports.deliver = [this](requestshare::Kind kind, const QString& text, const QImage& image) {
            ++clipboard;
            if (delivery == requestshare::Delivery::Accepted) { if (kind == requestshare::Kind::Uri) clipboard_text = text; else clipboard_image = image; }
            return delivery;
        };
        auto files = std::make_shared<requestimageexport::Services>();
        files->inspect = [this](const QUrl& url) {
            ++inspections;
            return backup::Proposal{url.toLocalFile().toStdString(), target_version, target_exists, false, backup::Error::None};
        };
        files->write = [this](const backup::Proposal& target, bool, const QImage& image, const std::shared_ptr<requestshare::Grant>& grant) {
            if (target_changed) return backup::WriteResult{backup::Error::Changed};
            if (!grant->admit()) return backup::WriteResult{backup::Error::Changed};
            ++file_writes; written_image = image; written_path = QString::fromStdString(target.path); return backup::WriteResult{};
        };
        ports.files = std::move(files); return ports;
    }
    WalletReceiveModel::Ports all() { return {editors(), reservations(), sharing()}; }
    void publish() { for (size_t turns{0}; !gui.empty() && turns < 1000; ++turns) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); } }
    void drainWorkers()
    {
        do {
            execution.drain();
            editor_work.drain();
        } while (!execution.quiescent() || !editor_work.quiescent());
    }
    void drain() { for (size_t turns{0}; turns < 1000; ++turns) { drainWorkers(); if (gui.empty()) return; publish(); } }
    void domain()
    {
        while (execution.step(execution::Lane::Utility) || editor_work.step(execution::Lane::Utility)) {}
        publish();
    }
    bool quiescent() { return execution.quiescent() && editor_work.quiescent(); }
    void useDraft() { draft.used = true; epoch->advance(); }
    void useSaved(int64_t id) { records.at(id).used = true; ++records.at(id).revision; epoch->advance(); }
};
#endif // BITCOIN_QML_TEST_MOCKS_RECEIVEEDITORTEST_H
