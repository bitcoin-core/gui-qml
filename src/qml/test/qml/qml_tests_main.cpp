// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtQuickTest/quicktest.h>

#include <qml/appmode.h>
#include <qml/buildinfo.h>
#include <qml/clipboard.h>
#include <qml/components/blockclockdial.h>
#include <qml/controls/linegraph.h>
#include <qml/guiconstants.h>

#include <QCoreApplication>
#include <QPixmap>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickStyle>
#include <QSettings>
#include <QStringLiteral>
#include <QTemporaryDir>
#include <qqml.h>

#include <memory>

class TestImageProvider : public QQuickImageProvider
{
public:
    TestImageProvider() : QQuickImageProvider{QQuickImageProvider::Pixmap} {}

    QPixmap requestPixmap(const QString&, QSize* size, const QSize& requested_size) override
    {
        const QSize image_size{requested_size.isValid() ? requested_size : QSize{1, 1}};
        if (size) *size = image_size;
        QPixmap pixmap{image_size};
        pixmap.fill(Qt::transparent);
        return pixmap;
    }
};

class QmlTestsSetup : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void applicationAvailable()
    {
        Q_INIT_RESOURCE(bitcoin_qml);
        Q_INIT_RESOURCE(bitcoin_compat);
        QCoreApplication::setOrganizationName(QStringLiteral(QAPP_ORG_NAME));
        QCoreApplication::setOrganizationDomain(QStringLiteral(QAPP_ORG_DOMAIN));
        QCoreApplication::setApplicationName(QStringLiteral(QAPP_APP_NAME_DEFAULT));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings_dir.path());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_settings_dir.path());
        QQuickStyle::setStyle(QStringLiteral("Basic"));

        m_app_mode = std::make_unique<AppMode>(AppMode::DESKTOP);
        m_build_info = std::make_unique<BuildInfo>();
        m_clipboard = std::make_unique<Clipboard>();
        qmlRegisterSingletonInstance("org.bitcoincore.qt", 1, 0, "AppMode", m_app_mode.get());
        qmlRegisterSingletonInstance("org.bitcoincore.qt", 1, 0, "BuildInfo", m_build_info.get());
        qmlRegisterSingletonInstance("org.bitcoincore.qt", 1, 0, "Clipboard", m_clipboard.get());
        qmlRegisterType<BlockClockDial>("org.bitcoincore.qt", 1, 0, "BlockClockDial");
        qmlRegisterType<LineGraph>("org.bitcoincore.qt", 1, 0, "LineGraph");
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->addImageProvider(QStringLiteral("images"), new TestImageProvider{});
    }

private:
    QTemporaryDir m_settings_dir;
    std::unique_ptr<AppMode> m_app_mode;
    std::unique_ptr<BuildInfo> m_build_info;
    std::unique_ptr<Clipboard> m_clipboard;
};

int RunQmlTests(int argc, char* argv[])
{
    QmlTestsSetup setup;
    return quick_test_main_with_setup(argc, argv, "test_bitcoin_qt_qml", QUICK_TEST_SOURCE_DIR, &setup);
}

#include <qml_tests_main.moc>
