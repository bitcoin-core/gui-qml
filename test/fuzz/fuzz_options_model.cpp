// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/options_model.h>

#include <test/mocks/mocknode.h>

#include <chainparams.h>
#include <common/args.h>
#include <common/settings.h>
#include <init.h>
#include <qml/guiargs.h>

#include <QByteArray>
#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {
static int argc = 1;
static char arg0[] = "fuzz_options_model";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

static bool params_selected = false;
static QTemporaryDir g_settings_dir;

void EnsureParamsSelected()
{
    if (!params_selected) {
        SelectParams(ChainType::REGTEST);
        params_selected = true;
    }
}

void ClearSettingsDir()
{
    if (!g_settings_dir.isValid()) return;
    QDir dir(g_settings_dir.path());
    for (const QString& entry : dir.entryList(QDir::Files)) {
        dir.remove(entry);
    }
}

QString FuzzString(const uint8_t* data, size_t size)
{
    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));
    return QString::fromUtf8(raw);
}

bool PrepareArgs(ArgsManager& args, std::string& error)
{
    SetupServerArgs(args, /*can_listen_ipc=*/false);
    SetupQmlGuiArgs(args);
    const std::vector<std::string> argv{"bitcoinqml", "-regtest"};
    std::vector<const char*> raw_argv;
    raw_argv.reserve(argv.size());
    for (const std::string& arg : argv) raw_argv.push_back(arg.c_str());
    return args.ParseParameters(static_cast<int>(raw_argv.size()), raw_argv.data(), error);
}

void WireNode(MockNode& node, ArgsManager& args)
{
    node.get_persistent_setting_fn = [&args](const std::string& name) {
        return args.GetPersistentSetting(name);
    };
    node.update_rw_setting_fn = [&args](const std::string& name, const common::SettingsValue& value) {
        args.LockSettings([&](common::Settings& settings) {
            if (value.isNull()) {
                settings.rw_settings.erase(name);
            } else {
                settings.rw_settings[name] = value;
            }
        });
    };
    node.force_setting_fn = [&args](const std::string& name, const common::SettingsValue& value) {
        args.LockSettings([&](common::Settings& settings) {
            if (value.isNull()) {
                settings.forced_settings.erase(name);
            } else {
                settings.forced_settings[name] = value;
            }
        });
    };
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1) return 0;

    EnsureParamsSelected();

    // Isolate QSettings to a temporary directory so fuzzing does not touch
    // the user's real settings location.
    if (!g_settings_dir.isValid()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, g_settings_dir.path());
    }
    ClearSettingsDir();

    ArgsManager args;
    std::string error;
    if (!PrepareArgs(args, error)) return 0;
    args.SelectConfigNetwork(args.GetChainTypeString());

    MockNode node;
    WireNode(node, args);

    OptionsQmlModel model(node, args);

    const uint8_t op = data[0] % 16;
    const uint8_t* payload = data + 1;
    const size_t payload_size = size - 1;

    switch (op) {
    case 0:
        (void)model.validateProxyLocation(FuzzString(payload, payload_size));
        break;
    case 1:
        (void)model.commitProxyLocation(FuzzString(payload, payload_size));
        break;
    case 2:
        (void)model.commitTorLocation(FuzzString(payload, payload_size));
        break;
    case 3:
        (void)model.defaultProxyAddress();
        break;
    case 4:
        (void)model.validateCustomDataDir(FuzzString(payload, payload_size));
        break;
    case 5:
        (void)model.externalSignerPathValidationError(FuzzString(payload, payload_size));
        break;
    case 6:
        model.setExternalSignerPath(FuzzString(payload, payload_size));
        break;
    case 7:
        (void)model.languageLabel(FuzzString(payload, payload_size));
        break;
    case 8:
        (void)model.coreSettingStatus(FuzzString(payload, payload_size));
        break;
    case 9:
        (void)model.thirdPartyTransactionLinks(FuzzString(payload, payload_size));
        break;
    case 10: {
        if (payload_size < sizeof(int64_t)) return 0;
        int64_t satoshi = 0;
        for (size_t i = 0; i < sizeof(int64_t); ++i) {
            satoshi = (satoshi << 8) | payload[i];
        }
        (void)model.displayUnitLabelForAmount(satoshi);
        break;
    }
    case 11:
        model.setProxyAddress(FuzzString(payload, payload_size));
        break;
    case 12:
        model.setTorAddress(FuzzString(payload, payload_size));
        break;
    case 13:
        model.setDataDir(FuzzString(payload, payload_size));
        break;
    case 14:
        model.setThirdPartyTransactionUrls(FuzzString(payload, payload_size));
        break;
    case 15:
        model.setLanguage(FuzzString(payload, payload_size));
        break;
    }

    return 0;
}
