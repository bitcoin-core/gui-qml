// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/onboardingoptionsmodel.h>

#include <QByteArray>
#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

static int argc = 1;
static char arg0[] = "fuzz_onboarding_options_model";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

static QTemporaryDir g_settings_dir;

static void ClearSettingsDir()
{
    if (!g_settings_dir.isValid()) return;
    QDir dir(g_settings_dir.path());
    for (const QString& entry : dir.entryList(QDir::Files)) {
        dir.remove(entry);
    }
}

static QString FuzzString(const uint8_t* data, size_t size)
{
    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));
    return QString::fromUtf8(raw);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1) return 0;

    if (!g_settings_dir.isValid()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, g_settings_dir.path());
    }
    ClearSettingsDir();

    const std::vector<std::string> argv{"bitcoinqml", "-regtest"};
    OnboardingOptionsModel model(argv, /*can_listen_ipc=*/false);

    const uint8_t op = data[0] % 12;
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
        (void)model.getCustomDataDirString();
        break;
    case 6:
        model.setProxyAddress(FuzzString(payload, payload_size));
        break;
    case 7:
        model.setTorAddress(FuzzString(payload, payload_size));
        break;
    case 8:
        model.selectCustomDataDir(FuzzString(payload, payload_size));
        break;
    case 9:
        model.selectCustomDataDir(FuzzString(payload, payload_size));
        break;
    case 10:
        model.useDefaultDataDir();
        break;
    case 11:
        model.setCustomDataDirArgs(FuzzString(payload, payload_size));
        break;
    }

    (void)model.coreSettingStatuses();
    (void)model.storageStatus();
    (void)model.canFinish();
    return 0;
}
