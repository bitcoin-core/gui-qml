// Copyright (c) 2018-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/bitcoin.h>

#include <util/system.h>
#include <util/threadnames.h>
#include <util/translation.h>
#include <util/url.h>

#include <QCoreApplication>

#include <functional>
#include <string>
#include <tuple>

/** Translate string to current locale using Qt. */
extern const std::function<std::string(const char*)> G_TRANSLATION_FUN = [](const char* psz) {
    return QCoreApplication::translate("bitcoin-core", psz).toStdString();
};
UrlDecodeFn* const URL_DECODE = urlDecode;

int main(int argc, char* argv[])
{
#ifdef WIN32
    util::WinCmdLineArgs win_args;
    std::tie(argc, argv) = win_args.get();
#endif // WIN32

    SetupEnvironment();
    util::ThreadSetInternalName("main");

    return GuiMain(argc, argv);
}
