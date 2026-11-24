// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_RUNTIMEPROMPTWORKER_H
#define BITCOIN_QML_RUNTIMEPROMPTWORKER_H
#include <qml/presentationservices.h>
#include <qml/runtimeprompt.h>
#include <functional>

namespace runtimeprompt {
struct Projection {
    State state{State::Idle};
    std::shared_ptr<const Prompt> active;
    size_t pending{0};
    Disposition last{Disposition::Accepted};
    uint64_t rejected{0};
    Disposition rejection{Disposition::Accepted};
};
/** A continuation belongs to the original Native producer. Its acknowledgment
 * accounts for return from that producer, independently of popup retirement. */
class Continuation final {
public:
    Continuation(std::shared_ptr<const Request> request, std::function<void()> delivered);
    ~Continuation();
    Continuation(const Continuation&)=delete;
    Continuation& operator=(const Continuation&)=delete;
    // Reading the answer does not finish the original Native call. Its owner
    // retains this continuation through post-answer work and then releases it.
    Answer wait();
private:
    friend class Service;
    void rejected();
    struct Completion {
        std::function<void()> delivered;
        void release(){if(const auto done=std::exchange(delivered,{}))done();}
        ~Completion(){release();}
    };
    Completion m_completion; // Last retired: the original request drops first.
    std::shared_ptr<const Request> m_request;

};
class Service final {
public:
    struct Queues {
        std::shared_ptr<execution::SerialQueue> utility;
        PresentationServices::RetireValue retire;
    };
    Service(Queues queues, std::function<void()> changed,
            std::function<void()> drained = {}, Limits limits = {});
    ~Service();
    // Transfer one fresh original producer request to this owner. It must not
    // be submitted to multiple Services. Only a Native producer may call wait.
    // Nonblocking messages remain owned until an explicit answer/dismissal.
    bool enqueue(std::shared_ptr<const Request> request, bool blocking,
                 std::shared_ptr<Continuation>& continuation);
    bool visible(Id id);
    bool answer(Id id, uint32_t button);
    bool dismiss(Id id);
    bool unavailable(Id id);
    Projection projection() const;
    void close();
private:
    struct Publication;
    struct Impl;
    bool command(Id id, int kind, uint32_t button = 0);
    std::shared_ptr<Publication> m_publication;
    std::shared_ptr<Impl> m_owner;
    std::weak_ptr<Impl> m_state;
};
} // namespace runtimeprompt
#endif // BITCOIN_QML_RUNTIMEPROMPTWORKER_H
