// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/options_model.h>
#include <qml/signerdiscovery.h>
#include <qml/models/signerdiscoverymodel.h>

#include <common/args.h>
#include <common/settings.h>
#include <common/system.h>
#include <interfaces/node.h>
#include <mapport.h>
#include <qml/bitcoinunits.h>
#include <net.h>
#include <qml/core_settings.h>
#include <node/chainstatemanager_args.h>
#include <node/caches.h>
#include <qml/datadir.h>
#include <qml/guiconstants.h>
#include <qml/legacy_settings_migration.h>
#include <univalue.h>

#include <cassert>
#include <stdexcept>
#include <limits>
#include <tuple>
#include <utility>

#include <QDebug>
#include <QPointer>
#include <QQmlEngine>
#include <map>
#include <qml/guipresentation.h>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QScopeGuard>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

namespace {

constexpr const char* MONEY_FONT_EMBEDDED{"embedded"};
constexpr const char* MONEY_FONT_BEST_SYSTEM{"best_system"};

int NormalizeDisplayUnit(int display_unit)
{
    return display_unit >= 0 && display_unit <= 3 ? display_unit : 0;
}

bool IsThirdPartyTransactionUrlSchemeAllowed(const QUrl& url)
{
    const QString scheme = url.scheme().toLower();
    return scheme == QStringLiteral("http") || scheme == QStringLiteral("https");
}

QVariantMap CoreSettingStatusesForNames(const QVariantMap& statuses, const QStringList& names)
{
    QVariantMap subset;
    for (const QString& name : names) {
        const auto it = statuses.constFind(name);
        if (it != statuses.constEnd()) {
            subset.insert(name, *it);
        }
    }
    return subset;
}
} // namespace

struct OptionsLinksState {std::shared_ptr<const QVariantList> links,next;};
struct OptionsQmlModel::LinksRegistry {std::map<quint64,std::shared_ptr<OptionsLinksState>> views;};
namespace {
QVariantList BuildTransactionLinks(const QString& templates,const QString& txid)
{
    QVariantList links;
    const QStringList urls = templates.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    for (QString url : urls) {
        url = url.trimmed();
        if (!url.contains(QStringLiteral("%s"))) continue;
        const QUrl parsed{url, QUrl::StrictMode};
        if (!IsThirdPartyTransactionUrlSchemeAllowed(parsed)) continue;
        const QString host = parsed.host();
        if (host.isEmpty()) continue;
        QVariantMap link;
        link.insert(QStringLiteral("host"), host);
        link.insert(QStringLiteral("url"), url.replace(QStringLiteral("%s"), txid));
        links.push_back(link);
    }
    return links;
 }
}
OptionsTransactionLinks::OptionsTransactionLinks(OptionsQmlModel* source,std::weak_ptr<OptionsLinksState> state,quint64 id)
    :m_source(source),m_state(std::move(state)),m_id(id)
{
    connect(source,&OptionsQmlModel::thirdPartyTransactionUrlsChanged,this,&OptionsTransactionLinks::refresh);
}
OptionsTransactionLinks::~OptionsTransactionLinks(){if(m_source)m_source->removeLinks(m_id);}
void OptionsTransactionLinks::setTxid(const QString& txid){if(m_txid==txid)return;m_txid=txid;const QPointer<OptionsTransactionLinks> guard{this};refresh();if(guard)Q_EMIT txidChanged();}
void OptionsTransactionLinks::refresh(){if(m_source&&m_source->presentationOpen())m_source->refreshLinks(this);}
int OptionsTransactionLinks::rowCount(const QModelIndex& parent) const {return parent.isValid()||!m_source||!m_source->presentationOpen()?0:m_visible;}
QVariant OptionsTransactionLinks::data(const QModelIndex& index,int role) const {
    if(index.model()!=this||index.row()<0||index.row()>=rowCount())return {};
    const auto state=m_state.lock();if(!state||!state->links)return {};
    const auto map=state->links->at(index.row()).toMap();return map.value(role==HostRole?QStringLiteral("host"):role==UrlRole?QStringLiteral("url"):QString{});
}
void OptionsTransactionLinks::applyRows(quint64 revision){
    if(!m_source||!m_source->presentationOpen()||revision!=m_revision)return;
    const auto state=m_state.lock();if(!state)return;
    const QPointer<OptionsTransactionLinks> observer{this};
    for(int budget=0;budget<32;++budget){
        if(m_clearing&&m_visible){beginRemoveRows({},m_visible-1,m_visible-1);if(!observer)return;--m_visible;endRemoveRows();}
        else {
            if(m_clearing){state->links=std::move(state->next);m_clearing=false;}
            if(!state->links||m_visible==state->links->size()){m_pending=false;Q_EMIT resultChanged();return;}
            beginInsertRows({},m_visible,m_visible);if(!observer)return;++m_visible;endInsertRows();
        }
        if(!observer||!m_source||!m_source->presentationOpen()||revision!=m_revision)return;
    }
    if(!m_clearing&&state->links&&m_visible==state->links->size()){m_pending=false;Q_EMIT resultChanged();return;}
    try {m_source->m_services.post([observer,revision]{if(observer)observer->applyRows(revision);});}
    catch(...){m_pending=false;m_error=tr("Transaction links could not be presented.");Q_EMIT resultChanged();}
}
OptionsTransactionLinks* OptionsQmlModel::createTransactionLinks(){
    if(m_stopping)return nullptr;
    if(!m_links){setError(tr("Transaction link is unavailable."));return nullptr;}
    auto state=execution::RetainSnapshot<OptionsLinksState>(m_services.scheduler,[]{return std::make_unique<OptionsLinksState>();});
    if(!state){setError(tr("Transaction link is unavailable."));return nullptr;}const auto id=++m_next_links;m_links->views.emplace(id,state);
    auto* result=new OptionsTransactionLinks(this,state,id);QQmlEngine::setObjectOwnership(result,QQmlEngine::JavaScriptOwnership);return result;
}
void OptionsQmlModel::removeLinks(quint64 id){if(m_links)m_links->views.erase(id);}
void OptionsQmlModel::refreshLinks(OptionsTransactionLinks* view){
    if(m_stopping||!m_links)return;
    const auto revision=++view->m_revision;view->m_pending=true;view->m_error.clear();
    const QPointer<OptionsTransactionLinks> observer{view};const QPointer<OptionsQmlModel> source{this};const auto state=view->m_state;
    m_work->submit([templates=m_third_party_transaction_urls,txid=view->m_txid,scheduler=m_services.scheduler]{
            auto result=execution::RetainSnapshot<QVariantList>(scheduler,[&]{auto rows=BuildTransactionLinks(templates,txid);
                if(rows.size()>std::numeric_limits<int>::max())throw std::length_error("Transaction links exceed Qt row capacity");
                return std::make_unique<QVariantList>(std::move(rows));});
            if(!result)throw std::runtime_error("Transaction link capacity unavailable");
            return std::shared_ptr<const QVariantList>{std::move(result)};},
        [source,observer,state,revision](std::shared_ptr<const QVariantList> result){if(!source||!source->presentationOpen()||!observer||observer->m_revision!=revision)return;
            const auto cached=state.lock();if(!cached)return;cached->next=std::move(result);observer->m_clearing=true;observer->applyRows(revision);},
        [source,observer,revision](std::exception_ptr){if(!source||!source->presentationOpen()||!observer||observer->m_revision!=revision)return;
            observer->m_pending=false;observer->m_error=tr("Transaction links could not be prepared.");Q_EMIT observer->resultChanged();},
        {execution::Lane::Domain,{},execution::WorkClass::Observation});
    if(observer&&source&&source->presentationOpen())Q_EMIT observer->resultChanged();
}

struct OptionsQmlModel::Snapshot {
    QmlCoreSettings::Values core;
    QmlCoreSettings::Values effective_core;
    QVariantMap statuses;
    QmlCoreSettings::StatusProjection entry_statuses;
    preferences::WatchPaths watch;
    int dbcache;
    int mempool;
    int effective_mempool;
    int threads;
    int min_threads;
    QString signer;
    QString data_dir;
    QString default_data_dir;
    QUrl default_data_directory;
    QString custom_data_dir;
    QString custom_data_dir_display;
    QString language;
    int display_unit;
    QString transaction_urls;
    QString money_font;
    QStringList languages;
    bool hide_amounts;
};

OptionsQmlModel::Snapshot OptionsQmlModel::readSnapshot(interfaces::Node& node, ArgsManager& args)
{
    Snapshot result;
    result.languages=buildAvailableLanguages();
    result.core = QmlCoreSettings::LoadDisplayValues(node, args);
    result.effective_core = result.core;
    result.effective_mempool = SettingTo<int64_t>(QmlCoreSettings::DisplaySettingValue(node, args, QStringLiteral("maxmempool"))).value_or(DEFAULT_MAX_MEMPOOL_SIZE_MB);
    result.statuses = QmlCoreSettings::BuildCoreSettingStatuses(args, QmlCoreSettings::CoreSettingNames());
    result.entry_statuses=QmlCoreSettings::CaptureStatuses(CoreSettingStatusesForNames(result.statuses,QmlCoreSettings::OnboardingCoreSettingNames()));
    // Startup interactions remain effective until restart. Once the user saves
    // an override, show that saved request while retaining the source/status
    // explaining the running node's startup adjustment.
    const auto saved_override = [&](const QString& name) {
        const auto status = result.statuses.value(name).toMap();
        return status.value(QStringLiteral("startupAdjusted")).toBool()
            && status.value(QStringLiteral("hasRwSetting")).toBool();
    };
    const auto display_value = [&](const QString& name) {
        return saved_override(name) ? node.getPersistentSetting(name.toStdString())
                                    : QmlCoreSettings::DisplaySettingValue(node, args, name);
    };
    if (saved_override(QStringLiteral("listen"))) result.core.listen = SettingToBool(display_value(QStringLiteral("listen"))).value_or(result.core.listen);
    if (saved_override(QStringLiteral("natpmp"))) result.core.natpmp = SettingToBool(display_value(QStringLiteral("natpmp"))).value_or(result.core.natpmp);
    result.dbcache = SettingTo<int64_t>(display_value(QStringLiteral("dbcache"))).value_or(node::GetDefaultDBCache() / 1_MiB);
    result.mempool = SettingTo<int64_t>(display_value(QStringLiteral("maxmempool"))).value_or(DEFAULT_MAX_MEMPOOL_SIZE_MB);
    result.min_threads=-GetNumCores();
    result.threads = SettingTo<int64_t>(display_value(QStringLiteral("par"))).value_or(DEFAULT_SCRIPTCHECK_THREADS);
    result.signer = QString::fromStdString(SettingToString(QmlCoreSettings::DisplaySettingValue(node, args, QStringLiteral("signer"))).value_or(""));
    const QString gui_data_dir = QmlDataDir::ReadGuiDataDir();
    const QString active_data_dir = QString::fromStdString(args.GetDataDirBase().utf8string());
    result.default_data_dir = QmlDataDir::DefaultDataDirString();
    result.default_data_directory=QUrl::fromLocalFile(result.default_data_dir);
    result.data_dir = !active_data_dir.isEmpty() &&
        (QmlDataDir::HasExplicitDataDirArg(args) || QmlDataDir::IsDefaultDataDir(gui_data_dir))
        ? active_data_dir : gui_data_dir;
    if (!QmlDataDir::IsDefaultDataDir(result.data_dir)) result.custom_data_dir = result.data_dir;
    QSettings settings;
    settings.sync();
    result.watch = preferences::Paths(settings);
    result.language = QString::fromStdString(SettingToString(QmlCoreSettings::DisplaySettingValue(node, args, QStringLiteral("lang"))).value_or(""));
    const bool gui_language = result.language.isEmpty() && !QmlCoreSettings::IsCommandLineOverridden(args, QStringLiteral("lang"));
    if (gui_language) {
        result.language = settings.value(SettingsKeys::LANGUAGE,
            QmlLegacySettings::ReadLegacyGuiLanguage(QString::fromStdString(args.GetChainTypeString()))).toString();
    }
    const QString command_line_language = QString::fromStdString(args.GetArg("-lang", ""));
    if (!command_line_language.isEmpty()) result.language = command_line_language;
    const int fallback = QmlLegacySettings::ReadLegacyGuiDisplayUnit(QString::fromStdString(args.GetChainTypeString()), 0);
    const auto stored_unit = settings.value(SettingsKeys::DISPLAY_UNIT, fallback);
    bool unit_valid{false};
    const auto unit = stored_unit.toInt(&unit_valid);
    if (stored_unit.metaType().id() == QMetaType::Double && stored_unit.toDouble() != unit) unit_valid = false;
    result.display_unit = unit_valid ? NormalizeDisplayUnit(unit) : 0;
    if (settings.contains(SettingsKeys::DISPLAY_UNIT) && (!unit_valid || unit != result.display_unit)) {
        settings.setValue(SettingsKeys::DISPLAY_UNIT, result.display_unit);
        settings.sync();
    }
    // Validate app-controlled language data against the installed catalog.
    // An explicit CLI language remains the external Core request.
    if (gui_language && !result.languages.contains(result.language) && command_line_language.isEmpty()) {
        result.language.clear();
        if (settings.contains(SettingsKeys::LANGUAGE)) {
            settings.setValue(SettingsKeys::LANGUAGE, result.language);
            settings.sync();
        }
    }
    if (settings.status() != QSettings::NoError) throw std::runtime_error("Display preferences could not be read or repaired.");
    result.custom_data_dir_display=QmlDataDir::DisplayPath(result.custom_data_dir);
    result.transaction_urls = settings.value(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS, "").toString();
    result.money_font = settings.value(SettingsKeys::MONEY_FONT_CHOICE, MONEY_FONT_EMBEDDED).toString();
    if (result.money_font != MONEY_FONT_EMBEDDED && result.money_font != MONEY_FONT_BEST_SYSTEM) result.money_font = MONEY_FONT_EMBEDDED;
    result.hide_amounts = settings.value(SettingsKeys::HIDE_AMOUNTS, false).toBool();
    return result;
}

namespace {struct NativeSettingsWrite {quint64 revision;std::shared_ptr<std::atomic<int>> receipt;};}
OptionsQmlModel::OptionsQmlModel(interfaces::Node& node, ArgsManager& args, execution::SnapshotServices services)
    : m_node{node}, m_args{args}, m_services{std::move(services)}
{
    if(!m_services.scheduler) { m_executor=std::make_unique<BackendExecutor>(execution::Options{});m_services.scheduler=m_executor->scheduler(); }
    if(!m_services.post)m_services.post=presentation::QueuedPost();
    m_work=std::make_unique<execution::OwnedWork>(m_services);
    m_links=execution::RetainSnapshot<LinksRegistry>(m_services.scheduler,[]{return std::make_unique<LinksRegistry>();});
    m_core_settings.setAdmissionReady(false);m_core_settings.configureServices(m_services);
    resetDirtySnapshots();
    refreshCoreSettingStatuses();
    m_core_settings.setBeforeChangeHandler([this](CoreSettingsModel::ChangeOrigin) {
        m_core_change_dirty_snapshot = dirtySnapshot();
    });
    m_core_settings.setAfterChangeHandler([this](const QmlCoreSettings::Change& change, CoreSettingsModel::ChangeOrigin origin) {
        Q_UNUSED(origin);applyRuntimeCoreChange(change,m_core_change_dirty_snapshot);
    });
    const QPointer<OptionsQmlModel> observer{this};
    m_core_settings.setEffectPort([node=&m_node,args=&m_args](const QmlCoreSettings::Change& change,CoreSettingsModel::EffectContext opaque,std::shared_ptr<execution::Scheduler> scheduler,auto done,auto failed){
        const auto context=std::static_pointer_cast<const NativeSettingsWrite>(opaque);
        if(!change.accepted||(!QmlCoreSettings::ValuesChanged(change)&&!change.force_write)){if(context)context->receipt->store(change.accepted?SettingsApplied:SettingsFailed);done(CoreSettingsModel::EffectResult{});return;}
        const auto admission=execution::Submit(*scheduler,[node,args,scheduler,change,context]{
            try {
                QStringList names;if(change.setting_name.isEmpty()){
                    if(QmlCoreSettings::ProxyEnabledChanged(change)||QmlCoreSettings::ProxyAddressChanged(change))names<<QStringLiteral("proxy");
                    if(QmlCoreSettings::TorEnabledChanged(change)||QmlCoreSettings::TorAddressChanged(change))names<<QStringLiteral("onion");
                }else names<<change.setting_name;
                QStringList keys=names;for(const auto& name:names)keys<<name+QStringLiteral("-prev");
                QString error;const QmlCoreSettings::Session session{change.after};
                const bool saved=QmlCoreSettings::PersistSettings(*args,keys,[&]{for(const auto& name:names)if(!session.writeToArgs(*args,name))return false;return true;},&error);
                if(saved&&names.contains(QStringLiteral("natpmp")))node->mapPort(session.values().natpmp);
                if(context)context->receipt->store(saved?SettingsApplied:SettingsFailed);
                auto snapshot=execution::RetainSnapshot<Snapshot>(scheduler,[&]{return std::make_unique<Snapshot>(readSnapshot(*node,*args));});
                if(!snapshot)throw std::runtime_error("Settings effect facts capacity unavailable");
                return CoreSettingsModel::EffectResult{error,snapshot->core,snapshot->statuses,std::move(snapshot)};
            }catch(...){if(context&&context->receipt->load()==SettingsPending)context->receipt->store(SettingsUncertain);throw;}
        },done,failed,{execution::Lane::Core,{},execution::WorkClass::Action});
        if(!admission){if(context)context->receipt->store(SettingsFailed);failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));}
    },[this]()->CoreSettingsModel::EffectContext{
        auto receipt=std::make_shared<std::atomic<int>>(SettingsPending);m_operation_state=receipt;
        return std::make_shared<NativeSettingsWrite>(NativeSettingsWrite{++m_command_revision,std::move(receipt)});
    },[observer](CoreSettingsModel::EffectContext opaque,const CoreSettingsModel::EffectResult& result){
        if(!observer||observer->m_stopping)return;
        const auto context=std::static_pointer_cast<const NativeSettingsWrite>(opaque);
        if(!result.error.isEmpty()){observer->setError(result.error);if(!observer||observer->m_stopping)return;}
        if(!context||context->revision!=observer->m_command_revision)return;
        if(result.presentation)observer->applySnapshot(*std::static_pointer_cast<const Snapshot>(result.presentation),true);
        if(!observer||observer->m_stopping)return;
        if(observer&&!observer->m_stopping)Q_EMIT observer->settingsOperationChanged();
        if(observer&&!observer->m_stopping)observer->finishPreferenceRefresh();
    },[](CoreSettingsModel::EffectContext opaque){const auto context=std::static_pointer_cast<const NativeSettingsWrite>(opaque);
        if(context&&context->receipt->load()==SettingsPending)context->receipt->store(SettingsFailed);});
    connect(&m_core_settings,&CoreSettingsModel::operationChanged,this,[this]{if(m_stopping)return;const QPointer<OptionsQmlModel> guard{this};Q_EMIT settingsPendingChanged();if(guard&&!m_stopping)Q_EMIT settingsErrorChanged();if(guard&&!m_stopping)Q_EMIT pruneTargetInputChanged();if(guard&&!m_stopping)finishPreferenceRefresh();});
    runCommand([](interfaces::Node&, ArgsManager&) { return QString{}; });
}

SettingsEditorModel* OptionsQmlModel::createSettingsEditor()
{
    if(m_stopping)return nullptr;
    m_editors.removeIf([](const auto& editor){return !editor;});
    if(m_editors.size()>=16){setError(tr("Close another settings editor before opening a new one."));return nullptr;}
    SettingsEditorModel* editor{nullptr};
    const bool admitted=m_work->submitAsync<bool>([&](auto done,auto){
        auto ports=SettingsEditorModel::PublicPorts(m_node,m_args,m_services);
        ports.retired=[done]{done(true);};
        editor=new SettingsEditorModel(std::move(ports),this);
        return true;
    },[](bool){},[](std::exception_ptr){});
    if(!admitted)return nullptr;
    m_editors.push_back(editor);
    const auto previous=std::make_shared<QString>();
    connect(editor,&SettingsEditorModel::changed,this,[this,editor,previous]{
        if(m_stopping||!editor->lastSavePresent()||!editor->lastSavePersisted()||*previous==editor->lastSaveId())return;
        *previous=editor->lastSaveId();
        const QPointer<OptionsQmlModel> guard{this};
        runCommand([](interfaces::Node&,ArgsManager&){return QString{};});
        if(guard&&m_signer_model)m_signer_model->refresh();
    });
    connect(editor,&SettingsEditorModel::closed,editor,[editor]{const QPointer<SettingsEditorModel> guard{editor};editor->retire([guard]{if(guard)guard->deleteLater();});});
    return editor;
}
OptionsQmlModel::~OptionsQmlModel()
{
    closeAdmission();
    if(m_executor)m_executor->shutdown();
}
void OptionsQmlModel::closeAdmission()
{
    m_preference_watch.stop();
    m_stopping=true; m_pending=false; m_forwarded_signer_pending=false; m_validation_pending=false;m_signer_validation_pending=false; ++m_signer_validation_revision;
    m_work->close();m_links.reset();m_core_settings.closeAdmission();
    for(const auto& editor:m_editors)if(editor)editor->retire();
}
void OptionsQmlModel::beginShutdown()
{
    if(std::exchange(m_shutdown_started,true))return;
    closeAdmission();
    const QPointer<OptionsQmlModel> observer{this};
    if(m_executor) {
        m_executor->whenDrained(this,[observer] {if(observer)Q_EMIT observer->shutdownFinished();});
        m_executor->shutdown();
    } else {
        const auto remaining=std::make_shared<int>(2);const auto finish=[observer,remaining]{if(--*remaining==0&&observer)Q_EMIT observer->shutdownFinished();};
        m_work->close(finish);m_core_settings.whenEffectsDrained(finish);
    }
}

void OptionsQmlModel::setError(const QString& error)
{
    if (m_error == error) return;
    m_error = error;
    Q_EMIT settingsErrorChanged();
}

void OptionsQmlModel::setObservationError(const QString& error)
{
    if (m_observation_error == error) return;
    m_observation_error = error;
    Q_EMIT settingsErrorChanged();
}

void OptionsQmlModel::setPending(bool pending)
{
    if (m_pending == pending) return;
    m_pending = pending;
    refreshCoreSettingStatuses();
    Q_EMIT settingsPendingChanged();
}

void OptionsQmlModel::applySnapshot(Snapshot snapshot,bool core_adopted)
{
    const QPointer<OptionsQmlModel> observer{this};
    if(!core_adopted){
        const auto expected=m_core_settings.userRevision();const auto revision=m_command_revision;
        m_core_settings.applyPreviewValuesPreservingTouched(snapshot.core,snapshot.entry_statuses,
            [observer,snapshot,revision](bool accepted){if(!observer||observer->m_stopping||revision!=observer->m_command_revision)return;
                if(accepted||observer->m_ready)observer->applySnapshot(snapshot,true);else observer->setError(tr("The Core settings snapshot could not be adopted."));},true,expected);
        return;
    }
    const bool initial = !m_ready;
    const DirtySnapshot before = dirtySnapshot();
    const bool dbcache_changed = m_dbcache_size_mib != snapshot.dbcache;
    const bool mempool_changed = m_max_mempool_size_mb != snapshot.mempool;
    const bool threads_changed = m_script_threads != snapshot.threads;
    const bool limits_changed=m_min_script_threads!=snapshot.min_threads;
    const auto signer_path = m_signer_model && m_signer_model->configurationKnown() ? m_signer_model->effectivePath() : snapshot.signer;
    const bool signer_changed = m_external_signer_path != signer_path;
    const bool data_dir_changed = m_dataDir != snapshot.data_dir;
    const bool default_data_dir_changed = m_default_data_dir != snapshot.default_data_dir;
    const bool custom_data_dir_changed = m_custom_datadir_string != snapshot.custom_data_dir;
    const bool language_changed = m_language != snapshot.language;
    const bool display_unit_changed = m_display_unit != snapshot.display_unit;
    const bool transaction_urls_changed = m_third_party_transaction_urls != snapshot.transaction_urls;
    const bool hide_amounts_changed = m_hide_amounts != snapshot.hide_amounts;
    const bool money_font_changed = m_money_font_choice != snapshot.money_font;
    m_core_setting_statuses = std::move(snapshot.statuses);m_entry_statuses=std::move(snapshot.entry_statuses);
    m_dbcache_size_mib = snapshot.dbcache;
    m_max_mempool_size_mb = snapshot.mempool;
    m_script_threads = snapshot.threads;m_min_script_threads=snapshot.min_threads;
    m_external_signer_path = signer_path;
    m_dataDir = std::move(snapshot.data_dir);
    m_default_data_dir = std::move(snapshot.default_data_dir);
    m_default_data_directory=std::move(snapshot.default_data_directory);
    m_custom_datadir_string = std::move(snapshot.custom_data_dir);
    m_custom_datadir_display=std::move(snapshot.custom_data_dir_display);
    m_language = std::move(snapshot.language);
    m_display_unit = snapshot.display_unit;
    m_third_party_transaction_urls = std::move(snapshot.transaction_urls);
    m_money_font_choice = std::move(snapshot.money_font);
    m_available_languages=std::move(snapshot.languages);
    m_preference_watch.update(std::move(snapshot.watch));
    m_hide_amounts = snapshot.hide_amounts;
    m_ready = true;m_core_settings.setAdmissionReady(true);
    if(!observer||m_stopping)return;
    if (initial) {
        resetDirtySnapshots();
        m_initial_core_values = snapshot.effective_core;
        m_initial_max_mempool_size_mb = snapshot.effective_mempool;
    }
    if(initial)Q_EMIT availableLanguagesChanged();
    if(!observer||m_stopping)return;
    if (dbcache_changed) Q_EMIT dbcacheSizeMiBChanged(m_dbcache_size_mib);
    if(!observer||m_stopping)return;
    if (mempool_changed) Q_EMIT maxMempoolSizeMBChanged(m_max_mempool_size_mb);
    if(!observer||m_stopping)return;
    if(limits_changed)Q_EMIT threadLimitsChanged();
    if(!observer||m_stopping)return;
    if (threads_changed) Q_EMIT scriptThreadsChanged(m_script_threads);
    if(!observer||m_stopping)return;
    if (signer_changed) Q_EMIT externalSignerPathChanged(m_external_signer_path);
    if(!observer||m_stopping)return;
    if (data_dir_changed) Q_EMIT dataDirChanged(m_dataDir);
    if(!observer||m_stopping)return;
    if (default_data_dir_changed) Q_EMIT dataDirDefaultsChanged();
    if(!observer||m_stopping)return;
    if (custom_data_dir_changed) Q_EMIT customDataDirStringChanged(m_custom_datadir_string);
    if(!observer||m_stopping)return;
    if (language_changed) Q_EMIT languageChanged();
    if(!observer||m_stopping)return;
    if (display_unit_changed) Q_EMIT displayUnitChanged(m_display_unit);
    if(!observer||m_stopping)return;
    if (transaction_urls_changed) Q_EMIT thirdPartyTransactionUrlsChanged();
    if(!observer||m_stopping)return;
    if (hide_amounts_changed) Q_EMIT hideAmountsChanged();
    if(!observer||m_stopping)return;
    if (money_font_changed) {
        Q_EMIT moneyFontChoiceChanged();
    if(!observer||m_stopping)return;
        Q_EMIT moneyFontChanged();
    if(!observer||m_stopping)return;
    }
    refreshCoreSettingStatuses();
    if(!observer||m_stopping)return;
    emitDirtySignals(before);
    if(!observer||m_stopping)return;
    if (initial) Q_EMIT settingsReadyChanged();
    if(!observer||m_stopping)return;
}


void OptionsQmlModel::refreshDisplaySettings()
{
    if (m_stopping) return;
    m_preference_refresh_requested = true;
    finishPreferenceRefresh();
}
// Do not resume from synchronous admission failure: report it, retain demand,
// and wait for a fresh trigger. Successful asynchronous outcomes can continue.
void OptionsQmlModel::finishPreferenceRefresh()
{
    if (m_stopping || m_pending_commands || m_submitting_commands || m_core_settings.pending() || !m_preference_refresh_requested) return;
    m_preference_refresh_requested = false;
    runCommand([](interfaces::Node&, ArgsManager&) { return QString{}; },/*observation=*/true);
}

void OptionsQmlModel::runCommand(std::function<QString(interfaces::Node&, ArgsManager&)> command,bool observation)
{
    if(m_stopping)return;
    const QPointer<OptionsQmlModel> observer{this};
    ++m_submitting_commands;
    const auto submitting=qScopeGuard([observer]{if(observer)--observer->m_submitting_commands;});
    auto receipt=std::make_shared<std::atomic<int>>(SettingsPending);
    if(!observation)m_operation_state=receipt;
    const quint64 revision=observation?m_command_revision:++m_command_revision;
    // Refreshing files is an observation, not a new user operation. Retain the
    // original effect's identity, terminal receipt and diagnostic.
    ++m_pending_commands;
    const bool clear_error=!observation&&(!m_error.isEmpty()||!m_observation_error.isEmpty());
    const bool was_pending=m_pending; m_pending=true;
    if(clear_error){m_error.clear();m_observation_error.clear();}
    const bool admitted=m_work->submit([node=&m_node,args=&m_args,command=std::move(command),receipt] {
        QString error;
        try {error=command(*node,*args);} catch (...) {receipt->store(SettingsUncertain);throw;}
        receipt->store(error.isEmpty()?SettingsApplied:SettingsFailed);
        return std::make_pair(readSnapshot(*node,*args),error);
    },[observer,revision,observation](auto result) {
        if(!observer||observer->m_stopping)return;
        if(revision==observer->m_command_revision) {
            if(observation)observer->setObservationError({});
            if(!observer||observer->m_stopping)return;
            if(revision==observer->m_command_revision)observer->applySnapshot(std::move(result.first));
        }
        if(!observer||observer->m_stopping)return;
        if(revision==observer->m_command_revision&&!result.second.isEmpty())observer->setError(result.second);
        if(!observer||observer->m_stopping)return;
        if(--observer->m_pending_commands==0)observer->setPending(false);
        if(observer&&!observer->m_stopping&&!observation&&revision==observer->m_command_revision)Q_EMIT observer->settingsOperationChanged();
        if(observer&&!observer->m_stopping)observer->finishPreferenceRefresh();
    },[observer,receipt,revision,observation](std::exception_ptr exception) {
        if(receipt->load()==SettingsPending)receipt->store(SettingsFailed);
        if(!observer||observer->m_stopping)return;
        QString error;try{std::rethrow_exception(exception);}catch(const std::exception& e){error=QString::fromUtf8(e.what());}
        catch(...){error=tr("Unable to load or save settings.");}
        if(observation) {
            if(revision==observer->m_command_revision)observer->setObservationError(error);
        } else if(revision==observer->m_command_revision) {
            observer->setError(error);
        }
        if(!observer||observer->m_stopping)return;
        if(--observer->m_pending_commands==0)observer->setPending(false);
        if(observer&&!observer->m_stopping&&!observation&&revision==observer->m_command_revision)Q_EMIT observer->settingsOperationChanged();
    },{execution::Lane::Core,{},execution::WorkClass::Action});
    if(!admitted)receipt->store(SettingsFailed);
    if(!observer||m_stopping)return;
    if(clear_error)Q_EMIT settingsErrorChanged();
    if(!observer||m_stopping)return;
    refreshCoreSettingStatuses();if(!observer||m_stopping)return;
    if(!was_pending&&m_pending)Q_EMIT settingsPendingChanged();
    if(!observer||m_stopping)return;
    if(!observation)Q_EMIT settingsOperationChanged();
}

bool OptionsQmlModel::connectionSettingsDirty() const
{
    const QmlCoreSettings::Values& values = m_core_settings.values();
    return values.listen != m_initial_core_values.listen || values.server != m_initial_core_values.server;
}

bool OptionsQmlModel::storageSettingsDirty() const
{
    const QmlCoreSettings::Values& values = m_core_settings.values();
    return !QmlCoreSettings::CoreSettingValuesEqual(QStringLiteral("prune"), QmlCoreSettings::PruneSetting(values), QmlCoreSettings::PruneSetting(m_initial_core_values));
}

bool OptionsQmlModel::developerSettingsDirty() const
{
    return m_dbcache_size_mib != m_initial_dbcache_size_mib ||
           m_script_threads != m_initial_script_threads;
}

bool OptionsQmlModel::restartRequired() const
{
    return connectionSettingsDirty() ||
           storageSettingsDirty() ||
           developerSettingsDirty() ||
           mempoolSettingsDirty() ||
           proxySettingsDirty();
}

QVariantMap OptionsQmlModel::coreSettingStatuses() const
{
    QVariantMap statuses = m_core_setting_statuses;
    if (!m_ready || m_stopping) {
        for (const auto& name : QmlCoreSettings::CoreSettingNames()) {
            auto status = statuses.value(name).toMap();
            status.insert(QStringLiteral("canEdit"), false);
            statuses.insert(name, status);
        }
    }
    return statuses;
}

QVariantMap OptionsQmlModel::coreSettingStatus(const QString& name) const
{
    return coreSettingStatuses().value(name).toMap();
}

void OptionsQmlModel::resetDirtySnapshots()
{
    m_initial_core_values = m_core_settings.values();
    m_initial_dbcache_size_mib = m_dbcache_size_mib;
    m_initial_max_mempool_size_mb = m_max_mempool_size_mb;
    m_initial_script_threads = m_script_threads;
    m_initial_external_signer_path = m_external_signer_path;
}

OptionsQmlModel::DirtySnapshot OptionsQmlModel::dirtySnapshot() const
{
    DirtySnapshot snapshot;
    snapshot.connection = connectionSettingsDirty();
    snapshot.storage = storageSettingsDirty();
    snapshot.developer = developerSettingsDirty();
    snapshot.mempool = mempoolSettingsDirty();
    snapshot.proxy = proxySettingsDirty();
    snapshot.wallet = walletSettingsDirty();
    snapshot.restart = restartRequired();
    return snapshot;
}

void OptionsQmlModel::emitDirtySignals(const DirtySnapshot& before)
{
    const QPointer<OptionsQmlModel> observer{this};
    if (connectionSettingsDirty() != before.connection) Q_EMIT connectionSettingsDirtyChanged();
    if(!observer||m_stopping)return;
    if (storageSettingsDirty() != before.storage) Q_EMIT storageSettingsDirtyChanged();
    if(!observer||m_stopping)return;
    if (developerSettingsDirty() != before.developer) Q_EMIT developerSettingsDirtyChanged();
    if(!observer||m_stopping)return;
    if (mempoolSettingsDirty() != before.mempool) Q_EMIT mempoolSettingsDirtyChanged();
    if(!observer||m_stopping)return;
    if (proxySettingsDirty() != before.proxy) Q_EMIT proxySettingsDirtyChanged();
    if(!observer||m_stopping)return;
    if (walletSettingsDirty() != before.wallet) Q_EMIT walletSettingsDirtyChanged();
    if(!observer||m_stopping)return;
    if (restartRequired() != before.restart) Q_EMIT restartRequiredChanged();
    if(!observer||m_stopping)return;
}


void OptionsQmlModel::applyRuntimeCoreChange(const QmlCoreSettings::Change& change,const DirtySnapshot& before)
{
    const QPointer<OptionsQmlModel> observer{this};QmlCoreSettings::EmitCoreSettingSignals(*this,change);if(observer&&!m_stopping&&m_ready)emitDirtySignals(before);
}

common::SettingsValue OptionsQmlModel::currentCoreSettingValue(const QString& name) const
{
    if (name == QStringLiteral("dbcache")) return m_dbcache_size_mib;
    if (name == QStringLiteral("par")) return m_script_threads;
    if (name == QStringLiteral("maxmempool")) return m_max_mempool_size_mb;
    if (name == QStringLiteral("signer")) return common::SettingsValue{m_external_signer_path.toStdString()};
    if (name == QStringLiteral("lang")) return common::SettingsValue{m_language.toStdString()};
    return {};
}

bool OptionsQmlModel::canEditCoreSetting(const QString& name) const
{
    if (!m_ready || m_stopping || m_validation_pending) return false;
    return coreSettingStatus(name).value(QStringLiteral("canEdit"), false).toBool();
}

bool OptionsQmlModel::writeCoreSettingOverride(const QString& name, const common::SettingsValue& value)
{
    if (!canEditCoreSetting(name)) return false;
    runCommand([name, value](interfaces::Node& node, ArgsManager& args) {
        QString error;
        const bool saved = QmlCoreSettings::PersistSettings(args, {name},
            [&] { return QmlCoreSettings::WriteCoreSettingOverride(args, name, value); }, &error);
        if (saved && name == QStringLiteral("signer")) node.forceSetting("signer", value);
        return error;
    });
    return true;
}

void OptionsQmlModel::refreshCoreSettingStatuses()
{
    const QPointer<OptionsQmlModel> observer{this};
    m_core_settings.setStatuses(m_entry_statuses);
    if(observer&&!m_stopping)Q_EMIT coreSettingStatusesChanged();
}

void OptionsQmlModel::setDbcacheSizeMiB(int new_dbcache_size_mib)
{
    if (!canEditCoreSetting(QStringLiteral("dbcache"))) return;
    if (new_dbcache_size_mib != m_dbcache_size_mib) {
        const DirtySnapshot before = dirtySnapshot();
        m_dbcache_size_mib = new_dbcache_size_mib;
        writeCoreSettingOverride(QStringLiteral("dbcache"), currentCoreSettingValue(QStringLiteral("dbcache")));
        Q_EMIT dbcacheSizeMiBChanged(new_dbcache_size_mib);
        emitDirtySignals(before);
    }
}

void OptionsQmlModel::setListen(bool new_listen)
{
    if (m_stopping) return;
    m_core_settings.changeListen(new_listen);
}

void OptionsQmlModel::setMaxMempoolSizeMB(int new_max_mempool_size_mb)
{
    if (!canEditCoreSetting(QStringLiteral("maxmempool"))) return;
    if (new_max_mempool_size_mb != m_max_mempool_size_mb) {
        const DirtySnapshot before = dirtySnapshot();
        m_max_mempool_size_mb = new_max_mempool_size_mb;
        writeCoreSettingOverride(QStringLiteral("maxmempool"), currentCoreSettingValue(QStringLiteral("maxmempool")));
        Q_EMIT maxMempoolSizeMBChanged(new_max_mempool_size_mb);
        emitDirtySignals(before);
    }
}

void OptionsQmlModel::setNatpmp(bool new_natpmp)
{
    if (m_stopping) return;
    m_core_settings.changeNatpmp(new_natpmp);
}

void OptionsQmlModel::setPrune(bool new_prune)
{
    if (m_stopping) return;
    m_core_settings.changePrune(new_prune);
}

void OptionsQmlModel::setPruneSizeGB(int new_prune_size_gb)
{
    if (m_stopping) return;
    m_core_settings.changePruneSizeGB(new_prune_size_gb);
}

void OptionsQmlModel::setScriptThreads(int new_script_threads)
{
    if (!canEditCoreSetting(QStringLiteral("par"))) return;
    if (new_script_threads != m_script_threads) {
        const DirtySnapshot before = dirtySnapshot();
        m_script_threads = new_script_threads;
        writeCoreSettingOverride(QStringLiteral("par"), currentCoreSettingValue(QStringLiteral("par")));
        Q_EMIT scriptThreadsChanged(new_script_threads);
        emitDirtySignals(before);
    }
}

void OptionsQmlModel::setServer(bool new_server)
{
    if (m_stopping) return;
    m_core_settings.changeServer(new_server);
}

void OptionsQmlModel::setProxyEnabled(bool enabled)
{
    if (m_stopping) return;
    m_core_settings.changeProxyEnabled(enabled);
}

void OptionsQmlModel::setProxyAddress(const QString& address)
{
    if (m_stopping) return;
    m_core_settings.changeProxyLocation(address);
}

void OptionsQmlModel::setTorEnabled(bool enabled)
{
    if (m_stopping) return;
    m_core_settings.changeTorEnabled(enabled);
}

void OptionsQmlModel::setTorAddress(const QString& address)
{
    if (m_stopping) return;
    m_core_settings.changeTorLocation(address);
}

QString OptionsQmlModel::defaultProxyAddress() const
{
    return m_core_settings.defaultProxyAddress();
}

namespace {
QString SaveGuiSetting(const QString& key, const QVariant& value)
{
    QSettings settings;
    const QVariant previous = settings.value(key);
    if (value.isValid()) settings.setValue(key, value);
    else settings.remove(key);
    settings.sync();
    if (settings.status() == QSettings::NoError) return {};
    if (previous.isValid()) settings.setValue(key, previous);
    else settings.remove(key);
    settings.sync();
    return QObject::tr("Unable to save application settings.");
}
} // namespace

void OptionsQmlModel::setExternalSignerPath(const QString& path)
{
    if (m_stopping) return;
    if (!canEditCoreSetting(QStringLiteral("signer"))) return;
    if(path.size()>32768){setError(tr("The signer path is too long."));return;}
    if (m_signer_model) {
        const QPointer<OptionsQmlModel> observer{this}; const auto model = m_signer_model;
        model->editPath(path); if (!observer || m_stopping || !model) return;
        m_forwarded_signer_command = true;
        model->apply();
        return;
    }
    const QString& normalized_path = path;
    if (normalized_path == m_external_signer_path) return;
    const auto before = dirtySnapshot();
    m_external_signer_path = normalized_path;
    const QPointer<OptionsQmlModel> observer{this};
    runCommand([original=normalized_path](interfaces::Node& node, ArgsManager& args) {
        const auto normalized_path=signerdiscovery::NormalizeCommand(original);
        QString error = signerdiscovery::PathError(normalized_path);
        if (!error.isEmpty()) return error;
        const common::SettingsValue value = normalized_path.isEmpty()
            ? common::SettingsValue{} : common::SettingsValue{normalized_path.toStdString()};
        if (QmlCoreSettings::PersistSettings(args, {QStringLiteral("signer")},
                [&] { return QmlCoreSettings::WriteCoreSettingOverride(args, QStringLiteral("signer"), value); }, &error)) {
            node.forceSetting("signer", value);
        }
        return error;
    });
    if(!observer||m_stopping)return;
    Q_EMIT externalSignerPathChanged(m_external_signer_path);
    if(observer&&!m_stopping)emitDirtySignals(before);
}

void OptionsQmlModel::setSignerDiscoveryModel(SignerDiscoveryModel* model)
{
    if (m_stopping || !model || m_signer_model) return;
    m_signer_model = model;
    const QPointer<SignerDiscoveryModel> original{model};
    const auto project = [this, original] {
        if (m_stopping || !original) return;
        const QPointer<OptionsQmlModel> observer{this}; const auto before = dirtySnapshot();
        const bool pending_changed = m_forwarded_signer_pending != original->saving();
        m_forwarded_signer_pending = original->saving();
        if (original->configurationKnown() && m_external_signer_path != original->effectivePath()) {
            m_external_signer_path = original->effectivePath();
            Q_EMIT externalSignerPathChanged(m_external_signer_path);
            if (!observer || m_stopping || !original) return;
        }
        if (m_forwarded_signer_command && !m_forwarded_signer_pending) {
            m_forwarded_signer_command = false; setError(original->editError().message);
            if (!observer || m_stopping) return;
        }
        if (pending_changed) { Q_EMIT settingsPendingChanged(); if (!observer || m_stopping) return; }
        emitDirtySignals(before);
    };
    connect(model, &SignerDiscoveryModel::changed, this, project);
    connect(model, &SignerDiscoveryModel::editedChanged, this, project);
    project();
}

QString OptionsQmlModel::externalSignerPathValidationError(const QString& path) const
{
    return path == m_validated_signer_path ? m_signer_validation_error : QString{};
}

void OptionsQmlModel::requestExternalSignerPathValidation(const QString& path)
{
    if (m_stopping) return;
    m_requested_signer_path = path;
    ++m_signer_validation_revision;
    m_signer_validation_pending = true;
    m_signer_validation_error.clear();
    const QPointer<OptionsQmlModel> observer{this};
    if (!m_signer_validation_in_flight) startSignerValidation();
    if(observer&&!m_stopping)Q_EMIT signerPathValidationChanged();
}

void OptionsQmlModel::queueSignerValidation()
{
    if (m_stopping) return;
    m_signer_validation_in_flight = true;
    const QPointer<OptionsQmlModel> observer{this};
    presentation::ReliablePost(m_services.post)([observer] {
        if (observer && !observer->m_stopping) observer->startSignerValidation();
    });
}

void OptionsQmlModel::startSignerValidation()
{
    if(m_stopping)return;
    const QString normalized = m_requested_signer_path;
    const quint64 revision = m_signer_validation_revision;
    m_signer_validation_in_flight = true;
    m_work->submit( [normalized] { return normalized.size()>32768?tr("The signer path is too long."):signerdiscovery::PathError(signerdiscovery::NormalizeCommand(normalized)); }, [this, normalized, revision](const QString& error) {
        if(m_stopping)return;
        m_signer_validation_in_flight = false;
        if (revision != m_signer_validation_revision) { queueSignerValidation(); return; }
        m_validated_signer_path = normalized;
        m_signer_validation_error = error;
        m_signer_validation_pending = false;
        Q_EMIT signerPathValidationChanged();
    }, [this, revision](std::exception_ptr) {
        if(m_stopping)return;
        m_signer_validation_in_flight = false;
        if (revision != m_signer_validation_revision) { queueSignerValidation(); return; }
        m_signer_validation_error = tr("Unable to validate the signer path.");
        m_signer_validation_pending = false;
        Q_EMIT signerPathValidationChanged();
    },{execution::Lane::Core,{},execution::WorkClass::Observation});
}

QString OptionsQmlModel::getDefaultDataDirString()
{
    return m_default_data_dir;
}


QUrl OptionsQmlModel::getDefaultDataDirectory()
{
    return m_default_data_directory;
}

QString OptionsQmlModel::getCustomDataDirString(){return m_custom_datadir_display;}

QString OptionsQmlModel::validateCustomDataDir(const QString& path) const
{
    return path==m_data_dir_validation_source||path==m_validated_data_dir?m_data_dir_validation_error:QString{};
}
bool OptionsQmlModel::selectCustomDataDir(const QString& path)
{
    if(!m_ready||m_stopping||m_pending||m_validation_pending)return false;
    if(path.size()>32768){setError(tr("The data directory path is too long."));return false;}
    const auto revision=++m_data_dir_revision;m_data_dir_validation_source=path;m_validation_pending=true;
    const QPointer<OptionsQmlModel> observer{this};
    const bool admitted=m_work->submit([path]{const auto normalized=QmlDataDir::NormalizeLocalPath(path);QString error;
        const bool saved=QmlDataDir::PersistGuiDataDirSelection(normalized,&error);
        if(!saved&&error.isEmpty())error=tr("Unable to save the selected data directory.");
        return std::tuple{normalized,QmlDataDir::DisplayPath(normalized),error};},
        [observer,revision](const auto& result){
            if(!observer||observer->m_stopping||observer->m_data_dir_revision!=revision)return;
            const auto& [path,display,error]=result;observer->m_validated_data_dir=path;observer->m_data_dir_validation_error=error;
            if(error.isEmpty()) {observer->m_custom_datadir_string=path;observer->m_custom_datadir_display=display;Q_EMIT observer->customDataDirStringChanged(path);
                if(!observer||observer->m_stopping)return;
                observer->adoptDataDir(path);}
            if(!observer||observer->m_stopping)return;
            observer->setError(error);if(!observer||observer->m_stopping)return;
            observer->m_validation_pending=false;Q_EMIT observer->validationPendingChanged();
            if(observer&&!observer->m_stopping)Q_EMIT observer->dataDirSelectionFinished(error.isEmpty(),error);
        },[observer,revision](std::exception_ptr){
            if(!observer||observer->m_stopping||observer->m_data_dir_revision!=revision)return;
            const auto error=tr("Unable to select the data directory.");observer->setError(error);if(!observer||observer->m_stopping)return;
            observer->m_validation_pending=false;Q_EMIT observer->validationPendingChanged();
            if(observer&&!observer->m_stopping)Q_EMIT observer->dataDirSelectionFinished(false,error);
        });
    if(observer&&!m_stopping){Q_EMIT validationPendingChanged();if(observer&&!m_stopping&&admitted)setError({});}
    return admitted;
}
void OptionsQmlModel::useDefaultDataDir()
{
    if(!m_ready||m_stopping||m_pending||m_validation_pending)return;
    const auto revision=++m_data_dir_revision;m_validation_pending=true;const QPointer<OptionsQmlModel> observer{this};
    m_work->submit([]{QString error;const bool saved=QmlDataDir::PersistDefaultDataDirSelection(&error);
        if(!saved&&error.isEmpty())error=tr("Unable to save the default data directory.");
        return error;},
        [observer,revision](const QString& error){
            if(!observer||observer->m_stopping||observer->m_data_dir_revision!=revision)return;
            if(error.isEmpty()){observer->m_custom_datadir_string.clear();observer->m_custom_datadir_display.clear();Q_EMIT observer->customDataDirStringChanged({});
                if(!observer||observer->m_stopping)return;
                observer->adoptDataDir(observer->m_default_data_dir);}
            if(!observer||observer->m_stopping)return;
            observer->setError(error);if(!observer||observer->m_stopping)return;
            observer->m_validation_pending=false;Q_EMIT observer->validationPendingChanged();
            if(observer&&!observer->m_stopping)Q_EMIT observer->dataDirSelectionFinished(error.isEmpty(),error);
        },[observer,revision](std::exception_ptr){
            if(!observer||observer->m_stopping||observer->m_data_dir_revision!=revision)return;
            const auto error=tr("Unable to select the data directory.");observer->setError(error);if(!observer||observer->m_stopping)return;
            observer->m_validation_pending=false;Q_EMIT observer->validationPendingChanged();
            if(observer&&!observer->m_stopping)Q_EMIT observer->dataDirSelectionFinished(false,error);
        });
    if(observer&&!m_stopping)Q_EMIT validationPendingChanged();
}
void OptionsQmlModel::setDataDir(QString path)
{
    if(m_stopping)return;
    const auto revision=++m_data_dir_revision;const QPointer<OptionsQmlModel> observer{this};
    m_work->submit([path]{return QmlDataDir::NormalizeLocalPath(path);},[observer,revision](const QString& normalized){
        if(observer&&!observer->m_stopping&&observer->m_data_dir_revision==revision)observer->adoptDataDir(normalized);
    },[observer](std::exception_ptr){if(observer&&!observer->m_stopping)observer->setError(tr("Unable to check the data directory."));});
}
void OptionsQmlModel::adoptDataDir(const QString& normalized)
{
    const auto effective=normalized.isEmpty()?m_default_data_dir:normalized;if(effective==m_dataDir)return;m_dataDir=effective;Q_EMIT dataDirChanged(m_dataDir);
}

QStringList OptionsQmlModel::buildAvailableLanguages()
{
    QStringList languages;
    languages << "";  // empty = system default

    QDir translations_dir(":/translations");
    QStringList files = translations_dir.entryList({"bitcoin_*.qm"}, QDir::Files);
    QStringList tags;
    for (const QString& file : files) {
        // Strip "bitcoin_" prefix and ".qm" suffix to get locale tag.
        QString tag = file;
        tag.remove(0, 8);       // remove "bitcoin_"
        tag.chop(3);            // remove ".qm"
        // Skip QML-app-specific translation resources (e.g. "qml_es").
        if (tag.startsWith(QStringLiteral("qml_"))) continue;
        tags << tag;
    }
    tags.sort(Qt::CaseInsensitive);
    languages << tags;
    return languages;
}

void OptionsQmlModel::persistGuiSetting(const QString& key, const QVariant& value)
{
    if (!m_ready || m_stopping || m_validation_pending) return;
    runCommand([key, value](interfaces::Node&, ArgsManager&) { return SaveGuiSetting(key, value); });
}

void OptionsQmlModel::setLanguage(const QString& new_language)
{
    if (!canEditCoreSetting(QStringLiteral("lang")) || new_language == m_language) return;
    if (new_language.size() > 64) { setError(tr("The selected language is not available.")); return; }
    runCommand([new_language](interfaces::Node&, ArgsManager& args) {
        if (!buildAvailableLanguages().contains(new_language)) return tr("The selected language is not available.");
        QString error;
        if (QmlCoreSettings::PersistSettings(args, {QStringLiteral("lang")}, [&] {
                return QmlCoreSettings::WriteCoreSettingOverride(args, QStringLiteral("lang"), common::SettingsValue{new_language.toStdString()});
            }, &error)) error = SaveGuiSetting(SettingsKeys::LANGUAGE, new_language);
        return error;
    });
}

QString OptionsQmlModel::languageSummary() const
{
    return languageLabel(m_language);
}

QString OptionsQmlModel::languageLabel(const QString& locale_tag) const
{
    if (locale_tag.isEmpty()) {
        return QObject::tr("System default");
    }
    if (!m_available_languages.contains(locale_tag)) return tr("Unavailable translation (%1)").arg(locale_tag.left(64));
    QLocale locale(locale_tag);
    QString native = locale.nativeLanguageName();
    if (native.isEmpty()) {
        return locale_tag;
    }
    // Capitalize first letter of native name.
    native[0] = native[0].toUpper();
    QString english = QLocale::languageToString(locale.language());
    // Append territory disambiguation when the tag includes a territory code.
    if (locale_tag.contains('_')) {
        QString native_territory = locale.nativeTerritoryName();
        if (!native_territory.isEmpty()) {
            native += QStringLiteral(" (%1)").arg(native_territory);
        }
        english += QStringLiteral(" (%1)").arg(QLocale::territoryToString(locale.territory()));
    }
    return QStringLiteral("%1 \u2014 %2").arg(native, english);
}

void OptionsQmlModel::setDisplayUnit(int new_display_unit)
{
    new_display_unit = NormalizeDisplayUnit(new_display_unit);
    if (!m_ready || m_stopping || m_validation_pending || new_display_unit == m_display_unit) return;
    m_display_unit = new_display_unit;
    const QPointer<OptionsQmlModel> observer{this};
    persistGuiSetting(SettingsKeys::DISPLAY_UNIT, new_display_unit);
    if(observer&&!m_stopping)Q_EMIT displayUnitChanged(m_display_unit);
}

void OptionsQmlModel::setThirdPartyTransactionUrls(const QString& urls)
{
    if (!m_ready || m_stopping || m_validation_pending || urls == m_third_party_transaction_urls) return;
    m_third_party_transaction_urls = urls;
    const QPointer<OptionsQmlModel> observer{this};
    persistGuiSetting(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS, urls);
    if(observer&&!m_stopping)Q_EMIT thirdPartyTransactionUrlsChanged();
}

void OptionsQmlModel::setMoneyFontChoice(const QString& choice)
{
    const QString normalized = choice == MONEY_FONT_BEST_SYSTEM ? QString{MONEY_FONT_BEST_SYSTEM} : QString{MONEY_FONT_EMBEDDED};
    if (!m_ready || m_stopping || m_validation_pending || normalized == m_money_font_choice) return;
    m_money_font_choice = normalized;
    const QPointer<OptionsQmlModel> observer{this};
    persistGuiSetting(SettingsKeys::MONEY_FONT_CHOICE, normalized);
    if(!observer||m_stopping)return;
    Q_EMIT moneyFontChoiceChanged();
    if(observer&&!m_stopping)Q_EMIT moneyFontChanged();
}

void OptionsQmlModel::setHideAmounts(bool hide)
{
    if (!m_ready || m_stopping || m_validation_pending || hide == m_hide_amounts) return;
    m_hide_amounts = hide;
    Q_EMIT hideAmountsChanged();
    persistGuiSetting(SettingsKeys::HIDE_AMOUNTS, hide);
}

QFont OptionsQmlModel::moneyFont() const
{
    if (m_money_font_choice == MONEY_FONT_BEST_SYSTEM) {
        return QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    QFont font{QStringLiteral("Roboto Mono")};
    font.setStyleName(QStringLiteral("Regular"));
    return font;
}

QString OptionsQmlModel::displayUnitLabel() const
{
    return QmlBitcoinUnits::label(QmlBitcoinUnits::fromDisplayUnit(m_display_unit));
}

QString OptionsQmlModel::displayUnitLabelForAmount(qint64 satoshi) const
{
    return QmlBitcoinUnits::displayLabel(QmlBitcoinUnits::fromDisplayUnit(m_display_unit), satoshi);
}
