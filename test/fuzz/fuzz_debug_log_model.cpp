// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/debuglogmodel.h>

#include <util/fs.h>

#include <QByteArray>
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_debug_log_model";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    static QTemporaryDir temp_dir;
    if (!temp_dir.isValid()) return 0;

    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));

    const QString file_path = temp_dir.filePath(QStringLiteral("debug.log"));
    QFile log_file(file_path);
    if (!log_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 0;
    log_file.write(raw);
    log_file.close();

    DebugLogModel model(fs::u8path(file_path.toStdString()));

    // Exercise property setters that parse/filter log lines.
    model.setFilter(QString::fromUtf8(raw));
    model.setWarningsAndErrorsOnly(size > 0 && (data[0] & 1));
    model.setLoadLimit(static_cast<int>(size < DebugLogModel::kMaxLoadLimit ? size : DebugLogModel::kMaxLoadLimit));

    model.setActive(true);
    model.refresh(/*full_load=*/size > 0 && (data[0] & 2));
    model.loadMore();
    model.openLogFile();
    model.setActive(false);

    return 0;
}
