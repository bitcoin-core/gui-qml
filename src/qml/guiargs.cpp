// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/guiargs.h>

#include <common/args.h>

void SetupQmlGuiArgs(ArgsManager& argsman)
{
    argsman.AddArg("-choosedatadir", "Choose data directory on startup (default: 0)", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-lang=<lang>", "Set language, for example \"de_DE\" (default: system locale)", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-min", "Start minimized", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-resetguisettings", "Reset all settings changed in the GUI", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddHiddenArgs({"-qml_onboarded"});
    argsman.AddArg("-test-automation=<path>", "Enable the GUI test automation bridge at the given local socket path", ArgsManager::ALLOW_ANY | ArgsManager::DISALLOW_NEGATION | ArgsManager::DISALLOW_ELISION | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
    argsman.AddArg("-test-settings-dir=<dir>", "Store QSettings in this directory while GUI test automation is enabled", ArgsManager::ALLOW_ANY | ArgsManager::DISALLOW_NEGATION | ArgsManager::DISALLOW_ELISION | ArgsManager::DEBUG_ONLY, OptionsCategory::GUI);
}
