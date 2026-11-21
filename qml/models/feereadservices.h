// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_FEEREADSERVICES_H
#define BITCOIN_QML_MODELS_FEEREADSERVICES_H

#include <qml/execution.h>

#include <functional>
#include <memory>

/** Entry-transport debounce. It only enqueues a captured read, never calls Core. */
class FeeReadTimer
{
public:
    virtual ~FeeReadTimer() = default;
    virtual void restart(std::function<void()> expired) = 0;
    virtual void stop() = 0;
};

// Result and input are defined next to the existing SendFeePreview value.
struct FeeReadInput;
struct FeeReadResult;
struct FeeReadServices {
    std::shared_ptr<execution::Scheduler> scheduler;
    std::shared_ptr<FeeReadTimer> timer;
    std::function<FeeReadResult(const FeeReadInput&)> read;
    // Presentation transport: enqueue without invoking inline.
    std::function<void(std::function<void()>)> post{};
};

#endif // BITCOIN_QML_MODELS_FEEREADSERVICES_H
