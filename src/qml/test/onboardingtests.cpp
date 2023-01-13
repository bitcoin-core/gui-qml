#include <QtQuickTest>
#include <QQmlEngine>
#include <QQmlContext>
#include <qml/test/testimageprovider.h>
#include <qml/test/onboardingtests.h>

Setup::Setup()
{
}

void Setup::qmlEngineAvailable(QQmlEngine * engine) {
    engine->addImageProvider(QStringLiteral("images"), new TestImageProvider());
}

QUICK_TEST_MAIN_WITH_SETUP(onboarding, Setup)
