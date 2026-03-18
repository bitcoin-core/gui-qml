// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QApplication>

#include <chainparams.h>
#include <test/gmocktestfixture.h>
#include <test/qt_test_registry.h>
#include <util/translation.h>

const TranslateFn G_TRANSLATION_FUN{nullptr};

int main(int argc, char* argv[])
{
    testing::InitGoogleMock(&argc, argv);
    testing::UnitTest::GetInstance()->listeners().Append(new QtestGmockListener());
    QApplication app(argc, argv);
    SelectParams(ChainType::REGTEST);

    int status = 0;
    for (const auto& test : qttestregistry::SortedEntries()) {
        status |= test.run(argc, argv);
    }
    return status;
}
