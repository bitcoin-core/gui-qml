// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <util/translation.h>

#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

const TranslateFn G_TRANSLATION_FUN{nullptr};

// Run once per process: Core startup and the QApplication have process lifetime.
class StartupErrorApplication : public BitcoinQmlApplication
{
public:
    bool observed_error{false};

protected:
    void qmlEngineCreated(QQmlApplicationEngine& engine) override
    {
        QTimer::singleShot(0, &engine, [this, &engine] {
            const auto message = engine.rootContext()->contextProperty("message").toString();
            if (message.isEmpty()) return;
            auto* window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            observed_error = window && window->isVisible();
            std::fprintf(stderr, "STARTUP_ERROR: %s\n", qPrintable(message));
            Q_EMIT engine.quit();
        });
    }

    void onboardingCreated(QQmlApplicationEngine& engine) override
    {
        auto* model = qobject_cast<OnboardingOptionsModel*>(engine.rootContext()->contextProperty("optionsModel").value<QObject*>());
        auto* timer = new QTimer(&engine);
        QObject::connect(timer, &QTimer::timeout, &engine, [model, timer, &engine] {
            if (model->validationPending()) return;
            timer->stop();
            model->setPrune(true);
            auto* root = engine.rootObjects().first();
            root->setProperty("completed", true);
            QMetaObject::invokeMethod(root, "finished");
        });
        timer->start(10);
    }
};

int main(int argc, char* argv[])
{
    StartupErrorApplication app;
    const int status = app.run(argc, argv);
    return status == EXIT_FAILURE && app.observed_error ? EXIT_SUCCESS : EXIT_FAILURE;
}
