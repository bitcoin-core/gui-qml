// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_APPLICATION_H
#define BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_APPLICATION_H

#include <qml/bitcoinqmlapplication.h>

#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QTemporaryDir;

enum class WalletMode { Disabled, Enabled };

class TestApplication : public BitcoinQmlApplication
{
public:
    using RuntimeTests = std::function<int(BitcoinQmlApplication&)>;

    explicit TestApplication(WalletMode wallet_mode, const QString& profile = {});
    ~TestApplication() override;

    int runTests(const char* executable, const QStringList& extra_arguments, RuntimeTests tests);

    static QString DataDir(const QString& profile);
    static QStringList RegtestArguments(const QString& profile);

protected:
    void onboardingCreated(QQmlApplicationEngine& engine) override;
    void windowCreated() override;

private:
    bool prepareProfile();
    bool waitForWallets();
    void executeTests(const std::function<int()>& tests);
    int finish(int application_status) const;

    std::unique_ptr<QTemporaryDir> m_temporary_profile;
    WalletMode m_wallet_mode;
    QString m_profile;
    RuntimeTests m_runtime_tests;
    QPointer<QQmlApplicationEngine> m_onboarding_engine;
    QPointer<QQmlApplicationEngine> m_engine;
    QPointer<QQuickWindow> m_window;
    int m_status{0};
    bool m_tests_ran{false};
};

#endif // BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_APPLICATION_H
