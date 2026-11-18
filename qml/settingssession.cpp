// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/settingssession.h>
#include <QCoreApplication>
#include <qml/settingsparse.h>
#include <utility>
namespace settingsedit {
namespace {
using namespace effectivesettings;
QString Text(const char* text) { return QCoreApplication::translate("SettingsEditorModel",text); }
size_t Index(Key key) { return static_cast<size_t>(key); }
void RememberDisabled(Key key,Cell& cell,const Cell& previous)
{
    if(key==Key::Prune){const auto* value=std::get_if<int64_t>(&cell.value);const auto* old=std::get_if<int64_t>(&previous.value);if(!value||!old)return;if(*value==0&&*old>0)cell.remembered=previous.value;else if(*value>0)cell.remembered.reset();}
    if(key==Key::Proxy||key==Key::Onion){const auto* value=std::get_if<QString>(&cell.value);const auto* old=std::get_if<QString>(&previous.value);if(!value||!old)return;const auto enabled=[](const QString& text){return !text.isEmpty()&&text!=QStringLiteral("0");};if(!enabled(*value)&&enabled(*old))cell.remembered=previous.value;else if(enabled(*value))cell.remembered.reset();}
}

}
Session::Session(persistentedit::Identity identity,Reducer::Mask allowed):m_editor(std::move(identity)),m_allowed(allowed) {}
QString Session::Format(const Value& value)
{
    if(const auto* number=std::get_if<int64_t>(&value))return QString::number(*number);
    if(const auto* text=std::get_if<QString>(&value))return *text;
    return std::get<bool>(value)?QStringLiteral("true"):QStringLiteral("false");
}
Reducer::Values Session::Values(const Snapshot& snapshot) const
{
    Reducer::Values result{};
    for(const auto& field:snapshot.fields)if(Index(field.key)<FIELDS)result[Index(field.key)]={field.committed,field.has_override?Intent::ExplicitOverride:Intent::RemoveOverride,field.remembered,true,{}};
    return result;
}
bool Session::observe(Snapshot snapshot)
{
    if(m_editor.phase()==persistentedit::Phase::Saving||m_editor.phase()==persistentedit::Phase::Uncertain||m_editor.phase()==persistentedit::Phase::Retiring){m_needs_refresh=true;return false;}
    std::bitset<FIELDS> seen;
    for(const auto& field:snapshot.fields){
        if(Index(field.key)>=FIELDS||seen[Index(field.key)]){snapshot.ready=false;break;}
        const auto typed=[&](const Value& value){return Type(field.key)==Kind::Boolean?std::holds_alternative<bool>(value):Type(field.key)==Kind::Integer?std::holds_alternative<int64_t>(value):std::holds_alternative<QString>(value);};
        if(field.kind!=Type(field.key)||!typed(field.committed)||!typed(field.effective)||!typed(field.next_start)){snapshot.ready=false;break;}
        seen[Index(field.key)]=true;
    }
    if(!snapshot.ready||!seen.all()||m_observation==UINT64_MAX){m_fresh=false;m_error=snapshot.error.isEmpty()?Text("The complete settings snapshot is unavailable."):snapshot.error;return false;}
    if(!m_editor.observe(Values(snapshot),++m_observation))return false;
    m_snapshot=std::move(snapshot);m_fresh=true;if(!m_result.present||m_result.error.isEmpty())m_error={};m_fields.clear();projectUntouchedLiterals();return true;
}
void Session::projectUntouchedLiterals()
{
    for(size_t i=0;i<FIELDS;++i)if(!m_editor.dirtyFields()[i])m_literals[i]=Format(m_editor.draft()[i].value);
}
bool Session::acceptedKey(Key key) const
{
    const auto i=Index(key);const auto* field=Find(m_snapshot,key);
    return i<FIELDS&&m_allowed[i]&&field&&field->editable;
}
bool Session::edit(Key key,Cell cell,QString literal)
{
    const auto i=Index(key);
    const auto* metadata=Find(m_snapshot,key);
    const bool reset=i<FIELDS&&m_allowed[i]&&metadata&&metadata->has_override&&metadata->provenance!=Provenance::CommandLine&&m_snapshot.persistence_available&&cell.intent==Intent::RemoveOverride;
    if((!acceptedKey(key)&&!reset)||m_next_input==UINT64_MAX||!m_editor.edit(i,std::move(cell)))return false;
    m_inputs[i]=++m_next_input;m_literals[i]=std::move(literal);m_fields.clear();m_error={};return true;
}
bool Session::editBoolean(Key key,bool value)
{
    if(Type(key)!=Kind::Boolean||Index(key)>=FIELDS)return false;
    auto cell=m_editor.draft()[Index(key)];cell.value=value;cell.intent=Intent::ExplicitOverride;cell.valid=true;cell.invalid={};
    return edit(key,std::move(cell),value?QStringLiteral("true"):QStringLiteral("false"));
}
bool Session::editText(Key key,QString text)
{
    if(Type(key)==Kind::Boolean||Index(key)>=FIELDS)return false;
    auto cell=m_editor.draft()[Index(key)];cell.intent=Intent::ExplicitOverride;cell.valid=text.size()<=32768&&!text.contains(QChar::Null);
    if(!cell.valid){text=text.left(32769);cell.invalid=text;}
    else if(Type(key)==Kind::Integer){const auto value=settingsparse::Integer(text);cell.valid=bool(value);if(value)cell.value=*value;else cell.invalid=text;}
    else cell.value=text;
    if(cell.valid){cell.invalid={};RememberDisabled(key,cell,m_editor.draft()[Index(key)]);}
    return edit(key,std::move(cell),std::move(text));
}
bool Session::useConfiguration(Key key)
{
    if(Index(key)>=FIELDS)return false;
    auto cell=m_editor.draft()[Index(key)];cell.intent=Intent::RemoveOverride;cell.valid=true;cell.invalid={};
    return edit(key,cell,Format(cell.value));
}
bool Session::useValue(Key key,Value value)
{
    if(Index(key)>=FIELDS)return false;
    const bool typed=Type(key)==Kind::Boolean?std::holds_alternative<bool>(value):Type(key)==Kind::Integer?std::holds_alternative<int64_t>(value):std::holds_alternative<QString>(value);
    if(!typed)return false;
    auto cell=m_editor.draft()[Index(key)];cell.value=std::move(value);cell.intent=Intent::ExplicitOverride;cell.valid=true;cell.invalid={};RememberDisabled(key,cell,m_editor.draft()[Index(key)]);
    return edit(key,cell,Format(cell.value));
}
QString Session::literal(Key key) const {return Index(key)<FIELDS?m_literals[Index(key)]:QString{};}
uint64_t Session::input(Key key) const {return Index(key)<FIELDS?m_inputs[Index(key)]:0;}
QString Session::fieldError(Key key) const
{
    if(Index(key)>=FIELDS)return {};
    const auto& cell=m_editor.draft()[Index(key)];
    if(!cell.valid)return cell.invalid.size()>32768||cell.invalid.contains(QChar::Null)?Text("This input is too long or contains an invalid character."):Text("Enter a whole number.");
    const auto item=m_fields.find(key);return item==m_fields.end()?QString{}:item->second;
}
bool Session::valid() const
{
    if(!m_fresh)return false;
    for(size_t i=0;i<FIELDS;++i)if(m_allowed[i]&&!m_editor.draft()[i].valid)return false;
    return m_fields.empty();
}
bool Session::canSave() const {return m_editor.canSave()&&valid();}
effectivesettings::Batch Session::Batch(const Reducer::Attempt& attempt) const
{
    effectivesettings::Batch result;result.base_revision=m_snapshot.revision;
    for(size_t i=0;i<FIELDS;++i)if(attempt.dirty[i]){const auto& cell=attempt.values[i];result.edits.push_back({static_cast<Key>(i),cell.value,cell.intent,cell.remembered});}
    return result;
}
std::optional<Save> Session::check()
{
    if(!canSave())return {};
    const auto attempt=m_editor.check();if(!attempt)return {};
    m_active=Save{*attempt,m_snapshot,Batch(*attempt),m_inputs};m_error={};return m_active;
}
bool Session::validated(uint64_t token,bool checked,QString error,std::map<Key,QString> fields)
{
    if(!m_active||m_active->attempt.token!=token)return false;
    const bool current=m_active->attempt.base_revision==m_editor.baseRevision()&&m_active->original.revision==m_snapshot.revision&&m_active->inputs==m_inputs;
    if(!m_editor.validated(token,checked&&current)){
        if(current){m_error=std::move(error);m_fields=std::move(fields);}
        m_result={token,true,false,false,true,current?m_error:Text("The edited batch changed before save admission.")};
        m_active.reset();return false;
    }
    m_active->attempt=*m_editor.active();m_active->batch=Batch(m_active->attempt);return true;
}
bool Session::complete(const effectivesettings::Result& result,bool causal,bool reconcile)
{
    if(!m_active)return false;
    const auto saved=*m_active;
    persistentedit::Outcome outcome;
    if (result.outcome == Outcome::Applied) {
        outcome = persistentedit::Outcome::Saved;
    } else if (result.write_completed && !result.persisted) {
        outcome = persistentedit::Outcome::Unknown;
    } else if (result.persisted) {
        outcome = persistentedit::Outcome::Partial;
    } else if (result.outcome == Outcome::Rejected || result.outcome == Outcome::Conflict) {
        outcome = persistentedit::Outcome::Rejected;
    } else if (result.outcome == Outcome::Uncertain) {
        outcome = persistentedit::Outcome::Unknown;
    } else {
        outcome = persistentedit::Outcome::Failed;
    }
    Reducer::Receipt receipt{saved.attempt,outcome,{},saved.attempt.values,{}};
    for(size_t i=0;i<FIELDS;++i)if(saved.attempt.dirty[i]&&result.persisted){
        const auto item=result.persisted_values.find(static_cast<Key>(i));
        if(item==result.persisted_values.end())continue;
        receipt.persisted[i]=true;auto& cell=receipt.values[i];cell.intent=item->second?Intent::ExplicitOverride:Intent::RemoveOverride;
        if(item->second)cell.value=*item->second;
        if(const auto memory=result.remembered_values.find(static_cast<Key>(i));memory!=result.remembered_values.end())cell.remembered=memory->second;
    }
    if(causal&&result.write_completed&&result.snapshot.ready&&m_observation!=UINT64_MAX)receipt.observed=Reducer::Observation{Values(result.snapshot),++m_observation,saved.attempt.token};
    if(!(reconcile?m_editor.reconciled(receipt):m_editor.finished(receipt)))return false;
    m_result={saved.attempt.token,true,result.write_completed,result.persisted,outcome!=persistentedit::Outcome::Unknown,result.error};m_error=result.error;
    for(size_t i=0;i<FIELDS;++i)if(receipt.persisted[i]){if(!m_result.actual_values.isEmpty())m_result.actual_values+=QLatin1Char('\n');const auto& cell=receipt.values[i];m_result.actual_values+=Name(static_cast<Key>(i))+QStringLiteral(" = ")+(cell.intent==Intent::RemoveOverride?Text("Use configuration"):Format(cell.value));}
    for(const auto& [key,error]:result.validation.errors)if(Index(key)<FIELDS&&saved.inputs[Index(key)]==m_inputs[Index(key)])m_fields[key]=error;
    if(receipt.observed){m_snapshot=result.snapshot;m_fresh=true;}else if(result.write_completed)m_fresh=false;
    for(size_t i=0;i<FIELDS;++i)if(receipt.persisted[i]&&saved.inputs[i]==m_inputs[i])m_literals[i]=Format(m_editor.draft()[i].value);
    if(outcome!=persistentedit::Outcome::Unknown)m_active.reset();
    m_needs_refresh|=result.write_completed;return true;
}
bool Session::finished(const effectivesettings::Result& result,bool causal_observation) {return complete(result,causal_observation,false);}
bool Session::reconciled(const effectivesettings::Result& result) {return complete(result,false,true);}
bool Session::discard()
{
    if(!m_editor.discard())return false;
    m_error={};m_fields.clear();projectUntouchedLiterals();return true;
}
bool Session::requestClose() {return m_editor.requestClose();}
bool Session::answerClose(uint64_t token,persistentedit::CloseChoice choice) {return m_editor.answerClose(token,choice);}
bool Session::resolve(Key key,bool keep)
{
    if(Index(key)>=FIELDS||!m_editor.resolve(Index(key),keep))return false;
    if(!keep)m_literals[Index(key)]=Format(m_editor.draft()[Index(key)].value);
    m_fields.erase(key);return true;
}
void Session::retire() {m_editor.retire();}
} // namespace settingsedit
