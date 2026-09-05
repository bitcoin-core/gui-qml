// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SETTINGS_KEYS_H
#define BITCOIN_QML_MODELS_SETTINGS_KEYS_H

// QSettings key names for display settings persisted by OptionsQmlModel.
// Defined in a standalone header (no bitcoin build dependencies) so that
// unit tests can reference them without pulling in bitcoin internals.
namespace SettingsKeys {
    inline constexpr const char* LANGUAGE     = "language";
    inline constexpr const char* DISPLAY_UNIT = "DisplayBitcoinUnit";
    inline constexpr const char* DATA_DIR = "strDataDir";
} // namespace SettingsKeys

#endif // BITCOIN_QML_MODELS_SETTINGS_KEYS_H
