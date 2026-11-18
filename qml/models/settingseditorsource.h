// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SETTINGSEDITORSOURCE_H
#define BITCOIN_QML_MODELS_SETTINGSEDITORSOURCE_H

#include <QObject>

class SettingsEditorModel;

/** What a SettingsNavigationController needs from the options: new settings
 * editors. OptionsQmlModel implements it; QML tests use a fake. */
class SettingsEditorSource : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    //! A new editor of the settings, owned by the source. Null while no
    //! editor can open.
    virtual SettingsEditorModel* createSettingsEditor() = 0;
};

#endif // BITCOIN_QML_MODELS_SETTINGSEDITORSOURCE_H
