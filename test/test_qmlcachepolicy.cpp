// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/bitcoin.h>

#include <QByteArray>
#include <QtGlobal>

namespace {
class EnvironmentVariableGuard
{
public:
    explicit EnvironmentVariableGuard(const char* name)
        : m_name{name}, m_was_set{qEnvironmentVariableIsSet(name)}, m_value{qgetenv(name)}
    {
    }

    ~EnvironmentVariableGuard()
    {
        if (m_was_set) {
            qputenv(m_name.constData(), m_value);
        } else {
            qunsetenv(m_name.constData());
        }
    }

private:
    const QByteArray m_name;
    const bool m_was_set;
    const QByteArray m_value;
};
} // namespace

class QmlCachePolicyTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void ignoresHostGeneratedCache();
};

void QmlCachePolicyTests::ignoresHostGeneratedCache()
{
    const EnvironmentVariableGuard disk_cache{"QML_DISK_CACHE"};
    const EnvironmentVariableGuard disable_disk_cache{"QML_DISABLE_DISK_CACHE"};
    const EnvironmentVariableGuard force_disk_cache{"QML_FORCE_DISK_CACHE"};

    qputenv("QML_DISK_CACHE", "qmlc");
    qputenv("QML_DISABLE_DISK_CACHE", "1");
    qputenv("QML_FORCE_DISK_CACHE", "1");

    ConfigureQmlCachePolicy();

    QVERIFY(!qEnvironmentVariableIsSet("QML_FORCE_DISK_CACHE"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    QCOMPARE(qgetenv("QML_DISK_CACHE"), QByteArray{"aot"});
    QVERIFY(!qEnvironmentVariableIsSet("QML_DISABLE_DISK_CACHE"));
#else
    QVERIFY(!qEnvironmentVariableIsSet("QML_DISK_CACHE"));
    QCOMPARE(qgetenv("QML_DISABLE_DISK_CACHE"), QByteArray{"1"});
#endif
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(QmlCachePolicyTests)
#else
QTEST_MAIN(QmlCachePolicyTests)
#endif
#include "test_qmlcachepolicy.moc"
