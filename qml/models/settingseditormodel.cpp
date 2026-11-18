// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/settingseditormodel.h>
#include <qml/guipresentation.h>
#include <qml/retainedsnapshot.h>
#include <QCoreApplication>
#include <QPointer>
#include <QUuid>
#include <algorithm>
#include <mutex>
namespace {
using namespace effectivesettings;
size_t Index(Key key){return static_cast<size_t>(key);}
QString Source(Provenance value){switch(value){case Provenance::Default:return QStringLiteral("default");case Provenance::Configuration:return QStringLiteral("bitcoin.conf");case Provenance::Persistent:return QStringLiteral("settings.json");case Provenance::CommandLine:return QStringLiteral("command line");case Provenance::Startup:return QStringLiteral("startup");}return {};}
QString Failure(){return QCoreApplication::translate("SettingsEditorModel","The background operation could not complete. The original write will not be repeated.");}
}
struct SettingsEditorModel::Projection {
    settingsedit::Session session;
    std::array<Value,settingsedit::FIELDS> running;
    std::array<uint64_t,settingsedit::FIELDS> inputs;
    bool reading{false},restart{false},notice{false},redownload{false};
    uint64_t redownload_token{0},finished_save{0},rejection{0};
};
struct SettingsEditorModel::Mailbox {
    std::mutex mutex;
    std::shared_ptr<const Projection> latest;
    std::atomic_bool retired{false},drained{false},notice_pending{false};
};
struct SettingsEditorModel::State:std::enable_shared_from_this<State> {
    State(QString id,Ports ports,std::shared_ptr<Mailbox> mailbox,std::function<void()> notify):session({id.toStdString(),"effective-settings"},settingsedit::Reducer::Mask{}.set()),ports(std::move(ports)),scope(this->ports.execution.scheduler->openActionScope()),key(this->ports.execution.scheduler->allocateKey()),io(this->ports.execution.scheduler->allocateKey()),notify(std::move(notify)),mailbox(std::move(mailbox)){}
    settingsedit::Session session;
    const Ports ports;
    std::shared_ptr<execution::Scheduler> scope;
    const execution::ResourceKey key,io;
    const std::function<void()> notify;
    std::array<Value,settingsedit::FIELDS> running{};
    std::array<uint64_t,settingsedit::FIELDS> inputs{};
    bool initialized{false},reading{false},read_pending{false},retired{false},redownload{false},allow_redownload{false};
    QString pending_restart,acknowledged_restart;
    std::optional<settingsedit::Save> redownload_attempt;
    uint64_t read_token{0};
    std::optional<effectivesettings::Result> receipt;
    const std::shared_ptr<Mailbox> mailbox;
    uint64_t finished_save{0},rejection{0};
    ~State(){mailbox->drained.store(true);if(ports.retired)ports.retired();notify();}
    execution::Admission post(const std::shared_ptr<execution::Scheduler>& target,std::function<void(State&)> command,std::function<void(std::exception_ptr)> failed={}){if(!target)return {{},execution::Rejection::Closed};const auto self=shared_from_this();return execution::Submit(*target,[self,command=std::move(command)]{if(self->mailbox->retired.load())self->retire();command(*self);},[]{},std::move(failed),{execution::Lane::Domain,key,execution::WorkClass::Action});}
    QString restartSet()const {
        QString result;
        if(!initialized)return result;
        for(const auto& field:session.snapshot().fields)if(field.applicability==Applicability::Restart&&field.next_start!=running[Index(field.key)])result+=Name(field.key)+QLatin1Char('=')+settingsedit::Session::Format(field.next_start)+QLatin1Char('\n');
        return result;
    }
    void publish(){const auto changes=restartSet();if(changes!=pending_restart){pending_restart=changes;acknowledged_restart={};}auto value=std::make_shared<const Projection>(Projection{session,running,inputs,reading,!changes.isEmpty(),!changes.isEmpty()&&acknowledged_restart!=changes,redownload,redownload_attempt?redownload_attempt->attempt.token:0,finished_save,rejection});{std::lock_guard lock{mailbox->mutex};mailbox->latest=std::move(value);}notify();}
    void observe(Snapshot value){if(!initialized&&value.ready){for(const auto& field:value.fields)if(Index(field.key)<settingsedit::FIELDS)running[Index(field.key)]=field.effective;initialized=true;}session.observe(std::move(value));}
    void read(std::function<void(bool)> owner={}){
        const auto finished=[&owner](bool result){if(owner)owner(result);};
        if(retired){finished(false);return;}
        const auto phase=session.editor().phase();
        if(reading||phase==persistentedit::Phase::Checking||phase==persistentedit::Phase::Saving){read_pending=true;finished(false);return;}
        if(phase==persistentedit::Phase::Uncertain||phase==persistentedit::Phase::Closed||read_token==UINT64_MAX){finished(false);return;}
        reading=true;read_pending=false;const auto token=++read_token;const auto self=shared_from_this();
        const auto complete=[self,token,owner](Snapshot value){
            if(token!=self->read_token){if(owner)owner(false);return;}
            if(self->mailbox->retired.load())self->retire();
            self->reading=false;if(!self->retired)self->observe(std::move(value));
            const bool again=std::exchange(self->read_pending,false);self->publish();
            if(again&&!self->retired){if(!self->post(self->scope,[owner](State& state){state.read(owner);})&&owner)owner(false);}
            else if(owner)owner(true);
        };
        const auto rejected=[complete](std::exception_ptr){Snapshot failed;failed.error=Failure();complete(std::move(failed));};
        if(!ports.read||!scope||!execution::Submit(*scope,ports.read,complete,rejected,{execution::Lane::Core,io,execution::WorkClass::Observation})){Snapshot failed;failed.error=Failure();complete(std::move(failed));}
        publish();
    }
    void save(std::shared_ptr<execution::Scheduler> action,std::optional<uint64_t> token,std::function<void(bool)> owner){
        if(retired||reading){publish();owner(false);return;}
        if(token&&!session.answerClose(*token,persistentedit::CloseChoice::Save)){publish();owner(false);return;}
        auto attempt=session.check();if(!attempt){publish();owner(false);return;}attempt->batch.accept_redownload=allow_redownload;
        redownload=false;const auto self=shared_from_this();publish();
        const auto done=[self,action,attempt=*attempt,owner](Check checked){
            if(self->mailbox->retired.load())self->retire();
            self->redownload=checked.validation.requires_redownload;
            self->redownload_attempt=self->redownload?std::make_optional(attempt):std::nullopt;
            if(!self->session.validated(attempt.attempt.token,checked.valid,checked.error,checked.validation.errors)){self->publish();owner(false);return;}
            self->publish();
            // The review packet remains charged until this callback returns.
            // Continue on the original admitted scope after its budget is freed;
            // keep the immutable accepted attempt in State rather than copying
            // it into another queued packet or waiting for GUI publication.
            const auto token=attempt.attempt.token;
            const bool redownload=attempt.batch.accept_redownload;
            const auto rejected=[self,token,owner](std::exception_ptr error){
                if(!self->session.active()||self->session.active()->attempt.token!=token)return;
                const auto accepted=*self->session.active();
                Result result;result.outcome=Outcome::Rejected;result.error=Failure();
                if(error)try{std::rethrow_exception(error);}catch(const execution::NotStarted&){}catch(...){result.outcome=Outcome::Uncertain;}
                self->receipt=result;self->finish(accepted,std::move(result),false,owner);
            };
            if(!self->post(action,[action,token,redownload,owner](State& state){state.write(action,token,redownload,owner);},rejected))rejected({});
        };
        const auto failed=[done](std::exception_ptr){Check checked;checked.error=Failure();done(std::move(checked));};
        if(!ports.review||!execution::Submit(*action,[review=ports.review,attempt=*attempt]{return review(attempt);},done,failed,{execution::Lane::Core,io,execution::WorkClass::Action})){Check checked;checked.error=Failure();done(std::move(checked));}
    }
    void write(const std::shared_ptr<execution::Scheduler>& action,uint64_t token,bool redownload,std::function<void(bool)> owner){
        if(!session.active()||session.active()->attempt.token!=token){publish();owner(false);return;}
        auto accepted=*session.active();accepted.batch.accept_redownload=redownload;
        const auto self=shared_from_this();
        const auto written=[self,accepted,action,owner](Result value){(void)action;self->receipt=value;self->finish(accepted,std::move(value),false,owner);};
        const auto failed=[written](std::exception_ptr error){Result result;result.outcome=Outcome::Uncertain;result.error=Failure();
            try{std::rethrow_exception(error);}catch(const execution::NotStarted&){result.outcome=Outcome::Rejected;}catch(...){}
            written(std::move(result));};
        if(!ports.write||!execution::Submit(*action,[write=ports.write,accepted]{return write(accepted);},written,failed,{execution::Lane::Core,io,execution::WorkClass::Action})){Result result;result.outcome=Outcome::Rejected;result.error=Failure();written(std::move(result));}
    }
    void finish(const settingsedit::Save& attempt,Result result,bool query,std::function<void(bool)> owner={}){
        if(mailbox->retired.load())retire();
        if(!(query?session.reconciled(result):session.finished(result,true))){if(owner)owner(false);return;}
        if(result.runtime_confirmed)for(const auto& edit:attempt.batch.edits)if(const auto* field=Find(result.snapshot,edit.key);field&&field->applicability==Applicability::Live)running[Index(edit.key)]=field->next_start;
        allow_redownload=false;
        if(session.needsRefresh()&&!retired){session.clearRefreshRequest();read_pending=false;read([owner](bool){if(owner)owner(true);});}
        else if(owner)owner(true);
        publish();
    }
    void reconcile(){if(!receipt||!session.active()||session.editor().phase()!=persistentedit::Phase::Uncertain){publish();return;}const auto original=*session.active();auto value=*receipt;value.snapshot={};finish(original,std::move(value),true);}
    void retire(){if(std::exchange(retired,true))return;read_pending=false;session.retire();scope.reset();publish();}

};
SettingsEditorModel::SettingsEditorModel(Ports ports,QObject* parent):QObject(parent),m_id(QUuid::createUuid().toString(QUuid::WithoutBraces)),m_execution(ports.execution){
    Q_ASSERT(m_execution.scheduler);m_execution.post=presentation::ReliablePost(std::move(m_execution.post));ports.execution=m_execution;
    for(size_t i=0;i<m_entries.size();++i)m_entries[i]=new SettingsEntryModel(static_cast<Key>(i),*this);
    m_mailbox=std::make_shared<Mailbox>();const auto original_retired=ports.retired;
    const QPointer<SettingsEditorModel> observer{this};
    m_owner=execution::RetainSnapshot<std::shared_ptr<State>>(m_execution.scheduler,[id=m_id,ports=std::move(ports),mailbox=m_mailbox,observer,post=m_execution.post]()mutable{return std::make_unique<std::shared_ptr<State>>(std::make_shared<State>(id,std::move(ports),mailbox,[mailbox,observer,post]{
        if(mailbox->notice_pending.exchange(true))return;
        try{post([mailbox,observer]{mailbox->notice_pending.store(false);if(observer)observer->publish();});}catch(...){mailbox->notice_pending.store(false);throw;}
    }));});
    if(m_owner){m_state=*m_owner;m_retire_scope=(*m_owner)->scope;refresh();}
    else{m_immediate_error=tr("The settings editor could not reserve cleanup.");m_retired=true;m_mailbox->drained.store(true);if(original_retired)original_retired();m_execution.post([observer]{if(observer)observer->publish();});}
}
SettingsEditorModel::~SettingsEditorModel(){retire();}
bool SettingsEditorModel::command(std::function<void(State&)> callback,const QString& name){
    bool admitted{false};{const auto state=m_state.lock();admitted=!m_retired&&state&&bool(state->post(m_execution.scheduler,std::move(callback)));}
    if(admitted){m_immediate_error={};return true;}
    m_immediate_error=tr("This settings command could not be admitted.");const QPointer<SettingsEditorModel> guard{this};Q_EMIT changed();if(guard)Q_EMIT commandRejected(name,m_immediate_error);return false;
}
void SettingsEditorModel::publish(){
    std::shared_ptr<const Projection> value;{std::lock_guard lock{m_mailbox->mutex};value=m_mailbox->latest;}
    if(value)m_projection=std::move(value);
    if(m_projection)for(size_t i=0;i<m_inputs.size();++i)if(m_projection->inputs[i]>=m_inputs[i].sequence)m_inputs[i].value.reset();
    if(m_projection&&m_pending_save&&m_projection->finished_save>=m_pending_save){m_pending_save=0;m_immediate_error={};}
    const bool rejected=m_projection&&m_projection->rejection>m_seen_rejection;if(rejected)m_seen_rejection=m_projection->rejection;
    const bool drained=m_retired&&m_mailbox->drained.load()&&!m_drained;if(drained)m_drained=true;
    const QPointer<SettingsEditorModel> guard{this};for(auto* entry:m_entries){Q_EMIT entry->changed();if(!guard)return;}Q_EMIT changed();if(!guard)return;
    if(rejected){Q_EMIT commandRejected(QStringLiteral("save"),tr("This settings save did not start. The original draft and any earlier receipt are retained."));if(!guard)return;}
    if(!m_closed_emitted&&phase()==Closed){m_closed_emitted=true;Q_EMIT closed();if(!guard)return;}
    if(drained)Q_EMIT shutdownFinished();
}
QQmlListProperty<SettingsEntryModel> SettingsEditorModel::entries(){return {this,this,nullptr,[](QQmlListProperty<SettingsEntryModel>* list)->qsizetype{return qsizetype(static_cast<SettingsEditorModel*>(list->data)->m_entries.size());},[](QQmlListProperty<SettingsEntryModel>* list,qsizetype index)->SettingsEntryModel*{const auto& entries=static_cast<SettingsEditorModel*>(list->data)->m_entries;return index>=0&&size_t(index)<entries.size()?entries[size_t(index)]:nullptr;},nullptr};}
SettingsEditorModel::Phase SettingsEditorModel::phase()const{return m_retired&&m_mailbox->drained.load()?Closed:m_projection?static_cast<Phase>(m_projection->session.editor().phase()):m_retired?Retiring:Loading;}
bool SettingsEditorModel::fresh()const{return m_projection&&m_projection->session.fresh()&&!m_projection->reading;}
bool SettingsEditorModel::inputPending()const{return std::any_of(m_inputs.begin(),m_inputs.end(),[](const auto& input){return input.value.has_value();});}
bool SettingsEditorModel::dirty()const{return inputPending()||(m_projection&&m_projection->session.editor().dirty());}
bool SettingsEditorModel::busy()const{return !m_retired&&(inputPending()||m_pending_save||(m_projection&&(m_projection->reading||m_projection->session.editor().busy())));}
bool SettingsEditorModel::canSave()const{return !m_retired&&!m_pending_save&&!inputPending()&&fresh()&&m_projection->session.canSave();}
bool SettingsEditorModel::canDiscard()const{return !m_retired&&!m_pending_save&&m_projection&&m_projection->session.editor().canDiscard();}
QString SettingsEditorModel::error()const{return !m_immediate_error.isEmpty()?m_immediate_error:m_projection?m_projection->session.error():QString{};}
SettingsSaveResult SettingsEditorModel::saveResult()const{
    SettingsSaveResult result;result.editorId=m_id;
    if(!m_projection)return result;
    const auto& saved=m_projection->session.result();result.operationId=m_id+QLatin1Char(':')+QString::number(saved.token);
    result.present=saved.present;result.persisted=saved.persisted;result.outcomeKnown=saved.known;result.actualValues=saved.actual_values;result.error=saved.error;
    if(m_projection->session.editor().result())result.outcome=static_cast<SettingsSaveResult::Outcome>(m_projection->session.editor().result()->outcome);
    return result;
}
QString SettingsEditorModel::lastSaveId()const{return QString::number(m_projection?m_projection->session.result().token:0);}
QString SettingsEditorModel::redownloadToken()const{return QString::number(m_projection?m_projection->redownload_token:0);}
QString SettingsEditorModel::closeToken()const{return QString::number(m_projection?m_projection->session.editor().closeToken():0);}
bool SettingsEditorModel::closeQuestion()const{return m_projection&&m_projection->session.editor().closeQuestion();}
bool SettingsEditorModel::lastSavePresent()const{return m_projection&&m_projection->session.result().present;}
bool SettingsEditorModel::lastSavePersisted()const{return m_projection&&m_projection->session.result().persisted;}
bool SettingsEditorModel::lastSaveKnown()const{return m_projection&&m_projection->session.result().known;}
bool SettingsEditorModel::restartRequired()const{return m_projection&&m_projection->restart;}
bool SettingsEditorModel::restartNoticeVisible()const{return m_projection&&m_projection->notice;}
QString SettingsEditorModel::restartExplanation()const{return tr("Saved settings differ from this process's startup configuration. Restart Bitcoin Core to apply them. Acknowledging this notice does not apply the settings.");}
bool SettingsEditorModel::redownloadWarning()const{return m_projection&&m_projection->redownload;}
SettingsEntryModel* SettingsEditorModel::entry(const QString& key)const{for(auto* item:m_entries)if(item->key()==key)return item;return nullptr;}
const Field* SettingsEditorModel::field(Key key)const{return m_projection?Find(m_projection->session.snapshot(),key):nullptr;}
void SettingsEditorModel::save(){saveCommand();}
void SettingsEditorModel::saveCommand(std::optional<uint64_t> token){
    const QPointer<SettingsEditorModel> observer{this};
    if(m_pending_save){Q_EMIT commandRejected(QStringLiteral("save"),tr("The original settings save is still in progress."));return;}
    const auto action=!m_retired&&m_next_save!=UINT64_MAX?m_execution.scheduler->openActionScope():nullptr;
    const auto request=action?++m_next_save:0;bool admitted{false};
    if(request){m_pending_save=request;{const auto state=m_state.lock();admitted=state&&bool(state->post(action,[action,token,request](State& owner){const auto self=owner.shared_from_this();owner.save(action,token,[self,request](bool okay){self->finished_save=std::max(self->finished_save,request);if(!okay&&self->rejection!=UINT64_MAX)++self->rejection;self->publish();});}));}}
    if(admitted){m_immediate_error={};Q_EMIT changed();return;}
    m_pending_save=0;m_immediate_error=tr("The settings save could not be admitted.");Q_EMIT changed();if(observer)Q_EMIT commandRejected(QStringLiteral("save"),m_immediate_error);
}
void SettingsEditorModel::discard(){command([](State& state){state.session.discard();state.allow_redownload=false;state.redownload=false;state.redownload_attempt.reset();state.publish();},QStringLiteral("discard"));}
void SettingsEditorModel::refresh(){command([](State& state){state.read();},QStringLiteral("refresh"));}
void SettingsEditorModel::reconcile(){command([](State& state){state.reconcile();},QStringLiteral("reconcile"));}
void SettingsEditorModel::requestClose(){command([](State& state){state.session.requestClose();state.publish();},QStringLiteral("close"));}
void SettingsEditorModel::answerClose(const QString& token,CloseChoice choice){bool okay{false};const auto number=token.toULongLong(&okay);if(!okay||choice<Save||choice>KeepEditing)return;if(choice==Save){saveCommand(number);return;}command([number,choice](State& state){state.session.answerClose(number,static_cast<persistentedit::CloseChoice>(choice));state.publish();},QStringLiteral("close"));}
void SettingsEditorModel::acknowledgeRestart(){command([](State& state){state.acknowledged_restart=state.restartSet();state.publish();},QStringLiteral("acknowledge"));}
void SettingsEditorModel::setPruneMode(PruneMode mode){if(mode<Disabled||mode>Automatic)return;command([mode](State& state){int64_t value=mode==Disabled?0:mode==Manual?1:550;if(mode==Automatic){const auto& cell=state.session.editor().draft()[Index(Key::Prune)];const auto* current=std::get_if<int64_t>(&cell.value);const auto* memory=cell.remembered?std::get_if<int64_t>(&*cell.remembered):nullptr;if(current&&*current>=550)value=*current;else if(memory&&*memory>=550)value=*memory;}state.session.useValue(Key::Prune,value);state.allow_redownload=false;state.redownload=false;state.redownload_attempt.reset();state.publish();},QStringLiteral("prune"));}
void SettingsEditorModel::confirmRedownload(const QString& token,bool accepted){
    bool okay{false};const auto number=token.toULongLong(&okay);if(!okay)return;
    command([number,accepted](State& state){
        bool current=state.redownload_attempt&&state.redownload_attempt->attempt.token==number&&state.redownload_attempt->original.revision==state.session.snapshot().revision;
        for(size_t i=0;i<settingsedit::FIELDS&&current;++i)current=state.redownload_attempt->inputs[i]==state.session.input(static_cast<Key>(i));
        if(current){state.allow_redownload=accepted;if(accepted)state.session.clearFieldError(Key::Prune);}
        state.publish();
    },QStringLiteral("confirm"));
}
void SettingsEditorModel::close(){retire();}
void SettingsEditorModel::retire(std::function<void()> done){
    if(done){if(m_drained)m_execution.post(std::move(done));else connect(this,&SettingsEditorModel::shutdownFinished,this,std::move(done));}
    if(std::exchange(m_retired,true)){if(m_mailbox->drained.load()&&!m_drained)publish();return;}
    m_mailbox->retired.store(true);
    {const auto state=m_state.lock();if(state)state->post(m_retire_scope,[](State& owner){owner.retire();});}
    m_retire_scope.reset();m_owner.reset();
    const QPointer<SettingsEditorModel> observer{this};Q_EMIT changed();if(observer&&m_mailbox->drained.load())publish();
}
void SettingsEditorModel::editInput(Key key,std::variant<bool,QString> value){
    const QPointer<SettingsEditorModel> guard{this};
    if(m_next_input==UINT64_MAX){Q_EMIT commandRejected(QStringLiteral("edit"),tr("This settings input could not be admitted."));return;}
    const auto request=++m_next_input;
    const bool admitted=command([key,value,request](State& state){
        if(const auto* boolean=std::get_if<bool>(&value))state.session.editBoolean(key,*boolean);
        else state.session.editText(key,std::get<QString>(value));
        state.inputs[Index(key)]=std::max(state.inputs[Index(key)],request);
        state.allow_redownload=false;state.redownload=false;state.redownload_attempt.reset();state.publish();
    },QStringLiteral("edit"));
    if(!guard)return;
    if(!admitted){Q_EMIT m_entries[Index(key)]->changed();return;}
    m_inputs[Index(key)]={request,std::move(value)};
    Q_EMIT m_entries[Index(key)]->changed();if(guard)Q_EMIT changed();
}
void SettingsEditorModel::editBoolean(Key key,bool value){editInput(key,value);}
void SettingsEditorModel::editText(Key key,QString text){editInput(key,text.left(32769));}
void SettingsEditorModel::useConfiguration(Key key){command([key](State& state){state.session.useConfiguration(key);state.allow_redownload=false;state.redownload=false;state.redownload_attempt.reset();state.publish();},QStringLiteral("reset"));}
void SettingsEditorModel::resolve(Key key,bool keep){command([key,keep](State& state){state.session.resolve(key,keep);state.publish();},QStringLiteral("resolve"));}
SettingsEntryModel::SettingsEntryModel(Key key,SettingsEditorModel& editor):QObject(&editor),m_key(key),m_editor(editor){}
QString SettingsEntryModel::key()const{return Name(m_key);}
bool SettingsEntryModel::editable()const{const auto* field=m_editor.field(m_key);return !m_editor.m_retired&&field&&field->editable;}
bool SettingsEntryModel::booleanValue()const{const auto& pending=m_editor.m_inputs[Index(m_key)].value;if(pending)if(const auto* value=std::get_if<bool>(&*pending))return *value;if(!m_editor.m_projection)return false;const auto* value=std::get_if<bool>(&m_editor.m_projection->session.editor().draft()[Index(m_key)].value);return value&&*value;}
QString SettingsEntryModel::text()const{const auto& pending=m_editor.m_inputs[Index(m_key)].value;if(pending)if(const auto* value=std::get_if<QString>(&*pending))return *value;return m_editor.m_projection?m_editor.m_projection->session.literal(m_key):QString{};}
QString SettingsEntryModel::committedText()const{return m_editor.m_projection?settingsedit::Session::Format(m_editor.m_projection->session.editor().base()[Index(m_key)].value):QString{};}
QString SettingsEntryModel::effectiveText()const{const auto* field=m_editor.field(m_key);return field?settingsedit::Session::Format(m_editor.m_projection->running[Index(m_key)]):QString{};}
QString SettingsEntryModel::nextStartText()const{const auto* field=m_editor.field(m_key);return field?settingsedit::Session::Format(field->next_start):QString{};}
QString SettingsEntryModel::source()const{const auto* field=m_editor.field(m_key);return field?Source(field->provenance):QString{};}
QString SettingsEntryModel::information()const{const auto* field=m_editor.field(m_key);return field?field->information:QString{};}
QString SettingsEntryModel::error()const{return m_editor.m_projection?m_editor.m_projection->session.fieldError(m_key):QString{};}
bool SettingsEntryModel::dirty()const{return m_editor.m_projection&&m_editor.m_projection->session.editor().dirtyFields()[Index(m_key)];}
bool SettingsEntryModel::conflict()const{return m_editor.m_projection&&m_editor.m_projection->session.editor().conflicts()[Index(m_key)];}
bool SettingsEntryModel::overridden()const{return m_editor.m_projection&&m_editor.m_projection->session.editor().draft()[Index(m_key)].intent==Intent::ExplicitOverride;}
void SettingsEntryModel::editBoolean(bool value){m_editor.editBoolean(m_key,value);}
void SettingsEntryModel::editText(const QString& text){m_editor.editText(m_key,text);}
void SettingsEntryModel::useConfiguration(){m_editor.useConfiguration(m_key);}
void SettingsEntryModel::resolve(bool keep){m_editor.resolve(m_key,keep);}

SettingsEntryModel::InputKind SettingsEntryModel::kind()const{return static_cast<InputKind>(Type(m_key));}
QString SettingsEntryModel::section()const{switch(m_key){case Key::Listen:case Key::Natpmp:case Key::Server:case Key::Proxy:case Key::Onion:return QStringLiteral("connection");case Key::Prune:case Key::Dbcache:case Key::Threads:return QStringLiteral("storage");case Key::Mempool:return QStringLiteral("mempool");case Key::Signer:return QStringLiteral("external-signer");}return {};}
QString SettingsEntryModel::title()const{switch(m_key){case Key::Listen:return tr("Allow incoming connections");case Key::Natpmp:return tr("Map port using NAT-PMP");case Key::Server:return tr("Enable RPC server");case Key::Prune:return tr("Block storage target (MiB; 0 disables, 1 is manual)");case Key::Proxy:return tr("Default SOCKS5 proxy (empty disables)");case Key::Onion:return tr("Tor SOCKS5 proxy (empty inherits; 0 disables)");case Key::Dbcache:return tr("Database cache (MiB)");case Key::Threads:return tr("Script verification threads (0 automatic)");case Key::Mempool:return tr("Mempool size (MB; 0 is unlimited)");case Key::Signer:return tr("External signer command");}return {};}
