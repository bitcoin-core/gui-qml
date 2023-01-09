#include <QtQuickTest>
#include <QQmlEngine>
#include <QQmlContext>
#include <qml/test/imageprovider.h>
#include <qml/test/onboardingtests.h>
#include <qt/networkstyle.h>

Setup::Setup()
{
}

void Setup::qmlEngineAvailable(QQmlEngine * engine) {
    engine->addImageProvider(QStringLiteral("images"), new TestImageProvider());
}

QUICK_TEST_MAIN_WITH_SETUP(onboarding, Setup)
