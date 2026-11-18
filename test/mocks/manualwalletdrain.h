// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_MANUALWALLETDRAIN_H
#define BITCOIN_QML_TEST_MOCKS_MANUALWALLETDRAIN_H

#include <qml/backendexecutor.h>
#include <qml/models/walletqmlmodel.h>
#include <test/mocks/manualexecutionenvironment.h>

#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QtTest/QtTest>

#include <memory>
#include <vector>

/** A manual transport must outlive every original model and deferred alias.
 * Install immediately after construction so assertion failures also retire the
 * original graph. This owns no domain behavior and never fabricates a drain. */
class ManualWalletDrain
{
public:
    ManualWalletDrain(ManualRoutedExecution& execution, WalletQmlModel* model)
        : m_execution(execution)
    {
        add(model);
    }
    void add(WalletQmlModel* model)
    {
        m_models.emplace_back(model);
        m_backends.push_back(model->backendExecutor());
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
            for (const auto& backend : m_backends) {
                if (!backend->isDrained()) return false;
            }
            return m_execution.quiescent();
        }, 3000);
    }
    ~ManualWalletDrain()
    {
        if (!finish(true)) QTest::qFail("Original manual wallet graph did not physically drain", __FILE__, __LINE__);
    }
private:
    ManualRoutedExecution& m_execution;
    std::vector<QPointer<WalletQmlModel>> m_models;
    std::vector<std::shared_ptr<BackendExecutor>> m_backends;
};

#endif // BITCOIN_QML_TEST_MOCKS_MANUALWALLETDRAIN_H
