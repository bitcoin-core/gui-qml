// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/banlistmodel.h>
#include <qml/retainedsnapshot.h>
#include <interfaces/node.h>
#include <QDateTime>
#include <QLocale>
#include <QPointer>
#include <stdexcept>
namespace { struct BanRows { std::vector<NativeListRow> rows; }; }
BanListModel::BanListModel(interfaces::Node& node, QObject* parent, bool backend_ready, execution::SnapshotServices services)
    : NativeListModel(std::move(services),parent), m_node(node), m_backend_ready(backend_ready)
{
    if (!scheduler()) configureServices({});
    setRead([node=&node,scheduler=scheduler()] {
        banmap_t banned; if (!node->getBanned(banned)) throw std::runtime_error("Ban list unavailable");
        const QLocale locale;
        auto group=execution::RetainSnapshot<BanRows>(scheduler,[&] {
            auto result=std::make_unique<BanRows>(); result->rows.reserve(banned.size());
            for (const auto& [subnet,entry]:banned) {
                NativeListRow row; row.key=QString::fromStdString(subnet.ToString()); row.payload=std::make_shared<BanListEntry>(BanListEntry{subnet,entry});
                row.values={{int(BanRoles::AddressRole),row.key},{int(BanRoles::BanUntilRole),locale.toString(QDateTime::fromSecsSinceEpoch(entry.nBanUntil),QLocale::ShortFormat)}};
                result->rows.push_back(std::move(row));
            } return result;
        });
        if (!group) throw std::runtime_error("Ban inventory capacity unavailable");
        std::vector<NativeRowHandle> rows; rows.reserve(group->rows.size()); for(const auto& row:group->rows)rows.emplace_back(group,&row); return rows;
    });
    connect(this,&QAbstractItemModel::rowsInserted,this,&BanListModel::countChanged);
    connect(this,&QAbstractItemModel::rowsRemoved,this,&BanListModel::countChanged);
}
BanListModel::~BanListModel() { closeAdmission(); }
void BanListModel::closeAdmission() { m_unban_command.reset(); NativeListModel::closeAdmission(); }
void BanListModel::beginShutdown() { closeAdmission(); }
void BanListModel::backendInitialized() { if(stopping()||m_backend_ready)return; m_backend_ready=true; refresh(); }
void BanListModel::refresh() { if(m_backend_ready)refreshNative(); }
QHash<int,QByteArray> BanListModel::roleNames() const { return {{int(BanRoles::AddressRole),"address"},{int(BanRoles::BanUntilRole),"banUntil"}}; }
bool BanListModel::unbanAt(int position) {
    if(stopping()||!m_backend_ready||m_unban_command)return false;
    const auto row=rowHandle(index(position)); if(!row)return false;
    // Own the original immutable row rather than copying/destructing native payload on GUI.
    const QPointer<BanListModel> guard{this};
    auto command=std::make_shared<execution::OwnedCommand<bool>>(scheduler(),[guard,post=services().post] { post([guard] { if(guard)guard->publishCommand(); }); });
    m_unban_command=command;
    if(!command->start([node=&m_node,row] { return node->unban(static_cast<const BanListEntry*>(row->payload.get())->subnet); })) { m_unban_command.reset();return false; }
    return true;
}
void BanListModel::publishCommand() {
    if(stopping()||!m_unban_command)return;
    const auto result=m_unban_command->result();if(!result)return;
    m_unban_command.reset(); if(stopping())return;
    const QPointer<BanListModel> guard{this}; refresh(); if(guard&&!stopping())Q_EMIT unbanFinished(result->value.value_or(false)&&!result->error);
}
