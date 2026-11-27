// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/runtimepromptworker.h>
#include <qml/executor.h>
#include <qml/retainedsnapshot.h>
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
struct Service::Impl {
    struct Completion {std::function<void()> done;~Completion(){try{if(done)done();}catch(...){}}};
    Completion completion; // Last destroyed, after every request and port.
    struct Slot { std::shared_ptr<const Request> request; bool blocking{false}; Id id{0}; std::function<void()> completion; };
    const std::shared_ptr<execution::SerialQueue> utility;
    const std::shared_ptr<Publication> publication;
    const std::function<void()> changed;
    Queue queue;
    // Fixed capacity bounds Native ingress before Domain has consumed it.
    std::mutex mutex;
    std::array<Slot,32> slots;
    std::atomic_bool stopping{false};
    std::recursive_mutex reducer_mutex;
    bool retired{false};
    Impl(std::shared_ptr<Publication> output,
          std::function<void()> notify, std::function<void()> done, Limits limits, std::shared_ptr<execution::SerialQueue> utility)
        : completion{std::move(done)}, utility(std::move(utility)), publication(std::move(output)),
          changed(std::move(notify)), queue(limits) {}
    void publish(Disposition last = Disposition::Accepted)
    {
        { std::lock_guard lock{publication->mutex}; publication->value={queue.state(),queue.active(),queue.size(),last}; }
        try { if (changed) changed(); } catch (...) {} // The request/answer remains owned.
    }
    bool post(std::function<void()> turn)
    {
        return utility->post(std::move(turn));
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
        if (empty && !retired) { queue.close(); retired=true; }
    }
    void acknowledged(size_t slot,const std::shared_ptr<const Request>& request)
    {
        std::lock_guard reduce{reducer_mutex};
        Id id;
        { std::lock_guard lock{mutex}; if(slots.at(slot).request!=request)return; id=slots.at(slot).id; }
        if(stopping)queue.close();
        if(id)queue.delivered(id);
        release(slot,request);publish();
    }
    void intake(size_t slot,const std::shared_ptr<const Request>& request)
    {
        std::lock_guard reduce{reducer_mutex};
        { std::lock_guard lock{mutex}; if(slots.at(slot).request!=request)return; }
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
        std::lock_guard reduce{reducer_mutex};
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
        std::function<void()> completed;
        { std::lock_guard lock{mutex};if(slots[index].request==request&&!slots[index].blocking)completed=std::exchange(slots[index].completion,{});}
        if (completed) completed();
    }
    void closeQueue()
    {
        std::lock_guard reduce{reducer_mutex};
        queue.close();
        retireIfEmpty();
        publish();
    }
};
Service::Service(Queues queues, std::function<void()> changed,
                 std::function<void()> drained, Limits limits)
    :m_publication(std::make_shared<Publication>())
{
    if (!queues.utility || !queues.retire)
        throw std::invalid_argument{"Missing runtime prompt execution"};
    m_owner = execution::RetainSnapshot<Impl>(queues.retire, [&] {
        return std::make_unique<Impl>(m_publication, std::move(changed), std::move(drained), limits, std::move(queues.utility));
    });
    m_state = m_owner;
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
    std::function<void()> complete;
    try {
        const auto acknowledge = [state, index = *slot, request] { state->acknowledged(index, request); };
        complete = [state, acknowledge] {
            // An accepted producer owns its return even if Utility refuses
            // the wake. Serialize the original receipt in that case.
            try { if (state->post(acknowledge)) return; } catch (...) {}
            acknowledge();
        };
        completion_admitted = true;
        if (blocking) {
            continuation = std::make_shared<Continuation>(request, complete);
            complete = {};
        } else {
            std::lock_guard lock{state->mutex};
            if (!state->stopping) state->slots[*slot].completion = std::exchange(complete, {});
        }
        if (!state->stopping && state->post([state, index = *slot, request] { state->intake(index, request); })) return true;
    } catch (...) {}
    m_publication->refused(Disposition::NotReady);
    request->answer->cancel();if(continuation)continuation->rejected();continuation.reset();
    state->finishNonblocking(*slot,request);
    if (complete) complete();
    // An installed completion owns the rollback acknowledgment.
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
        std::array<std::function<void()>,32> completed;
        bool empty{true};
        {std::lock_guard lock{state->mutex};for(size_t i=0;i<state->slots.size();++i)if(state->slots[i].request){
            empty=false;state->slots[i].request->answer->cancel();
            if(!state->slots[i].blocking)completed[i]=std::exchange(state->slots[i].completion,{});
        }}
        // Blocking producers still own their return receipts after cancellation.
        for (auto& complete : completed) if (complete) complete();
        if (empty) state->post([state] { state->closeQueue(); });
    }
    m_owner.reset();
}

} // namespace runtimeprompt
