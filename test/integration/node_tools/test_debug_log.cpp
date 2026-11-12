// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_util.h>
#include <qml/models/debuglogmodel.h>
#include <common/args.h>
#include <common/system.h>
#include <logging.h>

#include <QDateTime>
#include <QFile>

class DebugLogIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

public:
    explicit DebugLogIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void fileLoggingDisabledHidesStaleContent()
    {
        QFile stale{QString::fromStdString(fs::PathToString(gArgs.GetDataDirNet() / "debug.log"))};
        QVERIFY(stale.open(QIODevice::WriteOnly));
        QVERIFY(stale.write("2026-01-01T00:00:00Z stale history marker\n") > 0);
        stale.close();
        auto* model = qmltest::Model<DebugLogModel>(m_app, "debugLogModel");
        QVERIFY(model);
        QVERIFY(!model->fileLoggingEnabled());
        QVERIFY(qmltest::SelectSettings(m_app, "debug-log"));
        QVERIFY(qmltest::WaitForVisible(m_app, "debugLogView"));
        QTRY_COMPARE(qmltest::Find(m_app, "debugLogOpenErrorBanner")->property("opacity").toDouble(), 1.0);
        QCOMPARE(qmltest::Find(m_app, "debugLogOpenErrorText")->property("text").toString(), QStringLiteral("Writing the debug log to a file is turned off."));
        QCOMPARE(model->rowCount(), 0);
        QVERIFY(!model->logAvailable());
        for (const auto* control : {"debugLogSearchBar", "debugLogFilterButton", "debugLogOpenFileButton"}) {
            auto* object = qmltest::Find(m_app, control);
            QVERIFY2(object, control);
            QVERIFY2(!object->property("enabled").toBool(), control);
        }
        QVERIFY(qmltest::SelectSettings(m_app, "display"));
        QVERIFY(qmltest::SelectSettings(m_app, "debug-log"));
        QVERIFY(qmltest::WaitForVisible(m_app, "debugLogView"));
        QTRY_COMPARE(qmltest::Find(m_app, "debugLogOpenErrorBanner")->property("opacity").toDouble(), 1.0);
        QVERIFY(!qmltest::Find(m_app, "debugLogSearchBar")->property("enabled").toBool());
        QCOMPARE(model->rowCount(), 0);
    }

    void customLogPathExcludesTheDefaultLog()
    {
        QFile stale{QString::fromStdString(fs::PathToString(gArgs.GetDataDirNet() / "debug.log"))};
        QVERIFY(stale.open(QIODevice::WriteOnly));
        QVERIFY(stale.write("2026-01-01T00:00:00Z stale history marker\n") > 0);
        stale.close();
        QFile log{QString::fromStdString(fs::PathToString(LogInstance().m_file_path))};
        QVERIFY(log.fileName() != stale.fileName());
        QVERIFY(log.open(QIODevice::WriteOnly | QIODevice::Append));
        QVERIFY(log.write("2026-01-01T00:00:00Z configured log marker\n") > 0);
        log.close();
        auto* model = qmltest::Model<DebugLogModel>(m_app, "debugLogModel");
        QVERIFY(model);
        QVERIFY(qmltest::SelectSettings(m_app, "debug-log"));
        QVERIFY(qmltest::WaitForVisible(m_app, "debugLogView"));
        QTRY_VERIFY(model->rowCount() > 0);
        QVERIFY(model->openError().isEmpty());
        QVERIFY(qmltest::SetText(m_app, "debugLogSearchField", "stale history marker"));
        QTRY_COMPARE(model->rowCount(), 0);
        QVERIFY(qmltest::SetText(m_app, "debugLogSearchField", "configured log marker"));
        QTRY_COMPARE(model->rowCount(), 1);
        QVERIFY(qmltest::SetText(m_app, "debugLogSearchField", ""));
        QTRY_VERIFY(model->rowCount() > 0);
    }

    void liveLogFilteringAndPagination()
    {
        QFile log{QString::fromStdString(fs::PathToString(gArgs.GetDataDirNet() / "debug.log"))};
        QVERIFY(log.open(QIODevice::WriteOnly | QIODevice::Append));
        for (int i{0}; i < 1200; ++i) {
            const auto line = QString{"2026-01-01T00:00:00.123456Z [net] integration history %1\n"}.arg(i).toUtf8();
            QCOMPARE(log.write(line), line.size());
        }
        QVERIFY(log.flush());
        auto* model = qmltest::Model<DebugLogModel>(m_app, "debugLogModel");
        QVERIFY(model);
        QVERIFY(qmltest::SelectSettings(m_app, "debug-log"));
        QVERIFY(qmltest::WaitForVisible(m_app, "debugLogView"));
        QTRY_VERIFY(model->active());
        QTRY_COMPARE(model->rowCount(), model->loadLimit());
        QVERIFY(model->hasMoreLines());
        QVERIFY(qmltest::SetText(m_app, "debugLogSearchField", "integration live"));
        QTRY_COMPARE(model->rowCount(), 0);
        const QByteArray lines{"2026-01-01T00:00:00.654321Z [net:warning] integration live warning\n"
                               "2026-01-01T00:00:01.654321Z [rpc:error] integration live error\n"
                               "2026-01-01T00:00:02.654321Z [net] integration live normal\n"};
        QCOMPARE(log.write(lines), lines.size());
        log.close();
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 3, 10'000);
        QCOMPARE(model->data(model->index(0), DebugLogModel::TimestampRole).toString(),
                 QDateTime::fromString("2026-01-01T00:00:00.654321Z", Qt::ISODateWithMs).toLocalTime().toString("HH:mm:ss"));
        model->setWarningsAndErrorsOnly(true);
        QTRY_COMPARE(model->rowCount(), 2);
        QVERIFY(model->data(model->index(1), DebugLogModel::IsErrorRole).toBool());
        QVERIFY(model->data(model->index(0), DebugLogModel::IsWarningRole).toBool());
        model->setWarningsAndErrorsOnly(false);
        QVERIFY(qmltest::SetText(m_app, "debugLogSearchField", ""));
        model->loadMore();
        QTRY_VERIFY(model->rowCount() > 1200);
        QVERIFY(qmltest::SetText(m_app, "debugLogSearchField", "integration history 0"));
        QTRY_COMPARE(model->rowCount(), 1);
        QVERIFY(qmltest::ReturnToNode(m_app));
        QTRY_VERIFY(!model->active());
    }
};

QStringList DebugLogTestArguments(const QString& selector)
{
    if (selector == "fileLoggingDisabledHidesStaleContent") return {"-nodebuglogfile"};
    if (selector == "customLogPathExcludesTheDefaultLog") return {"-debuglogfile=integration-custom.log"};
    return {};
}

std::unique_ptr<QObject> CreateDebugLogTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<DebugLogIntegrationTests>(app);
}

#include <test_debug_log.moc>
