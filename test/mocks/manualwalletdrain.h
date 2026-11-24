// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_MANUALWALLETDRAIN_H
#define BITCOIN_QML_TEST_MOCKS_MANUALWALLETDRAIN_H

#include <qml/models/walletmodel.h>
#include <test/mocks/manualexecutionenvironment.h>

#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QtTest/QtTest>

#include <memory>
#include <vector>

// Like the registry, keep the completion receiver alive even if the wallet
// model is destroyed while its accepted work is finishing.
inline std::shared_ptr<const bool> WatchWalletNativeDrain(WalletModel& wallet)
{
    auto drained = std::make_shared<bool>(false);
    wallet.whenNativeDrained(*QCoreApplication::instance(), [drained] { *drained = true; });
    return drained;
}

/** A manual transport must outlive every original model and deferred alias.
 * Install immediately after construction so assertion failures also retire the
 * original graph. This owns no domain behavior and never fabricates a drain. */
template <typename Execution>
class ManualWalletDrain
{
public:
    ManualWalletDrain(Execution& execution, WalletModel* model)
        : m_execution(execution)
    {
        add(model);
    }
    void add(WalletModel* model)
    {
        m_models.emplace_back(model);
        m_native_drains.push_back(WatchWalletNativeDrain(*model));
    }
    bool nativeDrained() const
    {
        for (const auto& drained : m_native_drains) {
            if (!*drained) return false;
        }
        return true;
    }
    bool finish(bool detach_observers = false)
    {
        // Presentation callbacks can refer to test-local variables declared
        // after this guard. Their phase assertions have finished; disconnect
        // those outgoing callbacks before physical teardown starts.
        for (const auto& model : m_models) {
            if (!model) continue;
            if (detach_observers) QObject::disconnect(model.data(), nullptr, nullptr, nullptr);
            model->beginShutdown();
        }
        return QTest::qWaitFor([&] {
            m_execution.drain();
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            m_execution.drain();
            for (const auto& model : m_models) {
                if (model && !model->shutdownComplete()) return false;
            }
            return nativeDrained() && m_execution.quiescent();
        }, 3000);
    }
    ~ManualWalletDrain()
    {
        if (!finish(true)) QTest::qFail("Original manual wallet graph did not physically drain", __FILE__, __LINE__);
    }
private:
    Execution& m_execution;
    std::vector<QPointer<WalletModel>> m_models;
    std::vector<std::shared_ptr<const bool>> m_native_drains;
};

#endif // BITCOIN_QML_TEST_MOCKS_MANUALWALLETDRAIN_H
