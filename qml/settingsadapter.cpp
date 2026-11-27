// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/settingseditormodel.h>
SettingsEditorModel::Ports SettingsEditorModel::PublicPorts(interfaces::Node& node,ArgsManager& args,presentation::Post post)
{
    return {std::move(post),[&args]{return effectivesettings::Read(args);},
        [&args](const settingsedit::Save& save){return effectivesettings::Review(args,save.original,save.batch);},
        [&node,&args](const settingsedit::Save& save){return effectivesettings::Commit(node,args,save.original,save.batch);}};
}
