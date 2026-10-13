// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/fixture/test_application.h>

#include <qml/models/nodemodel.h>
#include <qml/models/walletlistmodel.h>
#include <qml/walletqmlcontroller.h>
#include <util/translation.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <utility>
#include <vector>

const TranslateFn G_TRANSLATION_FUN{[](const char* text) {
    return QCoreApplication::translate("bitcoin-core", text).toStdString();
}};

TestApplication::TestApplication(WalletMode wallet_mode, const QString& profile)
    : m_wallet_mode{wallet_mode}, m_profile{profile}
{
    if (m_profile.isEmpty()) {
        m_temporary_profile = std::make_unique<QTemporaryDir>();
        m_profile = m_temporary_profile->path();
    }
}

TestApplication::~TestApplication() = default;

QString TestApplication::DataDir(const QString& profile)
{
    return profile + QString::fromUtf8("/node with spaces \xc3\xbc");
}

QStringList TestApplication::RegtestArguments(const QString& profile)
{
    return {"-regtest", "-datadir=" + DataDir(profile), "-listen=0", "-listenonion=0", "-connect=0",
            "-dnsseed=0", "-fixedseeds=0", "-discover=0", "-natpmp=0", "-printtoconsole=0"};
}

bool TestApplication::prepareProfile()
{
    if (m_profile.isEmpty() || !QDir{}.mkpath(DataDir(m_profile))) return false;
    // Isolate settings before the production code constructs QApplication.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    for (const auto format : {QSettings::IniFormat, QSettings::NativeFormat}) {
        QSettings::setPath(format, QSettings::UserScope, m_profile + "/gui");
        QSettings::setPath(format, QSettings::SystemScope, m_profile + "/system");
    }
    return true;
}

int TestApplication::runTests(const char* executable, const QStringList& extra_arguments, RuntimeTests tests)
{
    if (!prepareProfile()) return EXIT_FAILURE;
    m_runtime_tests = std::move(tests);
    auto arguments = RegtestArguments(m_profile);
    arguments.append(extra_arguments);
    arguments.append(m_wallet_mode == WalletMode::Enabled ? "-disablewallet=0" : "-disablewallet=1");
    std::vector<QByteArray> encoded_arguments{QByteArray{executable}};
    for (const auto& argument : arguments) encoded_arguments.push_back(argument.toUtf8());
    std::vector<char*> argv;
    for (auto& argument : encoded_arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    return finish(BitcoinQmlApplication::run(static_cast<int>(encoded_arguments.size()), argv.data()));
}

void TestApplication::executeTests(const std::function<int()>& tests)
{
    m_tests_ran = true;
    try {
        m_status |= tests();
    } catch (const std::exception& error) {
        std::cerr << "Integration test exception: " << error.what() << '\n';
        m_status = EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Unknown integration test exception\n";
        m_status = EXIT_FAILURE;
    }
}

bool TestApplication::waitForWallets()
{
    if (m_wallet_mode == WalletMode::Disabled) return true;
    auto* controller = qobject_cast<WalletQmlController*>(engine().rootContext()->contextProperty("walletController").value<QObject*>());
    auto* wallets = qobject_cast<WalletListModel*>(engine().rootContext()->contextProperty("walletListModel").value<QObject*>());
    return controller && wallets && QTest::qWaitFor([&] {
        return controller->initialized() && wallets->walletDirLoaded();
    }, 30'000);
}

void TestApplication::onboardingCreated(QQmlApplicationEngine& engine)
{
    m_onboarding_engine = &engine;
    m_status = EXIT_FAILURE;
    std::cerr << "Unexpected onboarding window\n";
    QTimer::singleShot(0, &engine, [&engine] {
        qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst())->close();
    });
}

void TestApplication::windowCreated()
{
    m_engine = &engine();
    m_window = window();
    if (m_onboarding_engine) {
        std::cerr << "Onboarding engine still exists when the runtime window is ready\n";
        m_status = EXIT_FAILURE;
    }
    auto initialized = std::make_shared<QSignalSpy>(&nodeModel(), &NodeModel::nodeInitialized);
    QTimer::singleShot(0, &engine(), [this, initialized] {
        if (!QTest::qWaitFor([&] { return !initialized->isEmpty(); }, 30'000) || nodeModel().errorState()) {
            std::cerr << "The application did not initialize its regtest node\n";
            m_status = EXIT_FAILURE;
        } else if (!waitForWallets()) {
            std::cerr << "The application did not initialize its wallet models\n";
            m_status = EXIT_FAILURE;
        } else {
            executeTests([&] { return m_runtime_tests(*this); });
        }
        requestShutdown();
    });
}

int TestApplication::finish(int application_status) const
{
    const bool torn_down = !m_onboarding_engine && !m_engine && !m_window;
    if (!torn_down) std::cerr << "Application window or QML engine survived shutdown\n";
    return m_status | application_status | (m_tests_ran && torn_down ? EXIT_SUCCESS : EXIT_FAILURE);
}
