// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_OWNEDWORK_H
#define BITCOIN_QML_OWNEDWORK_H
#include <qml/snapshotquery.h>
#include <qml/retainedsnapshot.h>
#include <qml/guipresentation.h>
#include <QObject>
#include <QPointer>
#include <map>
#include <atomic>
#include <limits>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
namespace execution {
/** Original application owner for independently completed
 * effects. Domain records outcomes before GUI notification; quiet close drops
 * GUI packets, while accepted effects and their Native cleanup keep running.
 * The ledger drain means no accepted effect can still use its borrowed interface.
 * Retained payload retirement belongs to the common scheduler drain; composition
 * keeps the original public-interface owner alive through that final drain.
 */
class OwnedWork final : public QObject
{
    struct Ledger {
        std::mutex mutex;
        size_t pending{0}; bool closed{false}, notified{false};
        std::function<void()> drained;
        bool begin() { std::lock_guard lock{mutex}; if(closed)return false; ++pending; return true; }
        void finish() { {std::lock_guard lock{mutex}; --pending;} notify(); }
        void close(std::function<void()> callback) { {std::lock_guard lock{mutex};closed=true;if(callback)drained=std::move(callback);} notify(); }
        void notify() {
            std::function<void()> callback;
            {std::lock_guard lock{mutex}; if(!closed||pending||notified||!drained)return;notified=true;callback=std::exchange(drained,{});}
            try {callback();} catch (...) {
                std::lock_guard lock{mutex};notified=false;drained=std::move(callback);
            }
        }
    };
    struct Operation { virtual ~Operation()=default; virtual void publish()=0; };
    template<typename Value,typename Done,typename Failed> struct Result final : Operation {
        struct Packet { std::optional<Value> value; std::exception_ptr error; };
        std::shared_ptr<Packet> packet; Done done; Failed failed;
        Result(std::shared_ptr<Packet> value,Done completion,Failed error) : packet(std::move(value)),done(std::move(completion)),failed(std::move(error)) {}
        void publish() override { if(packet->error)failed(packet->error);else done(std::as_const(*packet->value)); }
    };
public:
    explicit OwnedWork(SnapshotServices services) : m_services(std::move(services)),m_ledger(std::make_shared<Ledger>()) {m_services.post=presentation::ReliablePost(std::move(m_services.post));}
    ~OwnedWork() override { close(); }
    template<typename Work,typename Done,typename Failed>
    bool submit(Work work,Done done,Failed failed,TaskSpec spec={}) {
        using Value=std::invoke_result_t<Work>;
        using Pending=Result<Value,Done,Failed>;
        auto packet=RetainSnapshot<typename Pending::Packet>(m_services.scheduler,[] {return std::make_unique<typename Pending::Packet>();});
        if(!packet||!m_ledger->begin()) { failed(std::make_exception_ptr(NotStarted{NotStarted::Cause::Transport}));return false; }
        const auto id=++m_next;
        m_operations.emplace(id,std::make_shared<Pending>(packet,std::move(done),std::move(failed)));
        const QPointer<OwnedWork> observer{this}; const auto ledger=m_ledger;
        const auto complete=[ledger,observer,id,post=m_services.post] {
            ledger->finish();
            try {post([observer,id] {if(observer)observer->publish(id);});} catch (...) {
                // The retained operation still owns the actual outcome. Quiet
                // close releases it independently of presentation availability.
            }
        };
        const auto admission=Submit(*m_services.scheduler,[packet,work=std::move(work)]() mutable {packet->value=work();return true;},
            [complete](bool){complete();},[packet,complete](std::exception_ptr error){packet->error=error;complete();},spec);
        if(!admission) {packet->error=std::make_exception_ptr(NotStarted{NotStarted::Cause::Transport});complete();}
        return bool(admission);
    }
    template<typename Value,typename Starter,typename Done,typename Failed>
    bool submitAsync(Starter starter,Done done,Failed failed,std::shared_ptr<Scheduler> action={}) {
        using Pending=Result<Value,Done,Failed>;
        if(!action)action=m_services.scheduler;
        auto packet=RetainSnapshot<typename Pending::Packet>(action,[]{return std::make_unique<typename Pending::Packet>();});
        if(!packet||!m_ledger->begin()){failed(std::make_exception_ptr(NotStarted{NotStarted::Cause::Transport}));return false;}
        const auto id=++m_next;const QPointer<OwnedWork> observer{this};const auto ledger=m_ledger;
        m_operations.emplace(id,std::make_shared<Pending>(packet,std::move(done),std::move(failed)));
        const auto completed=std::make_shared<std::atomic<bool>>(false);
        const auto complete=[packet,completed,ledger,observer,id,post=m_services.post](std::optional<Value> value,std::exception_ptr error) {
            if(completed->exchange(true))return;
            packet->value=std::move(value);packet->error=error;ledger->finish();
            try{post([observer,id]{if(observer)observer->publish(id);});}catch(...){}
        };
        try{
            const bool admitted=bool(starter([complete](Value value){complete(std::move(value),{});},[complete](std::exception_ptr error){complete({},error);}));
            if(!admitted)complete({},std::make_exception_ptr(NotStarted{NotStarted::Cause::Transport}));
            return admitted;
        }catch(...){complete({},std::current_exception());return false;}
    }
    void close(std::function<void()> drained={}) {
        m_operations.clear();
        const auto post=m_services.post;
        m_ledger->close(drained?[post,drained=std::move(drained)] {post(drained);}:std::function<void()>{});
    }
private:
    void publish(uint64_t id) {
        const auto found=m_operations.find(id);if(found==m_operations.end())return;
        const auto operation=std::move(found->second);m_operations.erase(found);operation->publish();
    }
    SnapshotServices m_services;
    std::shared_ptr<Ledger> m_ledger;
    std::map<uint64_t,std::shared_ptr<Operation>> m_operations;
    uint64_t m_next{0};
};
} // namespace execution
#endif // BITCOIN_QML_OWNEDWORK_H
