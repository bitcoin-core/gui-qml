// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtQuickTest/quicktest.h>

#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStringLiteral>

class QmlTestsSetup : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void applicationAvailable()
    {
        Q_INIT_RESOURCE(bitcoin_qml);
        QQuickStyle::setStyle(QStringLiteral("Basic"));
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->rootContext()->setContextProperty(QStringLiteral("nodeModel"), static_cast<QObject*>(nullptr));
    }
};

int RunQmlTests(int argc, char* argv[])
{
    QmlTestsSetup setup;
    return quick_test_main_with_setup(argc, argv, "test_bitcoin_qt_qml", QUICK_TEST_SOURCE_DIR, &setup);
}

#include <qml_tests_main.moc>
