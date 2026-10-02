// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QApplication>
#include <QEventLoop>

#include <chainparams.h>
#include <qml/backendexecutor.h>
#include <test/qt_test_registry.h>
#include <util/translation.h>

const TranslateFn G_TRANSLATION_FUN{nullptr};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("BitcoinCoreGuiQmlTests");
    QCoreApplication::setApplicationName("bitcoinqml_unit_tests");
    SelectParams(ChainType::REGTEST);

    const auto classes = qEnvironmentVariable("BITCOIN_QML_TEST_CLASSES").split(',', Qt::SkipEmptyParts);
    int status = 0;
    bool ran{false};
    for (const auto& test : qttestregistry::SortedEntries()) {
        if (!classes.isEmpty() && !classes.contains(QString::fromUtf8(test.name))) continue;
        ran = true;
        status |= test.run(argc, argv);
    }
    // The last test may retire an executor or thread owner before its accepted
    // work and final cleanup finish. Keep Qt alive through those shared drains.
    QEventLoop loop;
    bool drained{false};
    BackendExecutor::shutdownAll(&loop, [&] { drained = true; loop.quit(); });
    while (!drained) loop.exec();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return ran ? status : 1;
}
