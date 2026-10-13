// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_PROCESS_H
#define BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_PROCESS_H

#include <QString>
#include <QStringList>

#include <vector>

struct IntegrationTestModule;
enum class WalletMode;

struct IntegrationTestMessage {
    QString type;
    QString text;
    QString source_file;
    int source_line{0};
};

enum class IntegrationTestStatus { Passed, Skipped, Failed };

struct IntegrationTestResult {
    IntegrationTestStatus status{IntegrationTestStatus::Failed};
    QString details;
    QString source_file{};
    int source_line{0};
    std::vector<IntegrationTestMessage> messages{};
};

struct IntegrationTestDiscovery {
    IntegrationTestResult result;
    QStringList tests;
};

// Called in the child before creating a Qt application. Logs are written as
// they happen, independently of the parent's captured pipes and result file.
bool PrepareIntegrationChild(const QString& profile, bool discover);

IntegrationTestDiscovery DiscoverIntegrationTests(const IntegrationTestModule& module, WalletMode mode,
                                                  const QString& class_name, const QStringList& functions,
                                                  const QString& profile_dir);

IntegrationTestResult RunIntegrationTest(const IntegrationTestModule& module, WalletMode mode,
                                         const QString& selector, const QString& profile_dir,
                                         const QStringList& options);

void ReportIntegrationMessages(const IntegrationTestResult& result);

#endif // BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_PROCESS_H
