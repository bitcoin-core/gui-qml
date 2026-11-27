// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/effectivesettings.h>
#include <bitcoin-build-config.h> // IWYU pragma: keep
#include <qml/core_settings.h>
#include <qml/signerdiscovery.h>
#include <common/args.h>
#include <common/settings.h>
#include <init.h>
#include <interfaces/node.h>
#include <kernel/mempool_limits.h>
#include <netbase.h>
#include <univalue.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QStringList>
#include <array>
#include <algorithm>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>

namespace effectivesettings {
namespace {
constexpr std::array KEYS{Key::Listen,Key::Natpmp,Key::Server,Key::Prune,Key::Proxy,Key::Onion,Key::Dbcache,Key::Threads,Key::Mempool,Key::Signer};
QString Text(const char* text) { return QCoreApplication::translate("SettingsModel", text); }
common::Settings Capture(ArgsManager& args) { common::Settings result; args.LockSettings([&](common::Settings& settings) { result = settings; }); return result; }
using SavedValues = std::map<std::string,common::SettingsValue>;
SavedValues Disk(ArgsManager& args)
{
    fs::path path;
    if (!args.GetSettingsPath(&path)) throw std::runtime_error("Persistent settings are disabled in this profile.");
    SavedValues result;
    std::vector<std::string> errors;
    if (!common::ReadSettings(path, result, errors)) throw std::runtime_error(errors.empty() ? "The settings file could not be read." : errors.front());
    return result;
}
bool SameSavedValues(const SavedValues& left, const SavedValues& right)
{
    if (left.size() != right.size()) return false;
    for (const auto& [name,value] : left) {
        const auto item = right.find(name);
        if (item == right.end() || item->second.write() != value.write()) return false;
    }
    return true;
}
QString Revision(const common::Settings& settings, const std::string& network)
{
    QByteArray bytes = QByteArray::fromStdString(network);
    const auto frame = [&](const std::string& value) { bytes += QByteArray::number(qulonglong(value.size())); bytes += ':'; bytes += QByteArray::fromStdString(value); };
    const auto add = [&](const auto& values) {
        for (const auto& [key, value] : values) { frame(key); frame(value.write()); }
    };
    add(settings.rw_settings); bytes += '|'; add(settings.forced_settings); bytes += '|';
    const auto lists = [&](const auto& values) { for (const auto& [key, items] : values) { frame(key); bytes += QByteArray::number(qulonglong(items.size())); bytes += '['; for (const auto& item : items) frame(item.write()); bytes += ']'; } };
    lists(settings.command_line_options); bytes += '|';
    for (const auto& [section, values] : settings.ro_config) { frame(section); lists(values); }
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
std::unique_ptr<ArgsManager> CapturedArgs(const common::Settings& settings,const std::string& network)
{
    auto result=std::make_unique<ArgsManager>();SetupServerArgs(*result);result->SelectConfigNetwork(network);result->LockSettings([&](common::Settings& target){target=settings;});return result;
}
std::unique_ptr<ArgsManager> NextArgs(const common::Settings& settings,const std::string& network)
{
    auto next=CapturedArgs(settings,network);next->LockSettings([](common::Settings& target){target.forced_settings.clear();});InitParameterInteraction(*next);return next;
}
Value Scalar(Key key, const common::SettingsValue& value)
{
    if (Type(key) == Kind::Boolean) return SettingToBool(value).value_or(SettingToBool(QmlCoreSettings::DefaultCoreSettingValue(Name(key))).value_or(false));
    if (Type(key) == Kind::Integer) return SettingTo<int64_t>(value).value_or(SettingTo<int64_t>(QmlCoreSettings::DefaultCoreSettingValue(Name(key))).value_or(0));
    const auto text = SettingToString(value).value_or("");
    if(text.size()>131072)return QString(32769,QChar::ReplacementCharacter);
    const auto bounded=QString::fromStdString(text);return bounded.size()>32768?QString(32769,QChar::ReplacementCharacter):bounded;
}
common::SettingsValue Stored(Key key, const Value& value)
{
    if (const auto* item = std::get_if<bool>(&value)) return *item;
    if (const auto* item = std::get_if<int64_t>(&value)) return *item;
    const auto& text = std::get<QString>(value);
    if ((key == Key::Proxy && (text.isEmpty() || text == "0")) || (key == Key::Onion && text == "0")) return false;
    return text.toStdString();
}
bool Enabled(Key key, const Value& value)
{
    if (key == Key::Prune) return std::get<int64_t>(value) != 0;
    const auto& text = std::get<QString>(value);
    return !text.isEmpty() && text != "0";
}
Snapshot Project(ArgsManager& current, const common::Settings& source, const std::string& network)
{
    auto next = NextArgs(source, network); Snapshot result; result.ready = true; result.revision = Revision(source, network);
    result.persistence_available = current.GetSettingsPath(nullptr);
    for (const auto key : KEYS) {
        const auto name = Name(key);
        const auto argument = std::string{"-"} + name.toStdString();
        const auto status = QmlCoreSettings::CoreSettingStatus(current, name);
        const auto stored = source.rw_settings.find(name.toStdString());
        const auto provenance = status.value("source").toString();
        Field field{key,Type(key),Scalar(key,current.GetSetting(argument)),Scalar(key,current.GetSetting(argument)),Scalar(key,next->GetSetting(argument))};
        field.has_override = stored != source.rw_settings.end() && !stored->second.isNull();
        if (field.has_override) field.committed = Scalar(key, stored->second);
        field.editable = status.value("canEdit").toBool();
        field.provenance = provenance == "command_line" ? Provenance::CommandLine : provenance == "startup" ? Provenance::Startup : provenance == "settings_json" ? Provenance::Persistent : provenance == "bitcoin_conf" ? Provenance::Configuration : Provenance::Default;
        field.applicability = key == Key::Natpmp || key == Key::Signer ? Applicability::Live : Applicability::Restart;
        field.information = status.value("infoText").toString().left(4096);
        if (field.applicability == Applicability::Live && field.editable) field.information = Text("This setting is applied to the running application after a confirmed save.");
        if (key == Key::Proxy || key == Key::Onion) {
            const auto values = current.GetArgs(argument);
            if (values.size() > 1 || (!values.empty() && values.front().find('=') != std::string::npos)) {
                field.editable=false;
                QStringList shown;for(size_t i=0;i<std::min<size_t>(values.size(),32);++i){shown.push_back(QString::fromStdString(values[i].substr(0,128)));if(values[i].size()>128)shown.back()+=Text(" [truncated]");}
                if(values.size()>32)shown.push_back(Text("Additional entries are omitted from this bounded summary."));
                field.information=Text("This per-network or multiple proxy configuration is read-only here. Core reports these entries; change their original configuration or command-line option:")+QLatin1Char('\n')+shown.join(QLatin1Char('\n'));
            }
        }
#ifndef ENABLE_EXTERNAL_SIGNER
        if (key == Key::Signer) { field.editable = false; field.information = Text("External signer support is unavailable in this build."); }
#endif
        if (!result.persistence_available) { field.editable = false; field.information = Text("Persistent settings are disabled for this profile."); }
        if (key == Key::Prune || key == Key::Proxy || key == Key::Onion) {
            const auto previous = current.GetPersistentSetting(name.toStdString() + "-prev");
            if (!previous.isNull()) field.remembered = Scalar(key, previous);
        }
        result.fields.push_back(std::move(field));
    }
    return result;
}
bool Boolean(const Snapshot& snapshot, Key key) { const auto* field = Find(snapshot,key); return field && std::holds_alternative<bool>(field->next_start) && std::get<bool>(field->next_start); }
int64_t Integer(const Snapshot& snapshot, Key key) { const auto* field = Find(snapshot,key); return field && std::holds_alternative<int64_t>(field->next_start) ? std::get<int64_t>(field->next_start) : 0; }
QString String(const Snapshot& snapshot, Key key) { const auto* field = Find(snapshot,key); return field && std::holds_alternative<QString>(field->next_start) ? std::get<QString>(field->next_start) : QString{}; }
Validation Shape(const Snapshot& candidate, const Batch& batch)
{
    if (!candidate.ready || batch.edits.empty() || batch.edits.size() > KEYS.size()) return {{{Key::Listen, Text("The complete settings batch is unavailable or exceeds its field budget.")}}, false};
    Validation result; std::set<Key> seen;
    for(const auto& edit:batch.edits) {
        const auto* field=Find(candidate,edit.key);
        if(!candidate.persistence_available||!field||(!field->editable && !(edit.intent == Intent::RemoveOverride && field->provenance != Provenance::CommandLine))) { result.errors[edit.key]=Text("This setting is not editable in the current configuration.");continue; }
        if(!seen.insert(edit.key).second) { result.errors[edit.key]=Text("The settings batch contains a duplicate field.");continue; }
        if(edit.intent==Intent::RemoveOverride)continue;
        const bool typed = Type(edit.key)==Kind::Boolean ? std::holds_alternative<bool>(edit.value) : Type(edit.key)==Kind::Integer ? std::holds_alternative<int64_t>(edit.value) : std::holds_alternative<QString>(edit.value);
        if(!typed) { result.errors[edit.key]=Text("The setting has the wrong value type.");continue; }
        if(const auto* text=std::get_if<QString>(&edit.value);text&&(text->size()>32768||text->contains(QChar::Null)))result.errors[edit.key]=Text("The setting text exceeds its budget or contains an invalid character.");
        if (edit.remembered) {
            if (edit.remembered->index() != edit.value.index()) result.errors[edit.key] = Text("The remembered setting has the wrong value type.");
            if (const auto* text = std::get_if<QString>(&*edit.remembered); text && (text->size() > 32768 || text->contains(QChar::Null))) result.errors[edit.key] = Text("The remembered setting exceeds its text budget.");
        }
    }
    return result;
}
Constraints Inspect(const ArgsManager& args)
{
    Constraints result;
    result.binds = !args.GetArgs("-bind").empty() || !args.GetArgs("-whitebind").empty();
    result.listen_onion_explicit = args.GetBoolArg("-listenonion",true);
    const auto networks = args.GetArgs("-onlynet"); result.only_onion = std::find(networks.begin(),networks.end(),"onion") != networks.end();
    result.inbound_onion = args.GetBoolArg("-listen",true) && args.GetBoolArg("-listenonion",true);
    result.indexes = args.GetBoolArg("-txindex",false) || args.GetBoolArg("-txospenderindex",false);
    result.reindex_chainstate = args.GetBoolArg("-reindex-chainstate",false); result.dns = args.GetBoolArg("-dns",true);
    const auto proxies=args.GetArgs("-proxy");
    result.structured_proxy=proxies.size()>1||std::any_of(proxies.begin(),proxies.end(),[](const auto& value){return value.find('=')!=std::string::npos;});
    if(result.structured_proxy)for(const auto& value:proxies){
        const auto separator=value.rfind('=');const auto endpoint=QString::fromStdString(value.substr(0,separator));
        const auto network=separator==std::string::npos?std::string{}:value.substr(separator+1);
        const auto type=network.empty()?NET_UNROUTABLE:ParseNetwork(network);
        if(separator!=std::string::npos&&(network.empty()||(type!=NET_IPV4&&type!=NET_IPV6&&type!=NET_ONION&&type!=NET_CJDNS))){result.proxy_error=Text("The current proxy list contains an unrecognized network.");break;}
        const bool enabled=!endpoint.isEmpty()&&endpoint!=QStringLiteral("0");
        if(enabled){
            if(const auto error=QmlCoreSettings::ProxyValidationError(endpoint);!error.isEmpty()){result.proxy_error=error;break;}
            if(!result.dns&&QmlCoreSettings::ProxyRequiresNameLookup(endpoint)){result.proxy_error=Text("This proxy requires name lookup, but the effective configuration disables DNS.");break;}
        }
        // The public option grammar applies a plain item to all routes; a
        // later network-specific item replaces only that named route.
        if(network.empty()||type==NET_ONION)result.proxy_onion_route=enabled;
    }

    const int64_t cluster = args.GetIntArg("-limitclustersize",kernel::MemPoolLimits{}.cluster_size_vbytes / 1000);
    result.minimum_mempool_mb = cluster <= 0 || cluster > (std::numeric_limits<int64_t>::max() - 999) / 40 ? std::numeric_limits<int64_t>::max() : (cluster * 40 + 999) / 1000;
    return result;
}
} // namespace

namespace {
Validation Policy(const Snapshot& candidate,const Batch& batch,const Constraints& constraints)
{
    Validation result;
    if(!Boolean(candidate,Key::Listen)&&(constraints.binds||constraints.listen_onion_explicit))result.errors[Key::Listen]=Text("Listening cannot be disabled with bind, whitebind or listenonion enabled.");
    const auto proxy=String(candidate,Key::Proxy),onion=String(candidate,Key::Onion);
    const auto enabled=[](const QString& value){return !value.isEmpty()&&value!="0";};
    if(!constraints.proxy_error.isEmpty())result.errors[Key::Proxy]=constraints.proxy_error;
    for (const auto key : {Key::Proxy,Key::Onion}) {
        if(key==Key::Proxy&&constraints.structured_proxy)continue;
        const auto text = String(candidate,key);
        if (!enabled(text)) continue;
        if (const auto error = QmlCoreSettings::ProxyValidationError(text); !error.isEmpty()) result.errors[key] = error;
        else if (!constraints.dns && QmlCoreSettings::ProxyRequiresNameLookup(text)) result.errors[key] = Text("This proxy requires name lookup, but the effective configuration disables DNS.");
    }
    const bool onion_route=constraints.structured_proxy?constraints.proxy_onion_route:enabled(proxy);
    if(constraints.only_onion&&(onion=="0"||(!enabled(onion)&&!onion_route&&!constraints.inbound_onion)))result.errors[Key::Onion]=Text("The effective onlynet=onion policy requires a Tor proxy or an inbound onion service.");
    const auto prune=Integer(candidate,Key::Prune);
    if(prune<0||(prune>1&&prune<550))result.errors[Key::Prune]=Text("Use disabled, manual pruning, or a target of at least 550 MiB.");
    if(prune>0&&(constraints.indexes||constraints.reindex_chainstate))result.errors[Key::Prune]=Text("Pruning conflicts with the effective index or reindex-chainstate configuration.");
    if(prune==0&&constraints.pruned_chain) {result.requires_redownload=true;if(!batch.accept_redownload)result.errors[Key::Prune]=Text("Returning to full storage after pruning may require rebuilding and downloading the blockchain again. Confirm this recovery requirement before saving.");}
    if(Integer(candidate,Key::Mempool)<0||(Integer(candidate,Key::Mempool)>0&&Integer(candidate,Key::Mempool)<constraints.minimum_mempool_mb))result.errors[Key::Mempool]=Text("The mempool limit is smaller than the effective cluster policy permits.");
    // These preserve the pinned Core scalar representations, not RAM budgets.
    if(Integer(candidate,Key::Mempool)>std::numeric_limits<int64_t>::max()/1'000'000)result.errors[Key::Mempool]=Text("The mempool size exceeds its byte representation.");
    if(prune>static_cast<int64_t>(std::numeric_limits<uint64_t>::max()/(1024*1024)))result.errors[Key::Prune]=Text("The pruning target exceeds its byte representation.");
    if(Integer(candidate,Key::Threads)<std::numeric_limits<int>::min()||Integer(candidate,Key::Threads)>std::numeric_limits<int>::max())result.errors[Key::Threads]=Text("The thread setting is too large.");
    const auto editing=[&batch](Key key){return std::any_of(batch.edits.begin(),batch.edits.end(),[key](const auto& edit){return edit.key==key&&edit.intent==Intent::ExplicitOverride;});};
    // Core raises a smaller -dbcache to its minimum and starts, so only an
    // edit made here is held to it; a configured value never blocks a save.
    if(editing(Key::Dbcache)&&Integer(candidate,Key::Dbcache)<4)result.errors[Key::Dbcache]=Text("The database cache must be at least 4 MiB.");
    const bool editing_signer=editing(Key::Signer);
    const auto signer=String(candidate,Key::Signer);if(editing_signer&&!signer.isEmpty())if(const auto error=signerdiscovery::PathError(signer);!error.isEmpty())result.errors[Key::Signer]=error;
    return result;
}
} // namespace
Validation Validate(const Snapshot& candidate,const Batch& batch,const Constraints& constraints)
{
    const auto shape=Shape(candidate,batch);return shape.valid()?Policy(candidate,batch,constraints):shape;
}
Check ReviewCurrent(ArgsManager& args)
{
    try {
        const auto source=Capture(args);const auto network=args.GetChainTypeString();auto current=CapturedArgs(source,network);
        if(current->GetSettingsPath(nullptr)&&!SameSavedValues(source.rw_settings,Disk(*current)))return {false,{}, {},Text("Active overrides and the settings file disagree. Refresh after the other writer finishes.")};
        const auto snapshot=Project(*current,source,network);auto next=NextArgs(source,network);
        const auto candidate=Project(*next,source,network);
        const auto checked=Policy(candidate,{},Inspect(*next));
        return {checked.valid(),snapshot,checked,checked.valid()?QString{}:checked.errors.begin()->second};
    }catch(const std::exception& error){return {false,{}, {},QString::fromUtf8(error.what()).left(4096)};}
    catch(...){return {false,{}, {},Text("The current configuration could not be checked.")};}
}
Snapshot Read(ArgsManager& args)
{
    try {
        const auto settings=Capture(args);const auto network=args.GetChainTypeString();auto current=CapturedArgs(settings,network);
        if (current->GetSettingsPath(nullptr) && !SameSavedValues(settings.rw_settings, Disk(*current))) return {{},{},false,Text("Active overrides and the settings file disagree. Refresh after the other writer finishes, or reload the saved settings explicitly.")};
        return Project(*current,settings,network);
    }
    catch(const std::exception& error){return {{},{},false,QString::fromUtf8(error.what()).left(4096)};}
    catch(...){return {{},{},false,Text("The effective settings could not be read.")};}
}
namespace {
struct Prepared {
    Result result;
    std::map<std::string,std::optional<common::SettingsValue>> writes;
    std::unique_ptr<ArgsManager> next;
    std::string network;
    bool ready{false};
};
Prepared Prepare(ArgsManager& args,const Snapshot& original,const Batch& batch,bool verify_disk=true)
{
    Prepared prepared;
    if(!original.ready||batch.base_revision!=original.revision||batch.edits.empty()||batch.edits.size()>KEYS.size()){prepared.result.error=Text("The settings could not be checked. Refresh and try again.");return prepared;}
    const auto source=Capture(args);prepared.network=args.GetChainTypeString();const auto& network=prepared.network;
    if (verify_disk && !SameSavedValues(source.rw_settings, Disk(args))) { prepared.result.outcome=Outcome::Conflict;prepared.result.error=Text("The settings file changed independently. Reload and review it before saving.");return prepared; }
    if(Revision(source,network)!=original.revision){prepared.result.outcome=Outcome::Conflict;prepared.result.snapshot=Read(args);prepared.result.error=Text("The effective configuration changed. Review the settings before saving.");return prepared;}
    prepared.result.validation = Shape(original, batch);
    if (!prepared.result.validation.valid()) { prepared.result.snapshot = original; prepared.result.error = prepared.result.validation.errors.begin()->second; return prepared; }
    auto& writes=prepared.writes;
    for (const auto& edit : batch.edits) {
        const auto name = Name(edit.key).toStdString();
        writes[name] = edit.intent == Intent::RemoveOverride ? std::nullopt : std::make_optional(QmlCoreSettings::LegacyCompatibleRwSettingValue(Name(edit.key), Stored(edit.key, edit.value)));
        if (edit.key == Key::Prune || edit.key == Key::Proxy || edit.key == Key::Onion) {
            const auto previous = name + "-prev";
            const bool enabled = edit.intent != Intent::RemoveOverride && Enabled(edit.key, edit.value);
            if (edit.intent == Intent::RemoveOverride || enabled) writes[previous] = std::nullopt;
            else {
                const auto* field = Find(original, edit.key);
                const auto memory = edit.remembered ? edit.remembered : field ? Enabled(edit.key, field->effective) ? std::make_optional(field->effective) : field->remembered : std::nullopt;
                if (memory) writes[previous] = QmlCoreSettings::LegacyCompatibleRwSettingValue(QString::fromStdString(previous), Stored(edit.key,*memory));
            }
        }
    }
    const auto apply = [&](common::Settings& settings) {
        for (const auto& [name,value] : writes) {
            if (value) settings.rw_settings[name] = *value;
            else settings.rw_settings.erase(name);
        }
    };
    auto proposed=source; apply(proposed);
    prepared.next=NextArgs(proposed,network);const auto& next=prepared.next;const auto candidate=Project(*next,proposed,network);auto constraints=Inspect(*next);constraints.pruned_chain=Integer(original,Key::Prune)>0;prepared.result.validation=Validate(candidate,batch,constraints);
    if(!prepared.result.validation.valid()){prepared.result.snapshot=original;prepared.result.error=prepared.result.validation.errors.begin()->second;return prepared;}
    prepared.ready=true;
    return prepared;
}
} // namespace
Check ReviewStaged(ArgsManager& args,const Batch& input)
{
    try {
        const auto source=Capture(args);const auto network=args.GetChainTypeString();auto current=CapturedArgs(source,network);
        const auto original=Project(*current,source,network);
        if(!original.persistence_available)return {false,original,{},Text("Persistent settings are disabled in this profile.")};
        if(input.edits.empty()){
            auto next=NextArgs(source,network);const auto candidate=Project(*next,source,network);
            const auto checked=Policy(candidate,input,Inspect(*next));
            return {checked.valid(),original,checked,checked.valid()?QString{}:checked.errors.begin()->second};
        }
        auto batch=input;batch.base_revision=original.revision;
        const auto prepared=Prepare(args,original,batch,false);
        return {prepared.ready,prepared.result.snapshot,prepared.result.validation,prepared.result.error};
    }catch(const std::exception& error){return {false,{}, {},QString::fromUtf8(error.what()).left(4096)};}
    catch(...){return {false,{}, {},Text("The staged startup configuration could not be checked.")};}
}

Check Review(ArgsManager& args,const Snapshot& original,const Batch& batch)
{
    try {const auto prepared=Prepare(args,original,batch);return {prepared.ready,prepared.result.snapshot,prepared.result.validation,prepared.result.error};}
    catch(const std::exception& error){return {false,{}, {},QString::fromUtf8(error.what()).left(4096)};}
    catch(...){return {false,{}, {},Text("The effective batch could not be checked.")};}
}
Result Commit(interfaces::Node& node,ArgsManager& args,const Snapshot& original,const Batch& batch)
{
    Result result;
    bool write_attempted{false};
    try {
    auto prepared=Prepare(args,original,batch);
    result=std::move(prepared.result);
    if(!prepared.ready)return result;
    const auto& writes=prepared.writes;const auto& next=prepared.next;const auto& network=prepared.network;
    const auto apply=[&](common::Settings& settings){for(const auto& [name,value]:writes){if(value)settings.rw_settings[name]=*value;else settings.rw_settings.erase(name);}};
    QStringList keys;for(const auto& [name,value]:writes)keys.push_back(QString::fromStdString(name));bool conflict{false};
    write_attempted = true;
    const auto persistence = QmlCoreSettings::PersistTransaction(args, keys, [&] {
        bool accepted{false};
        args.LockSettings([&](common::Settings& current) {
            if (Revision(current, network) != original.revision) { conflict = true; return; }
            apply(current);
            accepted = true;
        });
        return accepted;
    }, [&] {
        const auto actual = Capture(args);
        const auto disk = Disk(args);
        for (const auto& [name,value] : writes) {
            const auto it = actual.rw_settings.find(name);
            const bool matches = value ? it != actual.rw_settings.end() && it->second.write() == value->write() : it == actual.rw_settings.end();
            if (!matches) throw std::runtime_error("Another writer changed the batch before its receipt. Refresh before retrying.");
            const auto stored = disk.find(name);
            const bool on_disk = value ? stored != disk.end() && stored->second.write() == value->write() : stored == disk.end();
            if (!on_disk) throw std::runtime_error("The settings file no longer contains the whole captured batch. Refresh before retrying.");
        }
        for (const auto key : KEYS) {
            const auto item = disk.find(Name(key).toStdString());
            result.persisted_values[key] = item == disk.end() ? std::nullopt : std::make_optional(Scalar(key,item->second));
            const auto memory = disk.find(Name(key).toStdString() + "-prev");
            result.remembered_values[key] = memory == disk.end() ? std::nullopt : std::make_optional(Scalar(key,memory->second));
        }
        result.persisted = true;
        for (const auto& edit : batch.edits) {
            if (edit.key == Key::Natpmp) node.mapPort(next->GetBoolArg("-natpmp",false));
            if (edit.key == Key::Signer) node.forceSetting("signer",next->GetSetting("-signer"));
        }
        result.snapshot = Read(args);
        if (!result.snapshot.ready) throw std::runtime_error("The saved settings could not be read back.");
        result.runtime_confirmed = true;
    });
    result.write_completed = persistence.persisted;
    result.error = persistence.error;
    result.outcome = persistence.completed ? Outcome::Applied : result.write_completed ? Outcome::Uncertain : conflict ? Outcome::Conflict : Outcome::Failed;
    if (result.write_completed && !persistence.completed) result.error = Text("The settings file was written, but it may not be complete. Refresh before trying again.") + QLatin1Char(' ') + result.error;
    if (!result.snapshot.ready) result.snapshot = Read(args);

    return result;
    } catch (const std::exception& error) {
        result.outcome = write_attempted ? Outcome::Uncertain : Outcome::Failed;
        result.error = QString::fromUtf8(error.what()).left(4096);
        return result;
    } catch (...) {
        result.outcome = write_attempted ? Outcome::Uncertain : Outcome::Failed;
        result.error = Text("The settings operation could not be completed. Refresh before retrying.");
        return result;
    }
}
} // namespace effectivesettings
