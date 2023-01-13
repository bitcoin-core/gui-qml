// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
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
