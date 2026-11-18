// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/runtimepromptworker.h>
#include <qml/retainedsnapshot.h>
#include <qml/atomicsharedptr.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <utility>
#include <QtGlobal>
#include <limits>
#include <stdexcept>

namespace runtimeprompt {
Continuation::Continuation(std::shared_ptr<const Request> request, std::function<void()> delivered)
    :m_completion{std::move(delivered)},m_request(std::move(request)){}
Continuation::~Continuation(){if(m_request)m_request->answer->cancel();}
Answer Continuation::wait(){return m_request->answer->wait();}
void Continuation::rejected()
{
    m_request->answer->cancel();m_request.reset();m_completion.release();
}
struct Service::Publication {
    std::mutex mutex;
    Projection value;
    size_t owned{0};bool closing{false};
    uint64_t rejected{0};Disposition rejection{Disposition::Accepted};
    void refused(Disposition reason){std::lock_guard lock{mutex};if(rejected!=std::numeric_limits<uint64_t>::max())++rejected;rejection=reason;}
};
struct Service::Owner {
    struct Completion {std::function<void()> done;~Completion(){try{if(done)done();}catch(...){}}};
    Completion completion; // Last destroyed, after every request/port/scope.
    struct Slot { std::shared_ptr<const Request> request; bool blocking{false}; Id id{0}; std::shared_ptr<execution::ResourceLease> completion; };
    const execution::ResourceKey key;
    const std::shared_ptr<Publication> publication;
    const std::function<void()> changed;
    ownership::AtomicSharedPtr<execution::Scheduler> scope;
    Queue queue;
    // Fixed capacity bounds Native ingress before Domain has consumed it.
    std::mutex mutex;
    std::array<Slot,32> slots;
    std::atomic_bool stopping{false};
    bool retired{false}; // Domain only
    Owner(execution::SnapshotServices execution, std::shared_ptr<Publication> output,
          std::function<void()> notify, std::function<void()> done, Limits limits)
        : completion{std::move(done)}, key(execution.scheduler->allocateKey()), publication(std::move(output)), changed(std::move(notify)),
          scope(execution.scheduler->openActionScope()), queue(limits) {}
    void publish(Disposition last = Disposition::Accepted)
    {
        { std::lock_guard lock{publication->mutex}; publication->value={queue.state(),queue.active(),queue.size(),last}; }
        try { if (changed) changed(); } catch (...) {} // The request/answer remains owned.
    }
    bool post(std::function<void()> turn)
    {
        const auto original=scope.load();
        return original && bool(execution::Submit(*original,std::move(turn),[] {},{},
            {execution::Lane::Domain,key,execution::WorkClass::Action}));
    }
    void release(size_t slot,const std::shared_ptr<const Request>& request)
    {
        { std::lock_guard lock{mutex}; if(slots.at(slot).request!=request)return; slots.at(slot)={};
          std::lock_guard output_lock{publication->mutex};--publication->owned; }
        if (stopping) retireIfEmpty();
    }
    void retireIfEmpty()
    {
        bool empty;
        { std::lock_guard lock{mutex}; empty=std::all_of(slots.begin(),slots.end(),[](const auto& slot){return !slot.request;}); }
        if (empty && !retired) { queue.close();retired=true;scope.store({}); }
    }
    void acknowledged(size_t slot,const std::shared_ptr<const Request>& request)
    {
        Id id;
        { std::lock_guard lock{mutex}; if(slots.at(slot).request!=request)return; id=slots.at(slot).id; }
        if(stopping)queue.close();
        if(id)queue.delivered(id);
        release(slot,request);publish();
    }
    void intake(size_t slot)
    {
        std::shared_ptr<const Request> request;
        { std::lock_guard lock{mutex}; request=slots.at(slot).request; }
        if(!request)return;
        const auto admission=queue.enqueue(request);
        { std::lock_guard lock{mutex}; slots.at(slot).id=admission.id; }
        if(!admission){
            publication->refused(admission.disposition);
            finishNonblocking(slot,request);
        }
        publish(admission.disposition);
    }
    void command(Id id,int kind,uint32_t button)
    {
        Disposition result{Disposition::InvalidRequest};
        switch(kind){
        case 0:result=queue.visible(id);break;
        case 1:result=queue.answer(id,button);break;
        case 2:result=queue.dismiss(id);break;
        case 3:result=queue.unavailable(id);if(result==Disposition::NotReady)result=queue.dismiss(id);break;
        }
        // Only blocking Native continuations need a separate return receipt.
        if(kind && (result==Disposition::Accepted||result==Disposition::AlreadyCompleted)){
            std::optional<size_t> slot;
            {std::lock_guard lock{mutex};for(size_t i=0;i<slots.size();++i)if(slots[i].id==id&&!slots[i].blocking)slot=i;}
            if(slot){std::shared_ptr<const Request> request;{std::lock_guard lock{mutex};request=slots[*slot].request;}finishNonblocking(*slot,request);}
        }
        publish(result);
    }
    void finishNonblocking(size_t index,const std::shared_ptr<const Request>& request)
    {
        std::shared_ptr<execution::ResourceLease> completed;
        {std::lock_guard lock{mutex};if(slots[index].request==request&&!slots[index].blocking)completed=std::exchange(slots[index].completion,{});}
        // Releasing the pre-admitted lease activates its reserved Native turn
        // and mandatory Domain outcome. No fresh admission is required.
    }

};
Service::Service(execution::SnapshotServices execution,std::function<void()> changed,std::function<void()> drained,Limits limits)
    :m_publication(std::make_shared<Publication>())
{
    if(!execution.scheduler)throw std::invalid_argument{"Missing runtime prompt scheduler"};
    m_owner=execution::RetainSnapshot<Owner>(execution.scheduler,[&]{return std::make_unique<Owner>(execution,m_publication,std::move(changed),std::move(drained),limits);});
    if(m_owner&&m_owner->scope.load()){m_state=m_owner;}else{m_owner.reset();m_publication->value.state=State::Stopped;m_publication->value.last=Disposition::NotReady;}
}
Service::~Service(){close();}
bool Service::enqueue(std::shared_ptr<const Request> request,bool blocking,std::shared_ptr<Continuation>& continuation)
{
    const auto state=m_state.lock();
    if(request&&request->answer->result())return false;
    if(!state||state->stopping||!request){if(request)request->answer->cancel();return false;}
    std::optional<size_t> slot;
    {std::lock_guard lock{state->mutex};
     if(std::any_of(state->slots.begin(),state->slots.end(),[&](const auto& entry){return entry.request==request;}))return false;
     if(!state->stopping)for(size_t i=0;i<state->slots.size();++i)if(!state->slots[i].request){slot=i;state->slots[i]={request,blocking,0,{}};{std::lock_guard output_lock{m_publication->mutex};++m_publication->owned;}break;}}
    if(!slot){request->answer->cancel();m_publication->refused(Disposition::Overflow);try{if(state->changed)state->changed();}catch(...){}return false;}
    bool completion_admitted{false};
    std::shared_ptr<execution::ResourceLease> lease;
    try{
        const auto original=state->scope.load();
        if(original){
            const auto key=original->allocateKey();
            const auto reserved=execution::Submit(*original,[]{},[state,index=*slot,request]{state->acknowledged(index,request);},{},
                {execution::Lane::Cleanup,key,execution::WorkClass::Action},true);
            if(reserved){
                completion_admitted=true;
                auto held=original->borrow(key);original->activateCleanup(reserved.id);
                if(held)lease=std::make_shared<execution::ResourceLease>(std::move(*held.lease));
            }
        }
        if(lease){
            if(blocking)continuation=std::make_shared<Continuation>(request,[held=std::move(lease)]()mutable{held.reset();});
            else {std::lock_guard lock{state->mutex};state->slots[*slot].completion=std::move(lease);}
            if(state->post([state,index=*slot]{state->intake(index);}))return true;
        }
    }catch(...){}
    m_publication->refused(Disposition::NotReady);
    request->answer->cancel();if(continuation)continuation->rejected();continuation.reset();
    state->finishNonblocking(*slot,request);lease.reset();
    // A reserved completion owns its own eventual rollback acknowledgment.
    // Without one there is no original callback or queue entry to retain.
    if(!completion_admitted){std::lock_guard lock{state->mutex};if(state->slots[*slot].request==request){state->slots[*slot]={};std::lock_guard output_lock{m_publication->mutex};--m_publication->owned;}}
    return false;
}
bool Service::command(Id id,int kind,uint32_t button)
{
    const auto state=m_state.lock();if(!state||state->stopping)return false;
    if(state->post([state,id,kind,button]{state->command(id,kind,button);}))return true;
    std::shared_ptr<const Request> request;std::optional<size_t> slot;
    {std::lock_guard lock{state->mutex};for(size_t i=0;i<state->slots.size();++i)if(state->slots[i].id==id){slot=i;request=state->slots[i].request;break;}}
    if(request){request->answer->cancel();state->finishNonblocking(*slot,request);}
    m_publication->refused(Disposition::NotReady);
    try{if(state->changed)state->changed();}catch(...){}
    return false;
}
bool Service::visible(Id id){return command(id,0);}
bool Service::answer(Id id,uint32_t button){return command(id,1,button);}
bool Service::dismiss(Id id){return command(id,2);}
bool Service::unavailable(Id id){return command(id,3);}
Projection Service::projection()const{std::lock_guard lock{m_publication->mutex};auto value=m_publication->value;value.rejected=m_publication->rejected;value.rejection=m_publication->rejection;value.pending=m_publication->owned;if(m_publication->closing){value.state=value.pending?State::Stopping:State::Stopped;value.active.reset();}return value;}
void Service::close()
{
    {std::lock_guard lock{m_publication->mutex};m_publication->closing=true;}
    const auto state=m_state.lock();
    if(state&&!state->stopping.exchange(true)){
        std::array<std::shared_ptr<execution::ResourceLease>,32> completed;
        bool empty{true};
        {std::lock_guard lock{state->mutex};for(size_t i=0;i<state->slots.size();++i)if(state->slots[i].request){
            empty=false;state->slots[i].request->answer->cancel();
            if(!state->slots[i].blocking)completed[i]=std::exchange(state->slots[i].completion,{});
        }}
        // Waiting Native producers keep their own leases until the original
        // answer is returned. Other prompts retire through these pre-admitted
        // acknowledgments even with ordinary admission full/closed.
        completed={};
        if(empty){
            const bool posted=state->post([state]{state->queue.close();state->retireIfEmpty();state->publish();});
            // No accepted request is stranded if this optional empty-owner
            // publication cannot be queued; its Native cleanup still retires.
            if(!posted)state->scope.store({});
        }
    }
    m_owner.reset();
}

} // namespace runtimeprompt
