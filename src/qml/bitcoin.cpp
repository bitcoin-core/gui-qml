// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoin.h>

#include <qml/test/testbridge.h>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStringLiteral>
#include <QUrl>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>

namespace {
std::optional<QString> CommandLineOption(int argc, char* argv[], const QString& option)
{
    const QString prefix{option + QStringLiteral("=")};
    std::optional<QString> value;
    for (int i = 1; i < argc; ++i) {
        const QString argument{QString::fromLocal8Bit(argv[i])};
        if (argument == option) {
            value = QString{};
        } else if (argument.startsWith(prefix)) {
            value = argument.sliced(prefix.size());
        }
    }
    return value;
}

bool HasCommandLineOption(int argc, char* argv[], const QString& option)
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == option) return true;
    }
    return false;
}

std::optional<QString> GetTestAutomationSocket(int argc, char* argv[], QString& error)
{
    const auto socket_path{CommandLineOption(argc, argv, QStringLiteral("-test-automation"))};
    if (!socket_path) return std::nullopt;
    if (socket_path->isEmpty()) {
        error = QStringLiteral("The -test-automation option requires a socket path.");
        return std::nullopt;
    }
    if (!HasCommandLineOption(argc, argv, QStringLiteral("-regtest"))) {
        error = QStringLiteral("The -test-automation option is only available on regtest.");
        return std::nullopt;
    }
    return socket_path;
}
} // namespace

int QmlGuiMain(int argc, char* argv[])
{
    Q_INIT_RESOURCE(bitcoin_qml);

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationDisplayName(QGuiApplication::translate("bitcoin-core", "Bitcoin Core"));

    QString automation_error;
    const std::optional<QString> test_automation_socket{
        GetTestAutomationSocket(argc, argv, automation_error)};
    if (!automation_error.isEmpty()) {
        std::cerr << "Error: " << automation_error.toStdString() << '\n';
        return EXIT_FAILURE;
    }

    QQmlApplicationEngine engine;
    engine.load(QUrl{QStringLiteral("qrc:///qml/pages/MainWindow.qml")});
    if (engine.rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }

    std::unique_ptr<TestBridge> test_bridge;
    if (test_automation_socket) {
        test_bridge = std::make_unique<TestBridge>(engine, *test_automation_socket);
        if (!test_bridge->isListening()) return EXIT_FAILURE;
    }

    return app.exec();
}
